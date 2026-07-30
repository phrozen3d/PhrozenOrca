## Why

**選中一顆支撐點後修改 Process → Support → Top 的任一欄位並失焦，應用程式會終止。** 這是可穩定重現的 crash，不只是被記錄下來的例外。

`support_top_apply_point()`（`GLGizmoSlaSupports.cpp:302`）的**第一行**就會拋例外：

```cpp
cfg.set("support_contact_type", use_sphere ? spSphere : spNone2);
```

`ContactType` 是 unscoped enum，隱式轉成 `int` 後選到 `ConfigBase::set(const std::string&, int, bool)`（`Config.cpp:515`）。該函式的 switch 只處理 `coInt` / `coFloat` / `coFloatOrPercent` / `coString`，而 `support_contact_type` 是 `ConfigOptionEnum<ContactType>`（`coEnum`）→ 直接掉進 `default: throw BadOptionTypeException`。

### 為什麼會 crash

`BadOptionTypeException` 的繼承鏈是 `ConfigurationError` → `Slic3r::RuntimeError` → `CriticalException` → `Slic3r::Exception` → `std::runtime_error`。`Exception.hpp:14` 對 `CriticalException` 的註解寫得很清楚：

> `// Critical exception produced by Slicer, such exception shall never propagate up to the UI thread.`

而三個呼叫點中有兩個沒有 try/catch，例外因此正是往 UI thread 傳：

| 呼叫點 | 有無 try/catch | 後果 |
|---|---|---|
| `notify_process_tab_selection_changed()`（`:1980`） | 有 | 記 log 後放棄，欄位不更新（不 crash） |
| `OptionsGroup.cpp:695` | **無** | 傳入 wx event dispatch |
| `Tab.cpp:7338` | **無** | 同上 |

逃出去之後：

```
wx main loop
  └─ GUI_App::OnExceptionInMainLoop()          GUI_App.cpp:6110
       └─ generic_exception_handle()            GUI_App.cpp:718
            └─ catch (const std::exception&)    GUI_App.cpp:767
                 ├─ wxLogError + BOOST_LOG
                 └─ throw;   ← 重新拋出         GUI_App.cpp:771
  → OnExceptionInMainLoop() 未正常返回
  → 例外逸出主迴圈 → Unhandled unknown exception; terminating the application
```

實測 VS Output 中交錯出現的 `[rethrow]` 條目，就是 `GUI_App.cpp:771` 的 `throw;`。

### 實測結果

| 操作 | 例外次數 | 結果 |
|---|---|---|
| 選點 A | 1 | 不 crash（走有 try/catch 的路徑） |
| 選點 B | 1 | 不 crash |
| 點空白處取消選取 | 0 | 走 `end_support_point_top_field_display()`，不呼叫投影邏輯 |
| **選點 + 改 Upper Diameter + 失焦** | **11（含 5 次 `BadOptionTypeException` 與 6 次 `[rethrow]`）** | **crash** |
| 不選點直接改 Upper Diameter + 失焦 | 0 | 正常（走 preset 模板路徑），所有 auto 點跟著改變 |

Release 版不掛偵錯器時**沒有**可感知的選取延遲——先前觀察到的延遲確定是偵錯器處理 first-chance 例外的成本，非產品問題。但 crash 在 Release 版同樣存在。

### 其他可見後果

即使在不 crash 的選取路徑上，`support_top_apply_point()` 一進去就中止意味著 `support_top_config_from_selection()` **永遠拿不到結果**，因此：

1. per-point Top 欄位顯示功能等同從未生效。實測時欄位看起來「有顯示手動改過的值」，但那是**沒有人更新過的殘留 UI 狀態**（使用者自己剛打進去的值），不是從點讀回來的——選取另一顆參數不同的點時欄位不會跟著變，即為佐證。
2. `OptionsGroup.cpp:693` 的 `apply_process_top_option()` 在拋出**之前**已執行完畢，所以編輯值確實有寫進點裡，但後續的 `canvas->set_as_dirty()`（`:696-699`）永遠執行不到。

本缺陷於 `perf-sla-support-points-preview-render` 的驗收期間發現，經比對確認與該 change 無關：其 diff 未觸碰 `support_top_apply_point()`，亦未新增任何 `cfg.set()` 呼叫。

## What Changes

- 將 `support_top_apply_point()` 中對 `support_contact_type` 的寫入改為 enum 專用路徑（`set_key_value()` 搭配 `new ConfigOptionEnum<ContactType>(...)`，即本 codebase 既有寫法），使該函式能完整執行到底、不再拋出。
- 掃描 SLA support gizmo 內其餘 `ConfigBase::set()` 呼叫，確認沒有第二處對非 `coInt` / `coFloat` / `coFloatOrPercent` / `coString` 型別的誤用——這類錯誤在編譯期完全無警告。
- 為 `OptionsGroup.cpp:695` 與 `Tab.cpp:7338` 這兩條無防護的路徑補上例外邊界，使**未來**任何同類錯誤降級為「功能失效」而非「應用程式終止」。修好根因後這層防護不會被觸發，但它是防止同一個 crash 模式再次出現的結構性保險。
- **主要工作量不在那一行**，而在驗證它所啟用的、目前完全沒有執行過的 per-point Top 欄位顯示路徑（`begin_support_point_top_field_display()` → `apply_support_point_top_fields()`）確實正確。

### Non-goals

- 不修改 `ConfigBase::set(key, int)` 讓它支援 `coEnum`。那是 libslic3r 的公用 API，影響範圍遠大於本 change；呼叫端用錯多載才是問題所在。
- 不改變 `generic_exception_handle()`（`GUI_App.cpp:718`）對 `std::exception` 重新拋出的既有策略。該策略本身是刻意的（讓非預期例外浮現而非默默吞掉），問題在於不該有例外傳到那裡。
- 不定義「選取狀態與 live 參數應如何影響 preview 錐體外型」。實測發現選取一顆手動點時錐體外型會改變、而 live 參數只即時影響 auto 點——該語意需要獨立定義，見 `fix-sla-support-preview-geometry-source-semantics`。本 change 修好 crash 是處理該議題的前置條件。
- 不改變 per-point Top 參數的語意、欄位集合，或 `preview_use_stored_top()` 的規則。
- 不處理支撐點 undo/redo 資料問題（→ `fix-sla-support-points-undo-snapshot`）。

## Capabilities

### New Capabilities

- `sla-support-point-top-field-display`：選取支撐點時，Process → Support → Top 欄位顯示該點的 per-point 參數而非 preset 值；以及編輯這些欄位時對選中點與 3D 視圖的回寫行為。涵蓋「`support_top_apply_point()` 必須完整執行、不得因型別誤用中斷」以及「per-point Top 編輯路徑不得使應用程式終止」的不變式。

### Modified Capabilities

<!-- 無。`sla-support-param-wiring` 規範的是 Process 設定 → SLAPrintObjectConfig 的 wiring，本 change 不改變其任何 requirement。 -->

## Impact

- **Primary**：`src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp` — `support_top_apply_point()` 的 enum 寫入
- **Primary**：`src/slic3r/GUI/OptionsGroup.cpp:686-705`、`src/slic3r/GUI/Tab.cpp:7334-7341` — 補上例外邊界
- **Secondary（僅驗證，預期零修改）**：`src/slic3r/GUI/Tab.cpp` 的 `begin_support_point_top_field_display()` / `apply_support_point_top_fields()` — 被例外擋住而從未執行的下游路徑
- 不影響 libslic3r、切片結果、檔案格式或 profile
- 無 public API 變更
