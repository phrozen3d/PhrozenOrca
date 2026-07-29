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
  ├─ OptionsGroup.cpp:695                    ← 例外往 wx 傳，set_as_dirty() 跳過
  └─ Tab.cpp:7338                            ← 同上
```

值得注意的是：`OptionsGroup.cpp:693` 的 `apply_process_top_option()` 在拋出**之前**就已執行完畢，所以使用者的編輯確實有寫進點裡——只是畫面不會重畫、欄位不會回填。這解釋了為何缺陷長期存在卻沒被當成「編輯功能壞掉」。

## Goals / Non-Goals

**Goals:**

- `support_top_apply_point()` 能完整執行七個 key 的寫入，不再中途拋出。
- `support_top_config_from_selection()` 回傳一份七個 key 皆反映選中點的 config。
- 選中支撐點時 Top 欄位顯示該點的值；編輯欄位後 3D 視圖被標記重畫。
- 消除 `BadOptionTypeException` 的持續拋出。

**Non-Goals:**

- 不擴充 `ConfigBase::set(key, int)` 使其支援 `coEnum`。
- 不改變 per-point 參數的語意或欄位集合。
- 不改變 `preview_use_stored_top()` / `preview_sla_head_for_point()` 的參數解析規則。
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

- 「手動模式下點選支撐點會延遲才變紅」是否由本缺陷的 first-chance 例外造成？需以 **Release 不掛偵錯器**的執行檔實測比對。若延遲在無偵錯器時消失，則本 change 順帶解決；若仍存在，另有成因需獨立追查。
