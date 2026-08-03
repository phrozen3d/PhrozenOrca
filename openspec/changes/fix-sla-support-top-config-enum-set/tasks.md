## 0. 前置條件與實施順序

- 本 change **無前置，且優先級最高**——它是可穩定重現的 crash（選中支撐點後編輯 Top 欄位並失焦即終止應用程式）
- 它是 `fix-sla-support-preview-geometry-source-semantics` 的**硬前置**：crash 未修好前，無法完成該 change 第 1 節所需的 per-point 編輯觀察
- 也建議在 `fix-sla-support-preview-stored-geometry-in-auto-mode` 之前完成——後者的驗收會反覆選取與編輯支撐點

> 全域相依圖與實施順序見 [`openspec/changes/README.md`](../README.md)。

## 1. 根因確認（已於提案階段完成）

- [x] 1.1 確認 `support_top_apply_point()`（`GLGizmoSlaSupports.cpp:302`）第一行 `cfg.set("support_contact_type", use_sphere ? spSphere : spNone2)` 因 `ContactType` 整數提升選到 `ConfigBase::set(key, int, bool)`（`Config.cpp:515`），而 `support_contact_type` 為 `coEnum`、不在該 switch 內 → `default: throw BadOptionTypeException`
- [x] 1.2 確認該行為函式第一個敘述，失敗模式是「整份 cfg 從未產出」而非「單一 key 沒設對」
- [x] 1.3 確認三個呼叫點：`notify_process_tab_selection_changed()`（`:1980`，有 try/catch 吞掉）、`OptionsGroup.cpp:695`、`Tab.cpp:7338`（後兩者無 try/catch）
- [x] 1.4 確認 `OptionsGroup.cpp:693` 的 `apply_process_top_option()` 在拋出前已執行完畢，故編輯值有寫進點裡，但 `:696-699` 的 `canvas->set_as_dirty()` 被略過
- [x] 1.5 確認同函式其餘六個 key 皆為 `coFloat`，`cfg.set(key, double)` 用法正確
- [x] 1.6 確認繼承鏈 `BadOptionTypeException` → `ConfigurationError`（`Config.hpp:114`）→ `Slic3r::RuntimeError` → `CriticalException` → `Slic3r::Exception` → `std::runtime_error`，而 `Exception.hpp:14` 對 `CriticalException` 的註解明訂「shall never propagate up to the UI thread」
- [x] 1.7 確認 crash 路徑：例外逸出 → `GUI_App::OnExceptionInMainLoop()`（`:6110`）→ `generic_exception_handle()`（`:718`）→ `catch (const std::exception&)`（`:767`）→ `throw;`（`:771`）重新拋出 → `OnExceptionInMainLoop()` 未正常返回 → 逸出主迴圈 → 應用程式終止
- [x] 1.8 實測基準：選點 A 拋 1 次（不 crash）、選點 B 拋 1 次（不 crash）、取消選取拋 0 次、**選點 + 改 Upper Diameter + 失焦拋 5 次 `BadOptionTypeException` 與 6 次 `[rethrow]` 後 crash**、不選點直接改欄位拋 0 次（正常走 preset 模板路徑）
- [x] 1.9 實測確認 Release 版不掛偵錯器時**無**可感知的選取延遲——先前觀察到的延遲為偵錯器處理 first-chance 例外的成本，非產品問題；crash 則在 Release 版同樣存在

## 2. 修正

- [x] 2.1 將 `support_top_apply_point()` 中 `support_contact_type` 的寫入改為 `cfg.set_key_value("support_contact_type", new ConfigOptionEnum<ContactType>(use_sphere ? spSphere : spNone2))`
- [x] 2.2 確認 `ConfigOptionEnum<ContactType>` 與 `ContactType` 在該 TU 可見——同檔案 `:120`/`:124` 已使用，無需補 include
- [x] 2.3 掃描 `GLGizmoSlaSupports.cpp` 內全部 `cfg.set(...)` / `config.set(...)` 呼叫，逐一比對目標 option 的 `type()`：
      `sync_generate_support_for_object()` 的 `mo->config.set("generate_support", enable)` — `generate_support` 為 `coBool`（`PrintConfig.cpp:7560`），比對 `set(key, bool)` 多載正確；
      密度 slider 的 `mo->config.set(density_key, (int)density)` — `support_points_density_relative` 為 `coInt`（`PrintConfig.cpp:7690`），正確；
      `apply_weight_preset()` 九個 key（`support_pillar_diameter` / `support_head_front_diameter` / `support_contact_diameter` / `support_base_diameter` / `support_base_height` / `support_head_width` / `support_head_back_diameter` / `support_head_penetration` / `support_segment_length`）逐一查證皆為 `coFloat`，與 `cfg.set(key, double)` 用法一致
- [x] 2.4 掃描結果：**沒有發現第二處 enum→int 誤用**，本任務無需修改

