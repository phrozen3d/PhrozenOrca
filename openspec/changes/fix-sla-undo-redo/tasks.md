## 1. Layer 1 測試 — SLA 資料序列化（先於實作撰寫）

- [ ] 1.1 在 `tests/sla_print/CMakeLists.txt` 加入 `test_sla_undo_redo_data.cpp`
- [ ] 1.2 建立 `tests/sla_print/test_sla_undo_redo_data.cpp`，加入 `sla_drain_holes` cereal round-trip 測試（單洞 pos/normal/radius/height 驗證）
- [ ] 1.3 加入多洞 count 與順序保留測試
- [ ] 1.4 加入 `hollowing_min_thickness`、`hollowing_quality`、`hollowing_closing_distance` 透過 `ModelObject::config` 的 round-trip 測試
- [ ] 1.5 執行 `cd build && ctest -R sla_print` 確認 Layer 1 測試全數通過（現有序列化正確，測試應立即通過）

## 2. Layer 2 測試 — Gizmo 序列化合約（先於實作撰寫）

- [ ] 2.1 在 `tests/slic3rutils/CMakeLists.txt` 加入 `test_sla_gizmo_serialization.cpp`
- [ ] 2.2 建立 `tests/slic3rutils/test_sla_gizmo_serialization.cpp`，定義 `HollowSerialState` stub struct（4 個欄位：float offset, float quality, float closing_d, bool enable）
- [ ] 2.3 加入 `HollowSerialState` non-default values round-trip 測試
- [ ] 2.4 加入 cereal stream 完整消耗驗證（REQUIRE_NOTHROW round-trip）
- [ ] 2.5 定義 `DrillSerialState` stub struct（4 個欄位：float radius, float height, vector<bool> selected, bool empty）
- [ ] 2.6 加入 `DrillSerialState` round-trip 測試，包含非空的 `selected` vector
- [ ] 2.7 執行 `cd build && ctest -R slic3rutils` 確認 Layer 2 測試全數通過（stub 是規格，應立即通過）

## 3. GLGizmoHollow — 修正 on_save / on_load

- [ ] 3.1 在 `GLGizmoHollow.hpp` 確認 `m_pending_offset`、`m_pending_quality`、`m_pending_closing_d`、`m_enable_hollowing` 四個成員存在且型別為 `float`、`float`、`float`、`bool`
- [ ] 3.2 修改 `GLGizmoHollow.cpp::on_save`：移除假值，改為 `ar(m_pending_offset, m_pending_quality, m_pending_closing_d, m_enable_hollowing)`（4 個欄位，順序對應 `HollowSerialState`）
- [ ] 3.3 修改 `GLGizmoHollow.cpp::on_load`：移除假值，改為 `ar(m_pending_offset, m_pending_quality, m_pending_closing_d, m_enable_hollowing)`，結尾加上 `m_pending_owner = nullptr`
- [ ] 3.4 對照 `HollowSerialState` 的欄位順序做靜態 review，確認 on_save 與 on_load 完全一致

## 4. GLGizmoDrill — 新增成員變數

- [ ] 4.1 在 `GLGizmoDrill.hpp` 新增 `float m_radius_before_change = 0.f`
- [ ] 4.2 在 `GLGizmoDrill.hpp` 新增 `float m_height_before_change = 0.f`
- [ ] 4.3 在 `GLGizmoDrill.hpp` 新增 `sla::DrainHoles m_holes_before_change`（型別與 `mo->sla_drain_holes` 一致）

## 5. GLGizmoDrill — 實作 begin_size_change / apply_size_change

- [ ] 5.1 在 `GLGizmoDrill.hpp` 宣告 `begin_size_change(float old_radius, float old_height)` 和 `apply_size_change(const std::string& snapshot_name)`
- [ ] 5.2 在 `GLGizmoDrill.cpp` 實作 `begin_size_change`：若 `m_radius_before_change == 0.f && m_height_before_change == 0.f`，儲存舊值並複製所有洞至 `m_holes_before_change`
- [ ] 5.3 在 `GLGizmoDrill.cpp` 實作 `apply_size_change`：
  - 若無暫存值則直接返回
  - 備份新值 → 還原 `mo->sla_drain_holes` 為 `m_holes_before_change` → `TakeSnapshot` → 重套新值至選取洞 → `set_as_dirty()` → 清除暫存

## 6. GLGizmoDrill — 接入 Diameter slider 與 InputFloat

- [ ] 6.1 在 diameter slider 首次啟動時呼叫 `begin_size_change(m_new_hole_radius, m_new_hole_height)`
- [ ] 6.2 在 diameter slider 的 `deactivated_after_edit` 呼叫 `apply_size_change("Change hole radius")`
- [ ] 6.3 在 diameter InputFloat 的 `IsItemActivated()` 呼叫 `begin_size_change(m_new_hole_radius, m_new_hole_height)`
- [ ] 6.4 在 diameter InputFloat 的 `IsItemDeactivatedAfterEdit()` 分支套用新值後呼叫 `apply_size_change("Change hole radius")`

## 7. GLGizmoDrill — 接入 Depth slider 與 InputFloat

- [ ] 7.1 在 depth slider 首次啟動時呼叫 `begin_size_change(m_new_hole_radius, m_new_hole_height)`
- [ ] 7.2 在 depth slider 的 `deactivated_after_edit` 呼叫 `apply_size_change("Change hole depth")`
- [ ] 7.3 在 depth InputFloat 的 `IsItemActivated()` 呼叫 `begin_size_change(m_new_hole_radius, m_new_hole_height)`
- [ ] 7.4 在 depth InputFloat 的 `IsItemDeactivatedAfterEdit()` 分支套用新值後呼叫 `apply_size_change("Change hole depth")`

## 8. 自動化測試回歸確認

- [ ] 8.1 執行 `cd build && ctest -R "sla_print|slic3rutils"` 確認 Layer 1 和 Layer 2 測試在實作後仍全數通過
- [ ] 8.2 確認新加的測試檔案已被 CMakeLists.txt 正確納入（檢查 ctest 輸出有出現對應的 test name）

## 9. 手動行為驗證

- [ ] 9.1 在 Hollow gizmo 中按 Hollow 兩次，undo 兩次，確認 hollowing 參數正確還原
- [ ] 9.2 在 Hollow gizmo 中按 Hollow 後離開至 prepare，執行 undo，確認 config 還原且 SLA 重算
- [ ] 9.3 在 Drill gizmo 中調整 diameter slider，放開後 undo，確認所有選取洞的 radius 還原
- [ ] 9.4 在 Drill gizmo 中連續修改 diameter 和 depth，undo 兩次，確認各自獨立還原
- [ ] 9.5 確認 add/delete/move 洞的 undo 行為未受影響
- [ ] 9.6 在 SlaSupports gizmo 執行 undo/redo，確認現有功能未退化
