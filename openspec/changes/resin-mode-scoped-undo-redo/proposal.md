## Why

Resin 的三個編輯 mode（Generate support / Hollow / Drill）目前 undo/redo 行為不一致：SlaSupports 已透過既有的雙 stack 機制（`enter_gizmos_stack` / `leave_gizmos_stack`）達成「進 mode 開子堆疊、出 mode 收斂成一筆」，但 Hollow 與 Drill 是「apply-only」模型——每次 Apply 直接在 main stack 壓一筆，導致同一段 session 的多次 Apply 散成多筆、且無法回朔到「剛進 mode」的乾淨邊界。更嚴重的是，任一 mode 開著時對該模型做結構性變更（刪除物件、清盤、load/reload project 等）會：(1) 把結構變更的快照誤打進子堆疊、(2) gizmo 仍持有已釋放 ModelObject 的懸空指標而 crash。此區塊已有前例 crash（commit `e367bb0025`），需以一致、可維護、且無遺漏的方式收斂。

**In-mode step 的顆粒度採「per-Apply」（定義 X）**：in-mode 的一個 undo step = 一次 Apply commit，不是每次 UI 拖桿。pending UI 編輯（Hollow 參數 slider、Drill 洞的 add/delete/move/size）維持既有的 pending-apply 模型、不各自記快照。因此本 change 是**延伸** Hollow/Drill 既有的 apply-only 架構（把 Apply 快照從 main 改記到 scoped 子堆疊、離開時收斂），而非取代它。

## What Changes

- **三個 mode 統一走 scoped 子堆疊**：進入 mode 開啟次級 undo/redo 子堆疊（以「剛進 mode」為 baseline）；in-mode 的 undo step 以 **Apply commit** 為單位（pending UI 編輯不記快照）；in-mode undo 最多回到 baseline、不再往回污染 main；in-mode redo 只推進到本 session 的 Apply step；離開 mode 時把最終結果收斂成 main stack 上的**單一**快照。
- **pending 工作狀態於 in-mode undo 後重建**：多次 Apply 後 in-mode undo 回到前一個 Apply/baseline 時，pending 暫存（Drill `m_working_holes`、Hollow pending 參數）需從還原後的 model 重新初始化——沿用既有路徑（Drill `data_changed(is_serializing=true)`、Hollow `on_load` 重置 pending owner）。
- **no-op 收斂語意**：若進 mode 後未 Apply，或有 Apply 但已 undo 回 baseline，離開時**不**在 main 記任何快照（採結構性判斷：子堆疊是否存在可 undo 的快照）。
- **共用 enter/leave 機制**：抽出跨三個 gizmo 共用的進出子堆疊入口（含離開時的收斂與 no-op 判斷、離開快照命名），SlaSupports 一併收編改用同一套；各 gizmo 保留自己的「進入時機」觸發點（Hollow/Drill 為 gizmo 開關 `on_set_state`；SlaSupports 維持其 editing mode 觸發）。
- **結構性變更安全（強制收斂 + 兜底）**：任何結構性變更（刪除物件 / 刪除 instance / 清盤 / New·Open·拖入 project / reload·replace mesh 等）在其快照落地前，若偵測到子堆疊 active，先強制收斂子堆疊、切回 main；並在 gizmo 資料層對「聚焦的 ModelObject 已消失」做 null-guard 自關，作為不需枚舉入口的最終兜底。
- **其餘 undo/redo 行為維持與原始一致**（main stack 上的非 mode 操作不變）。

## Capabilities

### New Capabilities

- `resin-mode-scoped-undo-stack`：Generate support / Hollow / Drill 進入時開啟 scoped 子堆疊，in-mode undo/redo 受 baseline 與本 mode step 邊界約束，離開時收斂為單一 main 快照，no-op session 不留痕。
- `resin-mode-structural-mutation-safety`：mode 子堆疊 active 期間發生結構性變更時，透過「強制收斂子堆疊 + gizmo 資料層 null-guard 自關」的 containment 策略，確保快照落在正確堆疊且不產生懸空指標；此保證一次覆蓋所有結構性入口（含未來新增者）。

### Modified Capabilities

- `drill-apply-only-undo`：延伸「Apply is the sole undo/redo boundary for Drill」requirement——Apply 快照改記到 scoped 子堆疊、同 session 多次 Apply 為 in-session steps、離開 Drill 時收斂為 main 上單一快照（或 no-op）。pending working set（`m_working_holes`）、`data_changed` 重建、exit 丟棄 pending 等 requirements 不變。

未修改但相關：
- `hollow-action-buttons`：其 pending-apply 模型不變（該 spec 未涉及 undo 行為）；Hollow 的 undo 行為屬 greenfield，全由新 capability `resin-mode-scoped-undo-stack` 定義（ADD）。
- `sla-supports-apply-undo-stack`：SlaSupports 收編改用共用機制屬實作層變更，Manual Apply 觀察行為不變；其新獲得的結構性變更安全保證由新 capability `resin-mode-structural-mutation-safety` 涵蓋。

## Impact

- **建立於已落地基礎**（非依賴未完成 change）：
  - `drill-apply-only-undo`（已 archive）：Drill 的 `m_working_holes` pending 模型、Apply-only 快照、`data_changed(is_serializing=true)` 重建 working set、exit 丟棄 pending——本 change 直接沿用並延伸。
  - Hollow `on_save/on_load` 的真序列化（`m_pending_*` + `m_pending_owner=nullptr` reset）已存在於原始碼（`fix-sla-undo-redo` 的 Hollow 半已落地），提供 in-mode undo 後 pending 重建的基礎。
  - **注意**：`fix-sla-undo-redo` proposal 描述的 Drill `begin_size_change/apply_size_change` 已被 revert（commit `040a240eee`）並由 `drill-apply-only-undo`（commit `0f302f003f`）取代；本 change 不依賴那套已失效的設計。
- **修改檔案（預期）**：
  - `src/slic3r/GUI/Gizmos/GLGizmoBase.hpp/.cpp` — 共用 enter/leave 子堆疊入口與離開快照命名 virtual。
  - `src/slic3r/GUI/Gizmos/GLGizmoHollow.*`、`GLGizmoDrill.*` — 於 `on_set_state` 接進出子堆疊、移除直接打在 main 的 Apply 快照、加 null-guard。
  - `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.*` — 收編改用共用機制。
  - `src/slic3r/GUI/Plater.cpp` — 結構性變更入口（`remove_selected`、`delete_object_from_model`、`delete_all_objects_from_model`、`remove_curr_plate_all`、load/reload 路徑）於快照落地前強制收斂子堆疊的 choke point；含 `wxID_DELETE`（`ObjectList::remove`）旁路。
- **不影響範圍**：FDM gizmo 系統、UndoRedo::Stack 核心資料結構、Model 序列化格式。
- **測試**：Layer 1 自動化（libslic3r 層，延續 `test_sla_undo_redo_data.cpp` 的序列化 round-trip）；Layer 2 手動矩陣（GUI 行為，Catch2 到不了）。
- **範圍排除**：不採「mode 中禁止刪除 / 禁止 delete all」的 prevention 作法（該法有 `wxID_DELETE` 旁路且無法涵蓋非刪除的結構變更，僅屬 UX 補強，不在本 change）。