## 2b. 例外邊界（結構性保險，見 design.md D2b）

- [x] 2b.1 為 `OptionsGroup.cpp:695` 的 `support_top_config_from_selection()` 呼叫補上 `try/catch (const std::exception&)`，失敗時 `BOOST_LOG_TRIVIAL(error)` 記錄而非致命
- [x] 2b.2 為 `Tab.cpp:7338` 補上同樣的邊界（`TabSLAPrint::on_value_change()` 是與 `OptionsGroup::on_change_OG()` 平行的第二個呼叫點，兩者皆需防護）
- [x] 2b.3 確認邊界的例外處理不會吞掉 `apply_process_top_option()` 已成功寫入的編輯，且 `canvas->set_as_dirty()` 在投影失敗時仍會執行——兩處皆將 `apply_process_top_option()` 留在 `try` 區塊之外（其呼叫早於 try，且本身不拋出），`set_as_dirty()` 置於 `try/catch` 之後、無條件執行
- [x] 2b.4 確認**未**修改 `generic_exception_handle()`（`GUI_App.cpp:718`）——本任務未觸碰 `GUI_App.cpp`

## 2c. 修正：取消選取後 Top 欄位未還原為 preset 顯示（驗收 4.6 發現，屬本 change 範圍內，非 follow-up）

驗收 4.6 時發現的既有缺陷，根因與本 change 啟用的「此前從未執行過的下游路徑」直接相關，spec 已有對應 requirement（「取消選取還原 preset 顯示」），故在本 change 內修正，不算 6.4 所指的 out-of-scope 發現。

- [x] 2c.1 根因：`m_support_point_top_field_update` 被同時挪用於兩種不同生命週期——(a) `on_value_change()` / `is_support_point_top_field_update()` 用的「暫時性重入保護」，`begin_support_point_top_field_display()` 設 true 後立刻用 `CallAfter` 排程重設回 false；(b) `end_support_point_top_field_display()` 用來判斷「目前是否正在顯示某點的 per-point 值」的**狀態旗標**。(a) 的 `CallAfter` 幾乎在下一個 idle 週期就把旗標重設為 false，遠早於使用者實際取消選取的時間點，導致 `end_support_point_top_field_display()` 的 `if (!m_support_point_top_field_update) return;` 幾乎每次都提前 return，`apply_support_point_top_fields(nullptr)` 從未被呼叫
- [x] 2c.2 修正：於 `Tab.hpp` 新增獨立的 `m_support_point_top_field_active`，只負責 (b) 的狀態語意，不受 `CallAfter` 影響；`m_support_point_top_field_update` 保留原本 (a) 的重入保護語意不變
- [x] 2c.3 `begin_support_point_top_field_display()` 額外設 `m_support_point_top_field_active = true`
- [x] 2c.4 `end_support_point_top_field_display()` 改為檢查 `m_support_point_top_field_active`；為求對稱與安全，比照 `begin()` 的模式，在呼叫 `apply_support_point_top_fields(nullptr)` 前後也套上 `m_support_point_top_field_update` 的暫時性重入保護（原本這段呼叫完全沒有防護）
- [x] 2c.5 建置驗證：`libslic3r_gui` 增量建置，四個修改檔案（`GLGizmoSlaSupports.cpp` / `OptionsGroup.cpp` / `Tab.cpp` / `Tab.hpp`）0 warnings 0 errors

## 3. 驗證：crash 消除（最高優先）

- [x] 3.0a **主要重現案例**：選中一顆支撐點 → 修改 Upper Diameter → 失焦。確認應用程式不終止、編輯正確套用、3D 視圖立即重畫 — **Pass**
- [x] 3.0b 反覆選取不同的點並修改各項 Top 參數數十次，確認全程不終止 — **Pass**
- [x] 3.0c 確認整個過程 `BadOptionTypeException` 拋出次數為 0、`[rethrow]` 為 0 — **Pass**
- [x] 3.0d 於 Release 版（不掛偵錯器）重跑 3.0a 與 3.0b — **Pass**

## 3. 驗證投影邏輯

> 這四項沒有獨立的觸發按鈕——`support_top_config_from_selection()` 是內部函式，只能透過觀察其**結果如何反映在 UI 上**間接驗證。3.1/3.2 實質上與 4.1/4.2 是同一組操作，差別只在於這裡專門盯著 **Contact Type 下拉選單**（因為它正是原本會導致 crash 的那個欄位，值得單獨確認一次）。

- [x] 3.1 選中一顆 Contact Type 為 Sphere 的點，確認 Top 群組的 **Contact Type** 下拉選單顯示 **Sphere**，其餘六個欄位也反映該點的值 — **Pass**
- [x] 3.2 選中一顆 Contact Type 為 None 的點，確認下拉選單顯示 **None** — **Pass**
- [x] 3.3 取消選取（套用 2c 修正後），確認下拉選單與其餘欄位都變回目前 preset 的設定值 — **Pass**
- [x] 3.4 確認整個過程 `BadOptionTypeException` 拋出次數為 0 — 與 3.0c 為同一項量測，已由你確認的「Pass」涵蓋

