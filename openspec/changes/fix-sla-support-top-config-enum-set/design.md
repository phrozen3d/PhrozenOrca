## Context

`support_top_apply_point(const sla::SupportPoint &sp, DynamicPrintConfig &cfg)` 的職責是「把一顆支撐點的 per-point 參數投影成一份 `DynamicPrintConfig`」，供 Process tab 的 Top 欄位顯示。它以 `sla_process_config()` 的完整副本為起點，再逐一覆寫七個 key。

七個 key 中六個是 `coFloat`，用 `cfg.set(key, double)` 完全正確。只有 `support_contact_type` 是 `ConfigOptionEnum<ContactType>`：

```cpp
cfg.set("support_contact_type", use_sphere ? spSphere : spNone2);   // ← 第一行就炸
```

`ConfigBase` 沒有 enum 多載，`ContactType` 走整數提升選到 `set(key, int, bool)`，其 switch 的 `default` 分支拋 `BadOptionTypeException`。

這行是**第一個敘述**，所以失敗模式不是「contact type 沒設對」而是「整份 cfg 從未產出」。下游因此全部空轉：

```
support_top_config_from_selection()          ← 拋出，永遠不回傳
  ├─ notify_process_tab_selection_changed()  ← try/catch 吞掉，欄位不更新
  ├─ OptionsGroup.cpp:695                    ← 無防護，例外往 wx 傳 → crash
  └─ Tab.cpp:7338                            ← 同上
```

值得注意的是：`OptionsGroup.cpp:693` 的 `apply_process_top_option()` 在拋出**之前**就已執行完畢，所以使用者的編輯確實有寫進點裡——只是畫面不會重畫、欄位不會回填。

而後兩條路徑沒有 try/catch，`BadOptionTypeException` 因此傳到 UI thread。它的繼承鏈是 `ConfigurationError` → `Slic3r::RuntimeError` → `CriticalException` → `Slic3r::Exception` → `std::runtime_error`，而 `Exception.hpp:14` 對 `CriticalException` 的註解正是：

> `// Critical exception produced by Slicer, such exception shall never propagate up to the UI thread.`

逃出去之後 `generic_exception_handle()`（`GUI_App.cpp:718`）的 `catch (const std::exception&)` 分支記錄完就 `throw;`（`:771`）重新拋出，`OnExceptionInMainLoop()` 因此未正常返回，例外逸出主迴圈 → 應用程式終止。

實測「選點 + 改 Upper Diameter + 失焦」會產生 5 次 `BadOptionTypeException` 與 6 次 `[rethrow]` 後 crash——`[rethrow]` 條目就是 `GUI_App.cpp:771` 的那一行。

## Goals / Non-Goals

**Goals:**

- **消除 crash**：選中支撐點後編輯 Top 欄位不得使應用程式終止。
- `support_top_apply_point()` 能完整執行七個 key 的寫入，不再中途拋出。
- `support_top_config_from_selection()` 回傳一份七個 key 皆反映選中點的 config。
- 選中支撐點時 Top 欄位顯示該點的值；編輯欄位後 3D 視圖被標記重畫。
- 讓同類錯誤在未來降級為「功能失效」而非「應用程式終止」。

**Non-Goals:**

- 不擴充 `ConfigBase::set(key, int)` 使其支援 `coEnum`。
- 不改變 `generic_exception_handle()` 對 `std::exception` 重新拋出的既有策略。
- 不改變 per-point 參數的語意或欄位集合。
- 不改變 `preview_use_stored_top()` / `preview_sla_head_for_point()` 的參數解析規則，亦不定義選取狀態應如何影響 preview 外型（→ `fix-sla-support-preview-geometry-source-semantics`）。
- 不重構 per-point Top 顯示的 UI 架構（`m_support_point_top_field_update` guard、`CallAfter` 時序等維持現狀）。

## Decisions

### D1. 用 `set_key_value()` 明確建構 `ConfigOptionEnum<ContactType>`

```cpp
cfg.set_key_value("support_contact_type",
                  new ConfigOptionEnum<ContactType>(use_sphere ? spSphere : spNone2));
```

- **為何不用 `cfg.option<ConfigOptionEnum<ContactType>>(key, true)->value = ...`**：兩者皆可行，但 `set_key_value()` 是本 codebase 寫入具型別 option 的既有慣用法（`ConfigManipulation.cpp` 有大量先例），一致性較佳。
- **為何不改 `ConfigBase::set(key, int)` 支援 `coEnum`**：那個多載是 libslic3r 的公用 API，被整個專案使用。加上 `case coEnum` 需要處理「int 值是否為該 enum 的合法值」，語意含糊且會讓所有誤用悄悄變成合法呼叫——遮蔽問題而非修正問題。呼叫端選錯多載才是本缺陷的根因。

