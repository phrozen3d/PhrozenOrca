## 0. 前置條件與實施順序

- 本 change **無前置，且優先級最高**——它是可穩定重現的 crash（選中支撐點後編輯 Top 欄位並失焦即終止應用程式）
- 它是 `fix-sla-support-preview-geometry-source-semantics` 的**硬前置**：crash 未修好前，無法完成該 change 第 1 節所需的 per-point 編輯觀察
- 也建議在 `fix-sla-support-preview-stored-geometry-in-auto-mode` 之前完成——後者的驗收會反覆選取與編輯支撐點
- 建議全域順序：
  1. **`fix-sla-support-top-config-enum-set`（本 change）** — crash，最高優先
  2. `fix-sla-support-preview-stored-geometry-in-auto-mode`
  3. `fix-sla-support-preview-geometry-source-semantics` — 需 1、2 先完成
  4. `fix-sla-support-points-invalidate-on-trafo-change`
  5. `fix-sla-support-points-undo-snapshot`

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

- [ ] 2.1 將 `support_top_apply_point()` 中 `support_contact_type` 的寫入改為 `cfg.set_key_value("support_contact_type", new ConfigOptionEnum<ContactType>(use_sphere ? spSphere : spNone2))`
- [ ] 2.2 確認 `ConfigOptionEnum<ContactType>` 與 `ContactType` 在該 TU 可見（必要時補 include），且編譯零警告
- [ ] 2.3 掃描 `GLGizmoSlaSupports.cpp` 內全部 `cfg.set(...)` / `config.set(...)` 呼叫（含 `apply_weight_preset()`、`support_top_apply_point()`、`sync_new_point_params_from_config()` 等），逐一比對目標 option 的 `type()`，確認沒有第二處 enum→int 誤用
- [ ] 2.4 若 2.3 發現其他誤用，一併以相同方式修正並在此列出

## 2b. 例外邊界（結構性保險，見 design.md D2b）

- [ ] 2b.1 為 `OptionsGroup.cpp:695` 的 `support_top_config_from_selection()` 呼叫補上例外邊界，使投影失敗降級為記錄而非致命
- [ ] 2b.2 為 `Tab.cpp:7338` 補上同樣的邊界
- [ ] 2b.3 確認邊界的例外處理不會吞掉 `apply_process_top_option()` 已成功寫入的編輯，且 `canvas->set_as_dirty()` 在投影失敗時仍會執行
- [ ] 2b.4 確認**未**修改 `generic_exception_handle()`（`GUI_App.cpp:718`）對 `std::exception` 重新拋出的既有策略——該策略是刻意的，問題在於不該有例外傳到那裡

## 3. 驗證：crash 消除（最高優先）

- [ ] 3.0a **主要重現案例**：選中一顆支撐點 → 修改 Upper Diameter → 失焦。確認應用程式不終止、編輯正確套用、3D 視圖立即重畫
- [ ] 3.0b 反覆選取不同的點並修改各項 Top 參數數十次，確認全程不終止
- [ ] 3.0c 確認整個過程 `BadOptionTypeException` 拋出次數為 0、`[rethrow]` 為 0
- [ ] 3.0d 於 Release 版（不掛偵錯器）重跑 3.0a 與 3.0b

## 3. 驗證投影邏輯

- [ ] 3.1 選中 `contact_sphere_radius > 0` 的點，確認 `support_top_config_from_selection()` 回傳的 `support_contact_type` 為 `spSphere`，且其餘六個 key 皆反映該點
- [ ] 3.2 選中未啟用 contact sphere 的點，確認回傳 `spNone2`
- [ ] 3.3 無選取點時，確認回傳的 config 等同 `sla_process_config()`
- [ ] 3.4 確認整個過程 `BadOptionTypeException` 拋出次數為 0

## 4. 驗證下游路徑（本 change 的主要風險，該路徑此前從未實際執行）

- [ ] 4.1 建立兩顆 `support_head_front_diameter` 不同的手動支撐點，交替點選，確認 Top 群組的 `Upper Diameter` 欄位隨選取切換為各自的值
      — **這是關鍵判別**：修復前欄位「看起來有顯示手動改過的值」其實是沒人更新過的殘留 UI 狀態（使用者自己剛打進去的值），交替選取兩顆參數不同的點才能證明欄位確實從點讀回
- [ ] 4.2 確認七個 Top 欄位（含 `support_contact_type` 下拉選單）皆正確回填
- [ ] 4.3 **回填不得回寫**：點選一顆點後，確認該點參數與點選前完全相同，且未產生新的 undo 快照
- [ ] 4.4 確認 `m_support_point_top_field_update` guard（`OptionsGroup.cpp:688` 檢查、`Tab.cpp:7295-7301` 設定與延後解除）正確阻止回填被誤判為使用者編輯
- [ ] 4.5 SpinCtrl 型欄位：確認 `set_value()` 之後送出的非同步變更事件不被視為使用者編輯（`Tab.cpp:7298` 的 `CallAfter` 是為此而設）
- [ ] 4.6 取消選取後，確認 `end_support_point_top_field_display()` 正確還原為 preset 顯示

## 5. 驗證編輯回寫

- [ ] 5.1 選中點後修改 `support_head_front_diameter`，確認該點 `head_front_radius` 更新為新值一半
- [ ] 5.2 確認修改後 3D 視圖立即重畫、preview 錐體尺寸跟著變（此前因例外導致 `set_as_dirty()` 被略過）
- [ ] 5.3 確認該編輯**未**寫入 SLA print preset（`flush_process_top_fields_to_config()` 在有選取點時應早退）
- [ ] 5.4 修改 `support_contact_type` 由 `None` 改為 `Sphere`，確認該點 `contact_sphere_radius` 依既有規則更新、欄位回填後仍顯示 `Sphere`、視圖重畫
- [ ] 5.5 無選取點時修改 Top 欄位，確認仍走「下一顆新點的模板」路徑（`Tab.cpp:7342-7349`），行為不變

## 6. 迴歸

- [ ] 6.1 手動放置新支撐點時仍套用當下 Top 欄位值（`freeze_process_top_into_point()` 路徑不受影響）
- [ ] 6.2 切片輸出不變
- [x] 6.3 記錄「Release 不掛偵錯器時，點選支撐點的延遲是否消失」— **已實測：Release 版無可感知延遲**，該症狀為偵錯器成本，不屬產品問題，本 change 不需處理
- [ ] 6.4 若修好後暴露出原本被例外掩蓋的其他缺陷，個別記錄為 follow-up，不併入本 change

## 7. Follow-up（out of scope）

- 選取狀態與 live 參數對 preview 錐體外型的正確語意 → `fix-sla-support-preview-geometry-source-semantics`。**本 change 是其前置條件**：crash 未修好前無法完整測試 per-point 編輯行為
- 非編輯模式下手動點不套用 per-point 幾何 → `fix-sla-support-preview-stored-geometry-in-auto-mode`
- `sla_trafo` 改變後前端支撐點快取不失效 → `fix-sla-support-points-invalidate-on-trafo-change`
- 支撐點 undo/redo 資料不正確 → `fix-sla-support-points-undo-snapshot`
- 是否為 `ConfigBase::set(key, int)` 增加 `coEnum` 支援或加上編譯期防護（例如刪除 enum 多載以強制呼叫端明示）——影響整個 libslic3r，需獨立評估