## 4. 驗證下游路徑（本 change 的主要風險，該路徑此前從未實際執行）

- [x] 4.1 建立兩顆 `support_head_front_diameter` 不同的手動支撐點，交替點選，確認 Top 群組的 `Upper Diameter` 欄位隨選取切換為各自的值 — **Pass**
- [x] 4.2 確認七個 Top 欄位（含 `support_contact_type` 下拉選單）皆正確回填 — **Pass**
- [x] 4.3 **回填不得回寫**：選點 A → 選點 B → 選點 A（不編輯任何欄位）→ 按 Ctrl+Z，確認完全沒有反應（選取本身未寫入任何資料，沒有東西可復原）— **Pass**
- [x] 4.4 確認 `m_support_point_top_field_update` guard 正確阻止回填被誤判為使用者編輯——與 4.3 為同一機制的兩種驗證角度，由 4.3 的 Pass 一併涵蓋
- [x] 4.5 ~~SpinCtrl 型欄位~~ — **查證後這項本身文字有誤，已改寫**：7 個 Top 欄位裡 6 個是 `coFloat`（對應 `TextCtrl`），1 個是 `coEnum`（對應 `Choice` 下拉選單），`OptionsGroup.cpp:85-91` 的 widget 對照表裡沒有一個對應到 `SpinCtrl` 類別（`coInt` 才會，這組欄位沒有 `coInt`）。既有的重入保護機制（`m_support_point_top_field_update` + `CallAfter`）仍然正確，只是它防的不是字面上的「SpinCtrl」，而是任何 widget 的 `set_value()` 之後可能延遲送達的變更事件——這點已被 4.1/4.2 的 Pass 間接驗證（回填沒有讓資料跑掉），視為涵蓋
- [x] 4.6 取消選取後，確認 `end_support_point_top_field_display()` 正確還原為 preset 顯示 — **原為 Fail，2c 修正後於新建置重測，Pass**

## 5. 驗證編輯回寫

- [x] 5.1 選中點後修改 `support_head_front_diameter`，確認該點 `head_front_radius` 更新為新值一半 — **Pass**
- [x] 5.2 確認修改後 3D 視圖立即重畫、preview 錐體尺寸跟著變 — **Pass**（與 5.1 一併確認）
- [x] 5.3 確認該編輯**未**寫入 SLA print preset——preset 下拉選單未出現修改標記，取消選取後欄位正確跳回原本的 preset 值 — **Pass**
- [x] 5.4 修改 `support_contact_type` 由 `None` 改為 `Sphere`，確認該點 `contact_sphere_radius` 依既有規則更新、欄位回填後仍顯示 `Sphere`、視圖重畫 — **Pass**
- [x] 5.5 無選取點時修改 Top 欄位，確認仍走「下一顆新點的模板」路徑——preset 下拉選單出現修改標記，新放置的手動點套用剛改的值 — **Pass**

## 6. 迴歸

- [x] 6.1 手動放置新支撐點時仍套用當下 Top 欄位值（`freeze_process_top_into_point()` 路徑不受影響）— **Pass**
- [x] 6.2 切片輸出不變 — **Pass**
- [x] 6.3 記錄「Release 不掛偵錯器時，點選支撐點的延遲是否消失」— **已實測：Release 版無可感知延遲**，該症狀為偵錯器成本，不屬產品問題，本 change 不需處理
- [x] 6.4 若修好後暴露出原本被例外掩蓋的其他缺陷，個別記錄為 follow-up，不併入本 change — 驗收 4.6 發現的「取消選取未還原」屬本 change spec 明列的 requirement，非 out-of-scope 發現，已於 2c 節直接修正，不算此項所指的 follow-up；本項無其他發現

## 7. Follow-up（out of scope）

- 選取狀態與 live 參數對 preview 錐體外型的正確語意 → `fix-sla-support-preview-geometry-source-semantics`。**本 change 是其前置條件**：crash 未修好前無法完整測試 per-point 編輯行為
- 非編輯模式下手動點不套用 per-point 幾何 → `fix-sla-support-preview-stored-geometry-in-auto-mode`
- `sla_trafo` 改變後前端支撐點快取不失效 → `fix-sla-support-points-invalidate-on-trafo-change`
- 支撐點 undo/redo 資料不正確 → `fix-sla-support-points-undo-snapshot`
- 是否為 `ConfigBase::set(key, int)` 增加 `coEnum` 支援或加上編譯期防護（例如刪除 enum 多載以強制呼叫端明示）——影響整個 libslic3r，需獨立評估
