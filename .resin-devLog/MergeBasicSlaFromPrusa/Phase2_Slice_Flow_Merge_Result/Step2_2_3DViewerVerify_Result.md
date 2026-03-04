# Step 2.2: 3D Viewer SLA 基本驗證 — 結果報告

**執行日期**: 2026-02-16
**前置條件**: Step 2.1 編譯通過

---

## 程式碼預檢 (自動化分析)

| # | 檢查項目 | 狀態 | 說明 |
|---|----------|------|------|
| 1 | Gizmo activability (`on_is_activable`) | ✅ Pass | SlaSupports/Hollow 正確檢查 `ptSLA` |
| 2 | Icon 檔案存在 | ✅ Pass | `resources/images/sla_supports.svg`, `hollow.svg` |
| 3 | `switch_gizmos_icon_filename()` | ✅ Pass | SlaSupports/Hollow case 已處理 |
| 4 | `GLCanvas3D::sla_print()` | ✅ Pass | 方法存在，有 nullptr 檢查 |
| 5 | `GLVolume::get_sla_shift_z()` | ✅ Pass | getter/setter 已實作 |
| 6 | SLAPrintObject API 完整性 | ✅ Pass | 6 個方法全部存在 |
| 7 | `MeshClipper::render_cut(ColorRGBA)` | ✅ Pass | 簽名匹配 |
| 8 | SelectionInfo 呼叫鏈完整 | ✅ Pass | sla_print → get_print_object → elevation |
| 9 | Gizmo event routing | ✅ Pass | 鍵盤/滑鼠事件已啟用 |
| 10 | Gizmo registration (emplace_back) | ✅ Pass | sprite_id 順序正確 |

**預檢結論**: 無阻斷問題，可進行手動驗證。

---

## 驗證過程中修復的問題

在手動測試的過程中，發現並修復了 5 個 SLA 模式下的 crash / 錯誤訊息問題。
以下修正是各 Section 通過的前提。

### Fix 1: PrintApply.cpp — `seam_slope_type` nullptr crash

**問題**: 載入模型時 crash (`PrintApply.cpp:1114`)
**原因**: `seam_slope_type` 是 FDM-only 參數，SLA config 中不存在。`obj->get_config_value()` 回傳 nullptr 後直接 `->value` 解引用導致 crash。
**根本原因**: `Print::apply()` 是 FFF-only 邏輯，但 PartPlate 在 SLA 模式下仍建立 FFF Print 物件（Step 2.3 已知問題），導致 FDM 驗證在 SLA 模式下被觸發。

**修改檔案**: `PhrozenOrca/src/libslic3r/PrintApply.cpp`
**修改內容**:
- 在存取 `seam_slope_type` 前加入 `new_full_config.option("seam_slope_type") != nullptr` 守衛
- 整段 scarf joint seam 邏輯包在條件內，SLA 模式下跳過
- `get_config_value` 結果加 nullptr 檢查：`auto opt = ...; return (opt && opt->value != ...)`

### Fix 2: bbs_3mf.cpp — `filament_colour` nullptr crash (2 處)

**問題**: 載入模型後 crash (`bbs_3mf.cpp:7655`)
**原因**: `filament_colour` 是 FDM-only 參數，SLA config 中不存在。`dynamic_cast<const ConfigOptionStrings*>(config.option("filament_colour"))->values.size()` 在 nullptr 上解引用。

**修改檔案**: `PhrozenOrca/src/libslic3r/Format/bbs_3mf.cpp`
**修改位置**:
1. `_add_model_config_file_to_archive()` (原 line 7655) — filament map 寫入
2. `_add_slice_info_config_file_to_archive()` (原 line 7837) — slice info filament map 寫入

**修改內容**:
- 兩處都改為先取得 `filament_colour` option pointer
- 有值時才寫入 filament map XML
- SLA 模式下跳過（SLA 沒有 filament 概念）

### Fix 3: BackgroundSlicingProcess.cpp — FFF validate 在 SLA 模式下觸發

**問題**: 啟動時（預設 SLA printer）出現 FDM 錯誤訊息：「Relative extruder addressing requires resetting the extruder position...」
**原因**: `BackgroundSlicingProcess::validate()` 原本 assert `m_print == m_fff_print` 並無條件呼叫 FFF `Print::validate()`。啟動時 `m_printer_tech` 預設為 `ptFFF`，`select_technology(ptSLA)` 尚未被呼叫，導致 FFF 驗證邏輯在 SLA 模式下執行。
**深層原因**: PartPlate 系統從未為 SLA 實作 `set_sla_print()` 呼叫（`Plater.cpp:3055-3058` 被整段註解），所以 `m_sla_print` 始終為 nullptr。

