## Why

SLA 模式的 undo/redo 系統存在靜默 bug 與潛在 crash 風險：GLGizmoHollow 的 `on_save/on_load` 為空 stub（序列化全部預設值），GLGizmoDrill 的 radius/height slider 變更直接寫入 ModelObject 但從未呼叫 `TakeSnapshot`，導致使用者無法正確還原這些操作。目前 FDM 的 undo/redo 正常運作，但 SLA 的兩個關鍵 gizmo 存在缺口，需要補齊以達到與 FDM 相同的可靠度。

## 結案說明（2026-08-08 補述）

本 change 實際上分兩批落地，事後盤點發現兩批的命運不同：

- **GLGizmoHollow 半部**：`2026-05-07` 由 commit `be622a039`（`Phase 3`）落地，**至今仍是現行程式碼**，未被取代。對應 `sla-hollow-gizmo-undo-redo` capability，視為本 change 唯一真正交付的內容。
- **GLGizmoDrill 半部**（`begin_size_change`/`apply_size_change`，對應 `sla-drill-size-undo-redo` capability）：`2026-05-07` 由 commit `2da76fa12`（`Phase 4+5`）落地，但短短 6 天後的 `2026-05-13`，就被另一個獨立 change 的 commit `0f302f003`（`fix: apply-only undo boundary for Drill holes`，對應已 archive 的 `drill-apply-only-undo`）整個換掉，改為現在使用的 `m_working_holes` pending-apply 模型。目前程式碼中已完全找不到 `begin_size_change`/`apply_size_change`。這半部因此判定為**死設計，不再交付**，`sla-drill-size-undo-redo` spec 不併入主 spec。

剩餘的 8.1/8.2（自動化回歸測試）與 tasks.md 第 4-7 節的驗收，其設計前提（Drill 的 begin/apply 機制）已不存在，不再具有驗收意義，結案時不強制執行（第 1 節的 Layer 1 序列化測試已在當時通過，且與 UI 機制無關，不受此影響）。

## What Changes

- **GLGizmoHollow::on_save / on_load**（已落地，仍有效）：從 dummy stub 改為正確序列化 `m_pending_offset`、`m_pending_quality`、`m_pending_closing_d`、`m_enable_hollowing`，並在 `on_load` 中強制重置 `m_pending_owner = nullptr` 以確保 undo 後 `data_changed()` 從還原的 config 重新初始化。
- ~~**GLGizmoDrill — slider snapshot 補齊**：新增 `begin_size_change()` / `apply_size_change()` 方法…~~ **（已被 `drill-apply-only-undo` 取代，見上方結案說明，不再是本 change 的交付內容）**
- ~~**GLGizmoDrill — 新增成員變數**：`m_radius_before_change`、`m_height_before_change`、`m_holes_before_change`…~~ **（同上，隨 Drill 半部一併作廢）**

## Capabilities

### New Capabilities

- `sla-hollow-gizmo-undo-redo`：GLGizmoHollow 正確支援 undo/redo，包含 pending 參數的序列化與 gizmo 開啟期間 undo 的安全性。**（唯一併入主 spec 的 capability）**
- ~~`sla-drill-size-undo-redo`~~：**作廢，不併入主 spec**（描述的機制已被 `drill-apply-only-undo` 取代，見上方結案說明）。

### Modified Capabilities

（無現有 spec 層行為變更）

## Impact

- **修改檔案（實際落地且仍有效的部分）**：
  - `src/slic3r/GUI/Gizmos/GLGizmoHollow.cpp` — `on_save`、`on_load`
  - `src/slic3r/GUI/Gizmos/GLGizmoHollow.hpp` — 無新增成員（m_pending_owner 保留，on_load 重置）
- **曾經落地但已被取代（不在本次結案範圍內）**：`GLGizmoDrill.cpp/.hpp` 的 `begin_size_change`/`apply_size_change` 與三個追蹤成員變數，已被 `drill-apply-only-undo` 的 `m_working_holes` 模型取代
- **不影響範圍**：UndoRedo::Stack 核心、Model 序列化、GLGizmoSlaSupports、FDM gizmo 系統
- **風險**：低。Hollow 部分的改動為 additive；GLGizmoHollow 的 on_save/on_load 原本寫入 4 個假值，cereal stream 格式不變（舊值被正確值取代，欄位數相同）。
