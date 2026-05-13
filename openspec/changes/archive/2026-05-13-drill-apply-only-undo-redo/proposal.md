## Why

目前 GLGizmoDrill 的每一個 Add/Delete/Move/Size 操作都立即呼叫 `TakeSnapshot` 並寫入 `mo->sla_drain_holes`，導致 undo stack 充斥細粒度操作節點，且每個中間狀態都會觸發 slicing pipeline 感知。應改為以 Apply 為唯一 undo 邊界：session 內的編輯只存在於 `m_working_holes`，不進 undo stack、不寫入 ModelObject，Apply 時才一次性提交並建立 snapshot。

## What Changes

- **新增 `m_working_holes`**：GLGizmoDrill 新增 `sla::DrainHoles m_working_holes` 作為 session 內 pending 工作集，取代直接讀寫 `mo->sla_drain_holes` 的所有編輯路徑。
- **Add/Delete/Move/Size 改寫 `m_working_holes`**：這四種操作不再呼叫 `TakeSnapshot`，不再寫入 `mo->sla_drain_holes`，所有變更僅存於 `m_working_holes`。
- **render/raycaster/drag/selection 路徑改讀 `m_working_holes`**：繪製、raycaster 建立、hover/selection 判斷、drag 操作皆以 `m_working_holes` 為資料來源。
- **Apply 成為唯一 undo 節點**：Apply 流程改為 `TakeSnapshot("Apply drain holes")` → `mo->sla_drain_holes = m_working_holes` → `reslice_until_step(slaposDrillHoles)`。
- **data_changed(is_serializing=true) 重建 `m_working_holes`**：Undo/Redo 後，`data_changed` 以 cereal 還原後的 `mo->sla_drain_holes` 重建 `m_working_holes`，並 reload cache 與 raycasters。
- **Exit 丟棄 `m_working_holes`**：離開 Drill 時直接丟棄 `m_working_holes`，不恢復 `mo->sla_drain_holes`。
- **移除 `m_holes_stash` / `m_stash_initialized` 的 exit-restore 責任**：此兩成員不再負責 exit-restore 或 Apply baseline，可移除或留空。

**明確不做**：不修改 GLGizmosManager.cpp、不攔截 Ctrl+Z、不實作 `has_pending_changes()` / `discard_pending_changes()`、不做 pending dirty 時 Ctrl+Z 先 discard pending、不實作 session-level local undo stack、不修改 SLA slicing pipeline / ModelObject 欄位 / 3MF/cereal 格式。

## Capabilities

### New Capabilities

- `drill-apply-only-undo`: Drill gizmo session 架構 — `m_working_holes` 作為 pending 工作集，只有 Apply 產生 undo snapshot，Undo/Redo 後以 cereal 還原結果重建 `m_working_holes`。

### Modified Capabilities

<!-- 現有 sla-drill-size-undo-redo spec（fix-sla-undo-redo 本地 spec）的 per-operation snapshot 語意被本次 change 全面取代，但因該 spec 未發佈至全域 openspec/specs/，此處不列入 modified。 -->

## Impact

- **主要修改**：`src/slic3r/GUI/Gizmos/GLGizmoDrill.cpp`、`src/slic3r/GUI/Gizmos/GLGizmoDrill.hpp`
  - 新增 `m_working_holes` 成員
  - 重寫 `data_changed()`、`on_set_state()`、Add/Delete/Move/Size 所有操作路徑
  - 重寫 Apply handler
  - `render()`、`on_start_dragging()`、`on_stop_dragging()`、raycaster 路徑改讀 `m_working_holes`
- **不影響**：`GLGizmosManager.cpp`、`GLGizmoHollow`、`GLGizmoSlaSupports`、ModelObject 序列化格式、SLA slicing pipeline、undo stack 核心