**修改檔案**: `PhrozenOrca/src/slic3r/GUI/BackgroundSlicingProcess.cpp`
**修改內容**:
- 改用 `preset_bundle->printers.get_edited_preset().printer_technology()` 取得實際 printer technology（不依賴可能尚未更新的 `m_printer_tech`）
- SLA 模式：使用 `m_sla_print->validate()` 或回傳空（SLAPrint 未初始化時）
- FFF 模式：維持原本邏輯不變

### Fix 4: PresetBundle.cpp — SLA 3MF 載入時誤報 customized preset 警告

**問題**: 載入 SL1 printer 存出的 3MF project 時彈出警告：「The 3mf has the following customized filament or printer presets」
**原因**: `validate_presets()` 整個函數是針對 FDM 設計的：
- 用 `filament_diameter` 計算 filament 數量（SLA 沒有此參數）
- 用 `filaments` preset 集合驗證（SLA 用 `sla_materials`，不是 `filaments`）
- SLA material name 在 filaments 集合中找不到 → 回傳 `VALIDATE_PRESETS_FILAMENTS_NOT_FOUND`

**修改檔案**: `PhrozenOrca/src/libslic3r/PresetBundle.cpp`
**修改內容**:
- 在 `validate_presets()` 開頭偵測 `Preset::printer_technology(config)`
- SLA 直接回傳 `VALIDATE_PRESETS_SUCCESS`，跳過所有 FDM-centric 驗證
- FDM 邏輯完全不變

### Fix 5: GLGizmosCommon.cpp — `sla_print()` nullptr crash

**問題**: 啟用 Flatten 等 gizmo 時 crash (`GLGizmosCommon.cpp:129`)，FDM 和 SLA 模式都會發生
**原因**: `SelectionInfo::on_update()` 無條件呼叫 `sla_print()->get_print_object_by_model_object_id()`。`sla_print()` 回傳 nullptr（因為 PartPlate 從未呼叫 `set_sla_print()`），在 nullptr 上存取 `m_objects` 導致 crash。此函式在任何 gizmo 啟用時都會執行，因此 FDM/SLA 都受影響。

**修改檔案**: `PhrozenOrca/src/slic3r/GUI/Gizmos/GLGizmosCommon.cpp`
**修改內容**:
- 取出 `sla_print()` 結果先判斷 nullptr
- 有值時才呼叫 `get_print_object_by_model_object_id()`
- `m_print_object` 在 nullptr 時保持預設值 nullptr，後續邏輯已有對應處理

### ⚠️ E4 Crash (未修復 — 移至 Step 2.3 一併解決)

**問題**: SLA 模式下按 A 鍵自動生成支撐時 crash (`GLGizmoSlaSupports.cpp:1088`)
**原因**: `has_backend_supports()` 呼叫 `m_parent.sla_print()->objects()`，`sla_print()` 回傳 nullptr。同檔案 `get_data_from_backend()` (line 1110) 也有相同問題。
**決定**: 此問題的根本原因是 PartPlate 從未初始化 `SLAPrint`，與 Step 2.3 的核心修復範圍完全重疊。不再逐個加 nullptr 守衛，直接在 Step 2.3 從根本解決 — 讓 PartPlate 正確建立 `SLAPrint` 物件並呼叫 `set_sla_print()`。

---

## 手動測試清單

### A. 基本啟動與切換

| # | 測試步驟 | 預期結果 | 實測結果 | 備註 |
|---|----------|----------|----------|------|
| A1 | 啟動 PhrozenOrca | 正常啟動，無 crash | ✅ Pass | |
| A2 | 切換到 SL1 printer | 無 crash，UI 切換到 SLA 模式 | ✅ Pass | |
| A3 | 切換回 FDM printer | 無 crash，UI 恢復 FDM 模式 | ✅ Pass | |
| A4 | 再次切換到 SL1 | 無 crash，可反覆切換 | ✅ Pass | |

### B. Bed 模型與紋理

