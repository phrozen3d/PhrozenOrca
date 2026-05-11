## Why

SLA gizmo 在 undo/redo 跨功能操作時幾乎必 crash。根本原因有兩個：(1) `GLGizmosManager::load()` 於還原過程中呼叫 `register_raycasters_for_picking()`，此時 `m_c->selection_info()` 尚未刷新，若前一個 gizmo 也是 SLA gizmo（保留 SelectionInfo valid），`model_object()` 回傳的是 undo 後已被刪除物件的 **dangling pointer**；(2) `update_after_undo_redo()` 內部的 `this->update()` 可能在 selection 還原前觸發 repaint，導致 `get_first_volume()` 回傳 null。

## What Changes

- **GLGizmoDrill::register_hole_raycasters_for_picking()**：停止使用可能為 stale/dangling 的 `m_c->selection_info()->model_object()`，改用 `m_parent.get_selection()` 直接取 ModelObject（與 SelectionInfo::on_update() 相同邏輯）。此時 Model 已還原，透過 Selection 取到的是正確的新 ModelObject。
- **GLGizmoSlaSupports::update_point_raycasters_for_picking_transform()**：加 `if (!vol) return;` null guard，防止 `get_first_volume()` 在 selection 尚未完全還原時回傳 null 導致 crash。

## Capabilities

### New Capabilities

- `drill-raycaster-registration-safety`：GLGizmoDrill 的 `register_hole_raycasters_for_picking()` 繞過可能 stale 的 `m_c->selection_info()`，改用 Selection 直接取 ModelObject，安全處理 null 與 dangling 兩種情況。
- `sla-supports-raycaster-transform-safety`：GLGizmoSlaSupports 的 `update_point_raycasters_for_picking_transform()` 在 `get_first_volume()` 回傳 null 時安全退出，raycaster transform 由下一幀更新。

### Modified Capabilities

（無現有 spec 層行為變更）

## Impact

- **修改檔案**：
  - `src/slic3r/GUI/Gizmos/GLGizmoDrill.cpp` — `register_hole_raycasters_for_picking()`
  - `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp` — `update_point_raycasters_for_picking_transform()`
- **不影響範圍**：UndoRedo::Stack 核心、GLGizmosManager 時序、Hollow gizmo、FDM gizmo 系統、ModelObject 序列化
- **風險**：極低。Drill 的改動是 additive（換一個等價但更安全的取值方式）；SlaSupports 的改動是 additive null guard；兩者均只在原本會 crash 的路徑上提早 return，正常路徑不受影響。