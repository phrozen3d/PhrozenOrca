## Why

SLA 模式的 undo/redo 系統存在靜默 bug 與潛在 crash 風險：GLGizmoHollow 的 `on_save/on_load` 為空 stub（序列化全部預設值），GLGizmoDrill 的 radius/height slider 變更直接寫入 ModelObject 但從未呼叫 `TakeSnapshot`，導致使用者無法正確還原這些操作。目前 FDM 的 undo/redo 正常運作，但 SLA 的兩個關鍵 gizmo 存在缺口，需要補齊以達到與 FDM 相同的可靠度。

## What Changes

- **GLGizmoHollow::on_save / on_load**：從 dummy stub 改為正確序列化 `m_pending_offset`、`m_pending_quality`、`m_pending_closing_d`、`m_enable_hollowing`，並在 `on_load` 中強制重置 `m_pending_owner = nullptr` 以確保 undo 後 `data_changed()` 從還原的 config 重新初始化。
- **GLGizmoDrill — slider snapshot 補齊**：新增 `begin_size_change()` / `apply_size_change()` 方法，仿照 GLGizmoBrimEars 的 `begin_radius_change` / `apply_radius_change` 模式，在 diameter 與 depth 的 slider 及 InputFloat 操作 commit 時正確取得 snapshot（restore old → TakeSnapshot → re-apply new）。
- **GLGizmoDrill — 新增成員變數**：`m_radius_before_change`、`m_height_before_change`、`m_holes_before_change`，追蹤 slider 拖曳前的舊值。

## Capabilities

### New Capabilities

- `sla-hollow-gizmo-undo-redo`：GLGizmoHollow 正確支援 undo/redo，包含 pending 參數的序列化與 gizmo 開啟期間 undo 的安全性。
- `sla-drill-size-undo-redo`：GLGizmoDrill 的 hole radius/height slider 與 InputFloat 操作納入 undo/redo 系統，commit 時正確建立 snapshot。

### Modified Capabilities

（無現有 spec 層行為變更）

## Impact

- **修改檔案**：
  - `src/slic3r/GUI/Gizmos/GLGizmoHollow.cpp` — `on_save`、`on_load`
  - `src/slic3r/GUI/Gizmos/GLGizmoHollow.hpp` — 無新增成員（m_pending_owner 保留，on_load 重置）
  - `src/slic3r/GUI/Gizmos/GLGizmoDrill.cpp` — 新增 `begin_size_change`、`apply_size_change`，修改 slider / InputFloat 事件處理
  - `src/slic3r/GUI/Gizmos/GLGizmoDrill.hpp` — 新增三個成員變數
- **不影響範圍**：UndoRedo::Stack 核心、Model 序列化、GLGizmoSlaSupports（已正確實作）、FDM gizmo 系統
- **風險**：低。所有改動為 additive；GLGizmoHollow 的 on_save/on_load 原本寫入 4 個假值，cereal stream 格式不變（舊值被正確值取代，欄位數相同）。