| # | 測試步驟 | 預期結果 | 實測結果 | 備註 |
|---|----------|----------|----------|------|
| B1 | SL1 選中時觀察 3D 視圖 | 顯示 SL1 bed 模型 (sl1_bed.stl) | ✅ Pass | |
| B2 | 觀察 bed 紋理 | 紋理正確覆蓋 (sl1.svg) | ✅ Pass | |
| B3 | 旋轉/縮放 3D 視圖 | bed 顯示正常，無閃爍 | ✅ Pass | |

### C. 模型匯入

| # | 測試步驟 | 預期結果 | 實測結果 | 備註 |
|---|----------|----------|----------|------|
| C1 | 匯入 STL 模型 | 模型正常載入到 SL1 bed 上 | ✅ Pass | 需 Fix 1~3 |
| C2 | 匯入 3MF 模型 | 模型正常載入 | ✅ Pass | 需 Fix 2, 4 |
| C3 | 模型在 bed 範圍內 | 模型位置正確，不超出 bed | ✅ Pass | |

### D. 通用 Gizmos (SLA 模式下)

| # | 測試步驟 | 預期結果 | 實測結果 | 備註 |
|---|----------|----------|----------|------|
| D1 | Move gizmo | 可啟用，拖拽移動模型 | ✅ Pass | |
| D2 | Rotate gizmo | 可啟用，旋轉模型 | ✅ Pass | |
| D3 | Scale gizmo | 可啟用，縮放模型 | ✅ Pass | |
| D4 | Flatten gizmo | 可啟用，平面貼合 | ✅ Pass | 需 Fix 5 |
| D5 | Cut gizmo | 可啟用，切割模型 | ✅ Pass | |
| D6 | Measure gizmo | 可啟用，量測距離 | ✅ Pass | |

### E. SLA Gizmos

| # | 測試步驟 | 預期結果 | 實測結果 | 備註 |
|---|----------|----------|----------|------|
| E1 | SlaSupports 工具列圖示可見 | 圖示出現在左側工具列 | ✅ Pass | |
| E2 | 點擊 SlaSupports 圖示 | Gizmo 啟用，無 crash | ✅ Pass | |
| E3 | 點擊模型表面放置支撐點 | 支撐點出現在點擊位置 | ✅ Pass | |
| E4 | 按 A 鍵自動生成支撐 | 自動生成支撐點（或顯示提示） | ⛔ Crash | sla_print() nullptr — 移至 Step 2.3 修復 |
| E5 | 拖拽已有支撐點 | 可移動位置 | ☐ | 受 E4 阻塞 |
| E6 | 選取支撐點後按 Delete | 刪除選中的支撐點 | ☐ | 受 E4 阻塞 |
| E7 | Hollow 工具列圖示可見 | 圖示出現在左側工具列 | ☐ | 待測 |
| E8 | 點擊 Hollow 圖示 | Gizmo 啟用，無 crash | ☐ | 待測 |
| E9 | 調整壁厚參數 | 滑桿/輸入框可操作 | ☐ | 待測 |
| E10 | Hollow 預覽 | 鏤空效果可視 | ☐ | 待測 |

### F. FDM Gizmo 隔離 (SLA 模式下)

| # | 測試步驟 | 預期結果 | 實測結果 | 備註 |
|---|----------|----------|----------|------|
| F1 | FdmSupports gizmo | SLA 模式下不可啟用 (灰色) | ☐ | |
| F2 | Seam gizmo | SLA 模式下不可啟用 | ☐ | |
| F3 | FuzzySkin gizmo | SLA 模式下不可啟用 | ☐ | |
| F4 | MmuSegmentation gizmo | SLA 模式下不可啟用 | ☐ | |
| F5 | BrimEars gizmo | SLA 模式下不可啟用 | ☐ | |

### G. FDM 回歸 (切回 FDM 後) — 同時覆蓋 Step 2.4

| # | 測試步驟 | 預期結果 | 實測結果 | 備註 |
|---|----------|----------|----------|------|
| G1 | 切換回 FDM printer | 無 crash | ☐ | |
| G2 | FDM Gizmos 全部可用 | FdmSupports/Seam/FuzzySkin/MmuSeg/BrimEars | ☐ | |
| G3 | SlaSupports gizmo | FDM 模式下不可啟用 (灰色) | ☐ | |
| G4 | Hollow gizmo | FDM 模式下不可啟用 (灰色) | ☐ | |
| G5 | FDM 切片 | 正常切片無影響 | ☐ | |
| G6 | BrimEars gizmo | 可正常啟用和使用 | ☐ | |

