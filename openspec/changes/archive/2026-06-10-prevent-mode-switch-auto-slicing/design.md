## Context

### 模式切換入口

`MainFrame::phrozen_apply_work_mode(bool resin)` (MainFrame.cpp:1885) 是 Phrozen mode-toggle 按鈕的唯一進入點。它依序：載入目標 preset 選擇 → `pb->load_selections()` → `load_current_presets()` → `set_printer_technology()` → UI 更新。

### 現有的「是否保存」流程

`Plater::close_with_confirm(std::function<bool(bool)> second_check = nullptr)` (Plater.cpp:12082) 是公開方法：
- 若 `up_to_date(false, false)` 為 true（無未保存變更）：直接返回 `wxID_NO`，不顯示對話框。
- 若有未保存變更：顯示 YES / NO / CANCEL 對話框（可記住選擇）。
  - YES → 呼叫 `save_project()`，若 save 被取消則返回 `wxID_CANCEL`。
  - NO → 放棄未保存變更，返回 `wxID_NO`。
  - CANCEL → 返回 `wxID_CANCEL`，呼叫端應中止動作。

### 清空平台流程

`Plater::new_project(bool skip_confirm=false, bool silent=false, ...)` (Plater.cpp:9623) 是公開方法：
- `skip_confirm=true`：跳過內部的 `close_with_confirm(check)` 呼叫（我們已在外層處理）。
- `silent=true`：跳過 `select_tab(tp3DEditor)` 的 tab 導航（我們自行導向 Home）。
- 清除所有 notification，呼叫 `reset(false)` 清空模型物件與支撐狀態，重置 dirty flags，不改變 preset 內容。

### Home 頁導航

`MainFrame::TabPosition::tpHome = 0` (MainFrame.hpp:218)。`m_tabpanel->SetSelection(tpHome)` 的 `wxEVT_BOOKCTRL_PAGE_CHANGED` handler (MainFrame.cpp:1311) 對 tpHome 不 post `EVT_GLVIEWTOOLBAR_PREVIEW`，也不啟動任何 slicing。Home 是靜態 WebView，切換安全。

### Preview/slicing 觸發路徑（前版調查結論）

`load_current_presets()` → `Tab::load_current_preset()` (Tab.cpp:5090) 在技術切換時呼叫 `m_tabpanel->SetSelection(GetPageCount()-1)` → 若 `GetPageCount()-1 == tpPreview`，則 `EVT_GLVIEWTOOLBAR_PREVIEW` 被 post → `select_view_3D("Preview", false)` → `set_current_panel(preview, false)` → `do_reslice()` (若有物件)。

新設計的三重保護：
1. `set_skip_preview_reslice(true)` 抑制 `do_reslice` 被呼叫一次（one-shot guard）。
2. `new_project(true, true)` 在 `load_current_presets` 之後清空物件 → `do_reslice` 的 `objects.empty()` guard 天然阻止。
3. 最終導向 Home（不是 Prepare/Preview），即使 `set_current_panel(preview, ...)` 被呼叫，m_plater 也被 Home tab 覆蓋，使用者不會看到 Preview。

### 現有 code 修改狀況

前一輪已在 MainFrame.cpp 做了以下修改（git diff）：
- ✅ 移除 dead `saved_plater_tab` 變數 — 新設計仍適用，保留。
- ✅ `m_plater->set_skip_preview_reslice(true)` — 新設計仍有用，保留作為第一層 guard。
- ❌ `m_tabpanel->SetSelection(tp3DEditor)` — 新設計要去 Home，需改為 `tpHome` 或移除後在正確位置新增。

## Goals / Non-Goals

**Goals:**
- mode switch 前若有未保存變更，向使用者詢問保存；使用者 Cancel 則中止切換。
- mode switch 後清空平台（物件、支撐、切片狀態）。
- mode switch 後導向 Home 頁，不停在 Prepare，不進入 Preview。
- mode switch 流程中不觸發 slicing / reslice。
- FDM/Resin preset refresh、toolbar、sidebar、UI gating 仍正確更新為新模式。
- 手動切片（Slice 按鈕、Ctrl+R）在 mode switch 後仍照既有流程運作。

**Non-Goals:**
- 改變 `close_with_confirm()` 或 `new_project()` 本身的邏輯。
- 改變 `set_current_panel()` 或 `do_reslice` lambda 的邏輯。
- 支援跨模式保留模型（本次不在範圍）。
- 改變 mode switch 以外任何流程的 slicing 行為。

## Decisions

### Decision 1: 在 `phrozen_apply_work_mode` 中，直接呼叫 `close_with_confirm()` + `new_project(skip_confirm=true, silent=true)`

**Chosen**：不呼叫完整的 `new_project()`（會雙重 prompt），而是：
1. 先自行呼叫 `m_plater->close_with_confirm(nullptr)` 取得使用者決定。
2. 若 CANCEL 則 return。
3. 繼續 preset loading（`pb->load_selections`、`load_current_presets`、`set_printer_technology`）。
4. 呼叫 `m_plater->new_project(/*skip_confirm=*/true, /*silent=*/true)` 做清空與 dirty reset，不重複 prompt，不觸發 tab 導航。
5. 最後 `m_tabpanel->SetSelection(tpHome)`。