### D2. 全面掃描而非只修這一處

同一類錯誤（enum option 走 `set(key, int)`）在編譯期不會有任何警告，因為整數提升是合法轉換。必須主動掃描 SLA support gizmo 內所有 `cfg.set(...)` 呼叫，逐一確認目標 option 的 `type()`。

`support_top_apply_point()` 內其餘六個 key 已確認為 `coFloat`，但 `apply_weight_preset()`（`:1929`）等處也有大量 `cfg.set()`，需一併檢查。

### D2b. 補上例外邊界，作為結構性保險

修好根因後 `support_top_config_from_selection()` 不再拋出，這層防護不會被觸發。但 `OptionsGroup.cpp:695` 與 `Tab.cpp:7338` 目前**完全沒有防護**，任何從投影邏輯逸出的例外都會直達 UI thread 並終止應用程式——這是一個結構性弱點，不是單一 bug。

因此除了修根因，也要為這兩處補上例外邊界，使同類錯誤降級為「該次欄位更新失敗並記錄」。

- **為何不改 `generic_exception_handle()` 不要重新拋出**：那個 `throw;`（`GUI_App.cpp:771`）是刻意的——讓非預期例外浮現而非默默吞掉，符合 `CriticalException` 註解所述「不該傳到 UI thread」的設計。放寬它會把所有未預期例外變成靜默失敗，代價遠大於效益。正確的作法是不讓例外傳到那裡。
- **為何不只靠修根因**：根因修好後這條路徑仍然是「任何一個 `cfg.set()` 用錯型別就 crash」的地雷區，而該類錯誤在編譯期無警告（enum→int 是合法轉換）。邊界的價值在於把失敗模式從致命降為可見。

### D3. 驗收重點在下游路徑，不在那一行

修好之後，`begin_support_point_top_field_display()` → `apply_support_point_top_fields()`（`Tab.cpp:7256`）這條路徑會**第一次真正執行**。它從未在實際運行中被驗證過，因此本 change 的驗收必須涵蓋：

- 七個欄位是否都正確回填（含 `support_contact_type` 這個 enum 下拉選單）。
- `m_support_point_top_field_update` guard 是否正確阻止回填動作被誤判成使用者編輯（否則會形成「顯示 → 觸發 on_change → 再寫回點」的迴圈）。
- 取消選取時 `end_support_point_top_field_display()` 是否正確還原成 preset 顯示。

**這是本 change 真正的風險所在**——那一行的修改本身近乎零風險，但它會啟用一段未經驗證的程式碼。

## Risks / Trade-offs

- **[啟用未驗證路徑導致回填迴圈]** → `apply_support_point_top_fields()` 呼叫 `og->set_value(key, val, false)`，第三個參數為 false 表示不觸發 change 事件；另有 `m_support_point_top_field_update` 旗標在 `OptionsGroup.cpp:688` 被檢查。兩層防護理論上足夠，但從未實測。緩解：驗收明列「選取點 → 欄位回填 → 不得反過來修改該點的值」。

- **[SpinCtrl 的非同步 change 事件]** → `begin_support_point_top_field_display()` 已用 `CallAfter` 延後解除 guard（`Tab.cpp:7298`），註解寫明是為此。驗收需涵蓋 SpinCtrl 型欄位。

- **[enum 下拉選單的回填與 `boost::any` 型別]** → `og->get_config_value()` 對 `coEnum` 回傳 `int`，而 `process_contact_type_is_sphere()`（`:110`）已假設 `val.type() == typeid(int)`。兩者一致，風險低。

- **[修好後才暴露出的其他問題]** → 這條路徑一旦開始執行，可能顯露原本被例外掩蓋的其他缺陷。屬預期內；若發現則個別記錄，不擴大本 change 範圍。

## Migration Plan

無資料遷移、無檔案格式或 profile 變更。純 GUI 行為修正，可直接隨版本發布。

回退策略：單行修改，revert 即恢復現況（功能繼續不生效，但不會有新的失敗模式）。

## Open Questions

無。

原本待決的「手動模式下點選支撐點會延遲才變紅是否由 first-chance 例外造成」已實測確認：**Release 版不掛偵錯器時沒有可感知的延遲**，該延遲確定是偵錯器處理 first-chance 例外的成本，非產品問題。crash 則在 Release 版同樣存在，是本 change 真正要解決的目標。
