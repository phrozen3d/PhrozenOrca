## 1. 根因確認（已於提案階段完成）

- [x] 1.1 確認 `support_top_apply_point()`（`GLGizmoSlaSupports.cpp:302`）第一行 `cfg.set("support_contact_type", use_sphere ? spSphere : spNone2)` 因 `ContactType` 整數提升選到 `ConfigBase::set(key, int, bool)`（`Config.cpp:515`），而 `support_contact_type` 為 `coEnum`、不在該 switch 內 → `default: throw BadOptionTypeException`
- [x] 1.2 確認該行為函式第一個敘述，失敗模式是「整份 cfg 從未產出」而非「單一 key 沒設對」
- [x] 1.3 確認三個呼叫點：`notify_process_tab_selection_changed()`（`:1980`，有 try/catch 吞掉）、`OptionsGroup.cpp:695`、`Tab.cpp:7338`（後兩者無 try/catch）
- [x] 1.4 確認 `OptionsGroup.cpp:693` 的 `apply_process_top_option()` 在拋出前已執行完畢，故編輯值有寫進點裡，但 `:696-699` 的 `canvas->set_as_dirty()` 被略過
- [x] 1.5 確認同函式其餘六個 key 皆為 `coFloat`，`cfg.set(key, double)` 用法正確

## 2. 修正

- [ ] 2.1 將 `support_top_apply_point()` 中 `support_contact_type` 的寫入改為 `cfg.set_key_value("support_contact_type", new ConfigOptionEnum<ContactType>(use_sphere ? spSphere : spNone2))`
- [ ] 2.2 確認 `ConfigOptionEnum<ContactType>` 與 `ContactType` 在該 TU 可見（必要時補 include），且編譯零警告
- [ ] 2.3 掃描 `GLGizmoSlaSupports.cpp` 內全部 `cfg.set(...)` / `config.set(...)` 呼叫（含 `apply_weight_preset()`、`support_top_apply_point()`、`sync_new_point_params_from_config()` 等），逐一比對目標 option 的 `type()`，確認沒有第二處 enum→int 誤用
- [ ] 2.4 若 2.3 發現其他誤用，一併以相同方式修正並在此列出

## 3. 驗證投影邏輯

- [ ] 3.1 選中 `contact_sphere_radius > 0` 的點，確認 `support_top_config_from_selection()` 回傳的 `support_contact_type` 為 `spSphere`，且其餘六個 key 皆反映該點
- [ ] 3.2 選中未啟用 contact sphere 的點，確認回傳 `spNone2`
- [ ] 3.3 無選取點時，確認回傳的 config 等同 `sla_process_config()`
- [ ] 3.4 確認整個過程 `BadOptionTypeException` 拋出次數為 0

## 4. 驗證下游路徑（本 change 的主要風險，該路徑此前從未實際執行）

- [ ] 4.1 建立兩顆 `support_head_front_diameter` 不同的手動支撐點，交替點選，確認 Top 群組的 `Upper Diameter` 欄位隨選取切換為各自的值
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
- [ ] 6.3 記錄「Release 不掛偵錯器時，點選支撐點的延遲是否消失」——若消失則本 change 順帶解決該症狀；若仍存在則另行追查，不擴大本 change 範圍
- [ ] 6.4 若修好後暴露出原本被例外掩蓋的其他缺陷，個別記錄為 follow-up，不併入本 change

## 7. Follow-up（out of scope）

- 非編輯模式下手動點不套用 per-point 幾何 → `fix-sla-support-preview-stored-geometry-in-auto-mode`
- `sla_trafo` 改變後前端支撐點快取不失效 → `fix-sla-support-points-invalidate-on-trafo-change`
- 支撐點 undo/redo 資料不正確 → `fix-sla-support-points-undo-snapshot`
- 是否為 `ConfigBase::set(key, int)` 增加 `coEnum` 支援或加上編譯期防護（例如刪除 enum 多載以強制呼叫端明示）——影響整個 libslic3r，需獨立評估