**Rationale**：`new_project(skip_confirm=true, silent=true)` 提供完整的 platform reset（model 清空、notification 清除、dirty flag 重設），而不需要自行拼湊 `reset()`、`reset_project_dirty_*()`、`notification_manager` 呼叫，降低漏掉清理步驟的風險。

**Alternative considered**：直接呼叫 `m_plater->reset(false)` + 手動清除 notification。已否決 — 可能漏掉 `new_project` 中的其他清理步驟（dirty flag reset、`update_saved_preset_from_current_preset()` 等），需要跟隨 `new_project` 原始碼更新。

### Decision 2: Preset loading 順序 — `load_current_presets` 在 `new_project` 之前

**Chosen**：
```
close_with_confirm()         ← 保存詢問
pb->load_selections(...)     ← 設定 preset bundle 選擇
pb->export_selections(...)   ← 寫入 app_config
cfg->set(phrozen_work_mode)  ← 記錄模式
cfg->save()
set_skip_preview_reslice(true) ← guard
load_current_presets(...)    ← 載入新 preset UI（此時仍有舊物件）
set_printer_technology(...)
new_project(true, true)      ← 清空物件、reset dirty state
UI updates (sidebar, topbar, etc.)
m_tabpanel->SetSelection(tpHome)
```

**Rationale**：preset loading 先完成，確保 `set_printer_technology` 時 preset bundle 已是新技術。`new_project(true, true)` 之後 model 為空，`load_current_presets` 的副作用（可能 post EVT_GLVIEWTOOLBAR_PREVIEW）已在 `set_skip_preview_reslice` + `objects.empty()` 雙重保護下無害。

### Decision 3: `set_skip_preview_reslice(true)` 保留為第一層 guard

**Chosen**：保留，置於 `load_current_presets` 之前。

**Rationale**：`new_project(true, true)` 在 `load_current_presets` 之後呼叫，所以 `EVT_GLVIEWTOOLBAR_PREVIEW` 從 queue 被處理時 `new_project` 已完成（objects cleared）。`objects.empty()` 天然阻止 reslice。但 `set_skip_preview_reslice` 作為額外 one-shot guard 可防止 do_reslice 被呼叫到中間的 reload_print() / update_fff_scene() 等副作用，保留成本低，防護價值高。

### Decision 4: 不修改 `Plater.cpp` 的 slicing infrastructure

與前版 Decision 1 相同，所有修改限制在 `MainFrame::phrozen_apply_work_mode` 函式內。

## Risks / Trade-offs

- **`new_project(true, true)` 的副作用**：`new_project` 呼叫 `wxGetApp().update_saved_preset_from_current_preset()` 和 `update_project_dirty_from_presets()`，這些在 preset 已切換的狀態下是否有預期外行為，需在實作時驗證。緩解：保持 `load_current_presets` 在 `new_project` 之前，確保 preset bundle 已設定好。
- **`close_with_confirm` 的 `second_check` 為 nullptr**：不傳入 second_check，表示沒有「modified preset 轉移」對話框。這對 mode switch 是正確行為（我們不保留舊模式的 preset 到新模式），但需確認 `second_check=nullptr` 路徑沒有 null dereference（已驗證：Plater.cpp:12085 有 `if (second_check && ...)` 保護）。
- **短暫 set_current_panel(preview) 副作用**：`EVT_GLVIEWTOOLBAR_PREVIEW` 在 queue 中被處理時，即使 objects.empty() 阻止 reslice，`set_current_panel(preview, false)` 仍可能呼叫 `preview->reload_print(true)` 或 `reset_gcode_toolpaths()`（Plater.cpp:6844-6854）。空平台 reload 應為 no-op，但需驗證。
- **Tab 閃爍**：`SetSelection(tpHome)` 之後，若 EVT_GLVIEWTOOLBAR_PREVIEW 從 queue 處理，內部 `current_panel` 切換到 preview，但 m_tabpanel 的 notebook 頁已是 Home。使用者看不到 Preview 閃爍，但 m_plater 內部狀態為 preview。當使用者之後點擊 Prepare 時，tab_change handler 會 post EVT_GLVIEWTOOLBAR_3D → select_view_3D("3D") 正確重設，無問題。

## Migration Plan

變更僅限於 `src/slic3r/GUI/MainFrame.cpp` 的 `phrozen_apply_work_mode` 函式，淨變更約 6–8 行（前版移除了 5 行 dead code，新增約 3 行）。

**前版修改的處理**：
- `saved_plater_tab` 移除 — 保留 ✓
- `set_skip_preview_reslice(true)` 新增 — 保留 ✓  
- `SetSelection(tp3DEditor)` 新增 — **需改為 `SetSelection(tpHome)`** 或移除後重新在正確位置加入

Rollback：還原整個函式至原始版本。

## Open Questions

1. **`new_project(true, true)` 呼叫時 preset bundle 已被 `load_selections` 修改**：`new_project` 內部的 `reset(false)` 是否會觸發任何基於現有 preset 的重算或 UI 更新？需在實作時確認 `reset(false)` 不呼叫 `load_current_presets` 或類似操作。
2. **`update_saved_preset_from_current_preset()` 時機**：此函式在 `new_project` 內呼叫，此時 presets 已是新模式的。是否會有預設值寫入 app_config 的問題？
3. **空平台的 `preview->reload_print(true)`**：若 `EVT_GLVIEWTOOLBAR_PREVIEW` queue event 觸發，`set_current_panel` 在空平台情況下呼叫 `reload_print` 是否安全（不崩潰）？