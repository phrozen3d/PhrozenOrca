## 1. GLGizmoDrill — 修正 register_hole_raycasters_for_picking()

> **重要**：本函式的問題不只是 null，而是可能的 dangling pointer（從另一個 SLA gizmo undo 回 Drill 時，`m_c->selection_info()` 仍 valid 但 `model_object()` 指向已被刪除的舊物件）。修法是完全繞過 `m_c->selection_info()`，改用 `m_parent.get_selection()` 直接取 ModelObject。

- [x] 1.1 在 `GLGizmoDrill.cpp::register_hole_raycasters_for_picking()` 找到目前使用 `m_c->selection_info()` 取 ModelObject 的部分（line 434 附近：`const CommonGizmosDataObjects::SelectionInfo* info = m_c->selection_info(); if (info != nullptr && !info->model_object()->sla_drain_holes.empty()) {`）
- [x] 1.2 將取 ModelObject 的方式改為直接使用 Selection：
  ```
  const Selection& sel = m_parent.get_selection();
  const int obj_idx = sel.get_object_idx();
  if (obj_idx < 0 || !sel.get_model() || obj_idx >= (int)sel.get_model()->objects.size())
      return;
  const ModelObject* mo = sel.get_model()->objects[obj_idx];
  if (mo == nullptr || mo->sla_drain_holes.empty())
      return;
  const sla::DrainHoles& drain_holes = mo->sla_drain_holes;
  ```
  （移除對 `m_c->selection_info()` 的依賴；函式後續 loop 改用此 `drain_holes` 參照）
- [x] 1.3 確認函式後續的 `for (int i = 0; i < (int)drain_holes.size(); ...)` loop 仍使用同一個 `drain_holes` 參照，無額外的 `info->model_object()` 存取
- [x] 1.4 靜態 review：確認修改後 `data_changed()` 的 `if (m_hole_raycasters.empty()) register_hole_raycasters_for_picking()` 路徑在 Selection/m_c 刷新後仍能正確補回 registration

## 2. GLGizmoSlaSupports — 修正 update_point_raycasters_for_picking_transform()

- [x] 2.1 在 `GLGizmoSlaSupports.cpp::update_point_raycasters_for_picking_transform()` 找到 `const GLVolume* vol = selection.get_first_volume();` 這行（line 1762 附近）
- [x] 2.2 在該行之後緊接加上 null guard：`if (!vol) return;`（參考 `GLGizmoDrill::update_hole_raycasters_for_picking_transform()` 的相同 pattern，加上說明 undo/redo 時機的注解）
- [x] 2.3 靜態 review：確認 null guard 位置在現有 early-return（`if (m_editing_cache.empty() || m_point_raycasters.empty()) return;`）之後，以及 `vol->get_instance_transformation()` dereference 之前

## 3. 手動行為驗證

> 以下測試矩陣需在實際應用程式中手動執行。每個情境的驗收標準：不 crash、正常操作。

- [x] 3.1 **Drill active → Undo**：在 Drill gizmo 內新增洞後，在 Drill 仍 active 的情況下 Ctrl+Z，確認不 crash，且洞被正確還原
- [x] 3.2 **Drill → Move → Undo**：在 Drill 新增洞後切到 Move gizmo，再 Ctrl+Z（null path），確認不 crash，Drill gizmo 重新 activate 且洞資料正確
- [x] 3.3 **Drill → Hollow → Undo**：在 Drill 新增洞後切到 Hollow gizmo，再 Ctrl+Z（dangling path：Hollow keeps SelectionInfo valid），確認不 crash，Drill gizmo 重新 activate 且洞資料正確
- [x] 3.4 **Drill → Move → Redo**：接續 3.2，Ctrl+Y redo，確認不 crash
- [x] 3.5 **Support active → Undo**：在 SlaSupports editing mode 新增支撐點後，在 SlaSupports 仍 active 的情況下 Ctrl+Z，確認不 crash，支撐點正確還原
- [x] 3.6 **Support → Move → Undo**：在 SlaSupports 新增支撐點後切到 Move，再 Ctrl+Z，確認不 crash，SlaSupports 重新 activate 且 support points 可正常 picking
- [x] 3.7 **Support → Drill → Undo**：在 SlaSupports 新增支撐點後切到 Drill，再 Ctrl+Z，確認不 crash
- [x] 3.8 **Support → Move → Redo**：接續 3.6，Ctrl+Y redo，確認不 crash
- [x] 3.9 **Hollow → Move → Undo**：在 Hollow gizmo 按 Hollow 後切到 Move，再 Ctrl+Z，確認不 crash，hollowing 參數正確還原
- [x] 3.10 **Drill ↔ Support 交叉 undo/redo**：Drill 新增洞 → 切到 Support 新增支撐點 → 多次 Ctrl+Z 交替 undo，確認不 crash
- [x] 3.11 **undo 後重新進入 Drill**：完成跨功能 undo 後，手動重新開啟 Drill gizmo，確認 hole raycaster picking 正常（點選洞可選中）
- [x] 3.12 **undo 後重新進入 Support**：完成跨功能 undo 後，手動重新開啟 SlaSupports gizmo，確認 support point picking 正常（點選支撐點可選中/拖曳）
- [x] 3.13 **Hollow / Drill / Support 連續跨功能 smoke test**：以任意順序在三個 gizmo 間切換並交叉 undo/redo 至少 5 次，確認全程不 crash

