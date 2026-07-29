## Why

`support_top_apply_point()`（`GLGizmoSlaSupports.cpp:302`）的**第一行**就會拋例外：

```cpp
cfg.set("support_contact_type", use_sphere ? spSphere : spNone2);
```

`ContactType` 是 unscoped enum，隱式轉成 `int` 後選到 `ConfigBase::set(const std::string&, int, bool)`（`Config.cpp:515`）。該函式的 switch 只處理 `coInt` / `coFloat` / `coFloatOrPercent` / `coString`，而 `support_contact_type` 是 `ConfigOptionEnum<ContactType>`（`coEnum`）→ 直接掉進 `default: throw BadOptionTypeException`。

因為它是函式的第一個敘述，整個 `support_top_apply_point()` 一進去就中止，後面六個欄位（contact diameter、penetration、front / back diameter、pillar diameter、segment length）全部沒有寫入。而 `support_top_config_from_selection()` 唯一的工作就是呼叫它，因此**該函式永遠拿不到結果**。

三個呼叫點的下場：

| 呼叫點 | 後果 |
|---|---|
| `notify_process_tab_selection_changed()`（`:1980`） | 有 try/catch → 記 log 後放棄，`begin_support_point_top_field_display()` 從不執行 |
| `OptionsGroup.cpp:695` | 無 try/catch，例外往 wx event handler 傳 |
| `Tab.cpp:7338` | 同上 |

**使用者可見的症狀**：

1. 選中一顆既有支撐點時，Process → Support → Top 欄位本應切換成顯示**該點**的參數，實際上永遠停留在 preset 值——per-point Top 欄位顯示這個功能等同從未生效。
2. 編輯選中點的參數時，`OptionsGroup.cpp:693` 的 `apply_process_top_option()` 先執行成功（值有寫進點裡），接著第 695 行拋例外，使後續的 `canvas->set_as_dirty()`（`:696-699`）永遠執行不到——**改完參數後 3D 視圖不會被標記為需要重畫**。
3. Visual Studio output 持續出現 `Slic3r::BadOptionTypeException`。掛偵錯器時每次 first-chance 例外都有可觀成本，是「點選支撐點後延遲才變紅」的候選成因之一（待實測確認）。

本缺陷於 `perf-sla-support-points-preview-render` 的驗收期間發現，經比對確認與該 change 無關：其 diff 未觸碰 `support_top_apply_point()`，亦未新增任何 `cfg.set()` 呼叫。

## What Changes

- 將 `support_top_apply_point()` 中對 `support_contact_type` 的寫入改為 enum 專用路徑（`set_key_value()` 搭配 `new ConfigOptionEnum<ContactType>(...)`，即本 codebase 既有寫法），使該函式能完整執行到底。
- 掃描 SLA support gizmo 內其餘 `ConfigBase::set()` 呼叫，確認沒有第二處對非 `coInt` / `coFloat` / `coFloatOrPercent` / `coString` 型別的誤用。
- **本 change 的主要工作量不在那一行**，而在驗證它所啟用的、目前完全沒有執行過的 per-point Top 欄位顯示路徑（`begin_support_point_top_field_display()` → `apply_support_point_top_fields()`）確實正確。

### Non-goals

- 不修改 `ConfigBase::set(key, int)` 讓它支援 `coEnum`。那是 libslic3r 的公用 API，影響範圍遠大於本 change；呼叫端用錯多載才是問題所在。
- 不改變 per-point Top 參數的語意、欄位集合，或 `preview_use_stored_top()` 的規則。
- 不處理「非編輯模式下手動點不套用 per-point 幾何」（→ `fix-sla-support-preview-stored-geometry-in-auto-mode`）。
- 不處理支撐點 undo/redo 資料問題（→ `fix-sla-support-points-undo-snapshot`）。
- 不最佳化 `begin_support_point_top_field_display()` 的效能。

## Capabilities

### New Capabilities

- `sla-support-point-top-field-display`：選取支撐點時，Process → Support → Top 欄位顯示該點的 per-point 參數而非 preset 值；以及編輯這些欄位時對選中點與 3D 視圖的回寫行為。涵蓋 `support_top_apply_point()` 必須完整執行、不得因型別誤用中斷的不變式。

### Modified Capabilities

<!-- 無。`sla-support-param-wiring` 規範的是 Process 設定 → SLAPrintObjectConfig 的 wiring，本 change 不改變其任何 requirement。 -->

## Impact

- **Primary**：`src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp` — `support_top_apply_point()` 的 enum 寫入
- **Secondary（僅驗證，預期零修改）**：`src/slic3r/GUI/Tab.cpp` 的 `begin_support_point_top_field_display()` / `apply_support_point_top_fields()`、`src/slic3r/GUI/OptionsGroup.cpp:686-705` — 這兩段是被例外擋住而從未執行的下游路徑
- 不影響 libslic3r、切片結果、檔案格式或 profile
- 無 public API 變更