---

## 核心發現：sla_print() nullptr 根本原因

**所有 SLA crash 的共通根因**：`BackgroundSlicingProcess::m_sla_print` 永遠是 nullptr。

**呼叫鏈**：
```
任意 Gizmo / SLA 功能
  → GLCanvas3D::sla_print()
    → m_process->sla_print()
      → BackgroundSlicingProcess::m_sla_print  ← 永遠 nullptr
```

**原因**：PhrozenOrca 用 PartPlate 系統取代了 PrusaSlicer 的單一 print 架構。
- PrusaSlicer：`Plater.cpp` 直接呼叫 `background_process.set_sla_print(&sla_print)` → 有值
- PhrozenOrca：該行被註解掉 (`Plater.cpp:3055-3058`)，改用 `partplate_list.update_slice_context_to_current_plate()`
- 但 `PartPlate.cpp` **完全沒有 SLA 概念** — 只設定 FFF print，從未呼叫 `set_sla_print()`

**影響範圍**：Fix 1, 3, 5 以及 E4 crash 都源自此問題。這是 Step 2.3 的核心修復目標。

---

## 已知限制 (不影響 Step 2.2 通過)

1. **SLA 切片尚不可用** — PartPlate 未建立 SLAPrint 物件，`m_sla_print` 始終為 nullptr (Step 2.3 修復範圍)
2. **SLA 層預覽未實作** — GUI_Preview 遇到非 FFF 直接 return (Step 2.3 修復範圍)
3. **show_supports 未啟用** — InstancesHider 缺少方法 (Phase 4 範圍)
4. **SLA combo_sla_print / combo_sla_material** — 可能未完全初始化 (Phase 1 進行中)
5. **Split to Volumes 在 SLA 下被禁用** — `Plater::priv::can_split_to_volumes()` 內有 `printer_technology != ptSLA` 條件，這是 PrusaSlicer 上游的設計行為（SLA 切片只看整個物件 mesh，不區分 parts/modifiers），非 bug

---

## 修改檔案總覽

| 檔案 | 修改類型 | 對應 Fix |
|------|----------|----------|
| `src/libslic3r/PrintApply.cpp` | SLA nullptr 守衛 | Fix 1 |
| `src/libslic3r/Format/bbs_3mf.cpp` (2 處) | SLA nullptr 守衛 | Fix 2 |
| `src/slic3r/GUI/BackgroundSlicingProcess.cpp` | SLA/FFF validate 路由 | Fix 3 |
| `src/libslic3r/PresetBundle.cpp` | SLA 跳過 FDM preset 驗證 | Fix 4 |
| `src/slic3r/GUI/Gizmos/GLGizmosCommon.cpp` | sla_print() nullptr 守衛 | Fix 5 |

---

## 測試結論

| 類別 | 通過 | 失敗 | 未測 | 結論 |
|------|------|------|------|------|
| A. 啟動與切換 | 4/4 | 0/4 | 0/4 | ✅ 全部通過 |
| B. Bed 模型 | 3/3 | 0/3 | 0/3 | ✅ 全部通過 |
| C. 模型匯入 | 3/3 | 0/3 | 0/3 | ✅ 全部通過 (需 Fix 1~4) |
| D. 通用 Gizmos | 6/6 | 0/6 | 0/6 | ✅ 全部通過 (D4 需 Fix 5) |
| E. SLA Gizmos | 3/10 | 1/10 | 6/10 | E4 crash (sla_print nullptr) → 移至 Step 2.3 |
| F. FDM 隔離 | 0/5 | 0/5 | 5/5 | 待測 |
| G. FDM 回歸 | 0/6 | 0/6 | 6/6 | 待測 |
| **總計** | **19/37** | **1/37** | **17/37** | 進行中 — E/F/G 部分項目阻塞於 sla_print nullptr |

**整體判定**: ☐ 有條件通過 — A~D 全部通過，E4 crash 需 Step 2.3 從根本修復 PartPlate SLAPrint 初始化

---

## 備註

- Section G 同時覆蓋 Step 2.4 (FDM 回歸驗證) 的測試範圍
- 測試時如遇 crash，請記錄 callstack 或錯誤訊息
- SLA 切片功能 (Step 2.3) 需另行修復 PartPlate.cpp 後才可測試
- Fix 1~4 皆為 SLA 模式下 FDM-only 程式碼缺少守衛的問題，不影響 FDM 功能
