## Why

GLGizmoDrill 的 UI 語意是「按 Apply 才正式提交孔資料」，但目前所有孔操作（Add / Delete / Move / Size 修改）都直接寫入 `ModelObject::sla_drain_holes`，使得 slicer 在未 Apply 的情況下也能看到並切出 pending 孔；Undo/Redo 後 stash-sync 機制又使已 Apply 的邊界被抹除，形成 incoherent hybrid，切片結果與 Apply 語意不一致。

## What Changes

- 在 `GLGizmoDrill` 引入 `m_working_holes`（`sla::DrainHoles`）作為 session 內的 pending working set
- 所有孔的 Add / Delete / Move / Size 操作改寫入 `m_working_holes`，不再直接寫入 `mo->sla_drain_holes`
- `render_points()`、raycaster 建置、dragging / selection / size-change 路徑全部改讀 `m_working_holes`
- Apply 按鈕改為：`TakeSnapshot("Apply drain holes")` → `sla_drain_holes = m_working_holes` → reslice
- Undo/Redo 後（`data_changed(is_serializing=true)`）：以 cereal 還原後的 `sla_drain_holes` 重建 `m_working_holes`，並清空 selection cache 避免越界
- Exit without Apply（`on_set_state(Off)`）：直接丟棄 `m_working_holes`；`sla_drain_holes` 保持最後 Apply 後的狀態，不做任何 restore 寫回
- 廢棄 `m_holes_stash` 的 exit-restore 責任；若 object-switch path 仍需還原，以 `sla_drain_holes`（本身即 applied state）直接操作，不需要獨立 stash

## Capabilities

### New Capabilities

- `drill-working-set`: GLGizmoDrill 引入 m_working_holes 作為 pending preview working set；Apply 是唯一從 working set 提交到 sla_drain_holes 的路徑；session 內的編輯不影響 slicer 讀取的正式孔資料

### Modified Capabilities

- `drill-raycaster-registration-safety`: raycaster 建置來源從 sla_drain_holes 改為 m_working_holes，register / unregister / transform 路徑需對應更新（安全防護邏輯維持不變）

## Impact

- 修改範圍：`src/slic3r/GUI/Gizmos/GLGizmoDrill.hpp` / `.cpp`（唯一修改檔）
- 不影響：SLA slicing pipeline、`ModelObject` 資料模型、3MF / cereal 序列化格式、GLGizmoHollow、GLGizmoSlaSupports
- 不新增 ModelObject 欄位
- Session 內無 fine-grained Undo（Add / Delete / Move / Size 不產生 snapshot）；Ctrl+Z 在 Apply 邊界有效，使用者必須先 Apply 再 Undo 才能回到前一個 Apply 狀態