## 5. GLGizmoHollow / GLGizmoDrill / GLGizmoSlaSupports — render path null guard

> **根因**：smoke test 中多次 gizmo 切換積累的 wx 事件可能在 undo 期間 activate_gizmo() 完成後、m_c->update() 執行前觸發 render。on_render() 與 on_render_input_window() 在此窗口中使用 m_c->selection_info() 而無 null/stale guard，造成 crash。原 Non-Goal「不修改 GLGizmoHollow」僅涵蓋 data path，未涵蓋 render path，已被實測推翻。

- [x] 5.1 在 `GLGizmoHollow.cpp::on_render()` line 97（`sel_info = m_c->selection_info()` 之後）加 null guard：`if (!sel_info) return;`
- [x] 5.2 在 `GLGizmoHollow.cpp::get_config_options()` line 183 前加 null guard on selection_info()：`if (!m_c->selection_info()) return out;`，並在後續 `mo->config.get()` 前加 stale detection（比對 mo 與 selection 當前 object，不符即 return out）
- [x] 5.3 在 `GLGizmoHollow.cpp::on_render_input_window()` line 212 前加 null guard：`if (!m_c->selection_info()) return;`，以及在 `if (!mo) return` 後加 stale detection（同 5.2 模式）
- [x] 5.4 在 `GLGizmoDrill.cpp::on_render_input_window()` line 776 前加 null guard + stale detection（同 5.3 模式：null check → mo null check → mo stale check，mo 傳入 render_new_drill_panel 前確保非 stale）；確認 Drill 與 Hollow 的 dangling pointer 風險相同，一併修正
- [x] 5.5 在 `GLGizmoSlaSupports.cpp::on_render_input_window()` line 694 前加 null guard + stale detection（同 5.4，mo 傳入 render_manual_support_panel / render_auto_support_panel 前確保非 stale）
- [x] 5.6 靜態 review：確認三個 gizmo 的 on_render() 與 on_render_input_window() 在 null guard 加入後，所有 m_c->selection_info() 的使用路徑均有保護

## 6. 手動行為驗證（Task 5 完成後）

- [x] 6.1 重新執行 3.13：Hollow / Drill / Support 連續跨功能 smoke test，確認 null guard 修正後不 crash
- [x] 6.2 確認 Hollow panel UI（hollowing 參數 slider）在正常使用時仍可正確顯示與互動（null guard 加入後不影響正常路徑）

## 7. 驗證過程發現的已知行為備註（非本 change 範圍）

> 以下行為在驗證過程中觀察到，分析後確認均**不屬於本 change（fix-sla-gizmo-undo-redo-crash）範圍**，記錄於此供未來參考。本 change 不處理這些問題。

**[A] Drill Apply 後 undo → pending holes 仍參與切片（已知問題，需另開 change）**

觀察：新增 3 個洞、按 Apply、Ctrl+Z 回到 2 個洞後切片，2 個洞仍被切出。

分析（已修正）：新版 Drill 設計為 pending-apply 模型——新增/編輯/移除孔應先進入 pending preview 狀態，按 Apply 後才寫入正式孔資料，切片只使用已 Apply 的正式孔。然而，目前實作每次新增洞仍透過 TakeSnapshot 立即寫入 `sla_drain_holes`；Apply 只觸發 reslice + 更新 session baseline，不做資料提交。這導致 undo 後畫面雖回到 2 孔，但這 2 孔仍是已寫入 Model 的正式資料，並非 pending preview 狀態，因此切片時仍被切出。

結論：此行為**不符合新版 pending-apply 設計，是已知問題**。確認非本次 fix-sla-gizmo-undo-redo-crash 引入（本次只改 raycaster null guard，未動孔資料提交路徑）。建議另開 change `fix-drill-pending-holes-undo-state` 處理。

**[B] SlaSupports Apply + 跨 gizmo undo → 支撐點行為需後續確認**

觀察：SlaSupports 手動新增支撐後 Apply，切到其他功能再 undo，支撐點有時全部被清掉。

分析：根據程式碼，Apply 建立 `Action` 型 snapshot（寫入 mo->sla_support_points），BBS undo 應停在此 snapshot（支撐點仍存在）。若觀察到一次 Ctrl+Z 清空所有支撐，可能是多次按 Ctrl+Z 或有其他路徑（如 reslice 觸發自動支撐重算）。本 change 未改動任何 Apply / snapshot / reslice 路徑，確認非本次引入。需獨立重現並調查。