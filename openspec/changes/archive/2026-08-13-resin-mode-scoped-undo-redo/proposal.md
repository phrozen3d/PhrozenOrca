## Why

Resin 的三個編輯 mode（Generate support / Hollow / Drill）目前 undo/redo 行為不一致：SlaSupports 已透過既有的雙 stack 機制（`enter_gizmos_stack` / `leave_gizmos_stack`）達成「進 mode 開子堆疊、出 mode 收斂成一筆」，但 Hollow 與 Drill 是「apply-only」模型——每次 Apply 直接在 main stack 壓一筆，導致同一段 session 的多次 Apply 散成多筆、且無法回朔到「剛進 mode」的乾淨邊界。更嚴重的是，任一 mode 開著時對該模型做結構性變更（刪除物件、清盤、load/reload project 等）會：(1) 把結構變更的快照誤打進子堆疊、(2) gizmo 仍持有已釋放 ModelObject 的懸空指標而 crash。此區塊已有前例 crash（commit `e367bb0025`），需以一致、可維護、且無遺漏的方式收斂。

## ⚠️ 結案後方向修正（2026-08-09）

本 change 最初的方向是「三個 mode 統一走 scoped 子堆疊」（下方 Decision A-E，見 design.md），且已完整實作、code review 通過。但進入 Section 7 手動驗證後，連續發現一系列互相牽連的問題：

1. `enter_gizmos_stack()` 的 baseline 快照預設型別算「有修改」，導致離開任一 mode 都會誤記一筆空白 main-stack undo
2. SlaSupports 的 in-mode Apply（`commit_manual_edits_keep_editing`）用「離開再重進」實作收斂，導致子堆疊被清空重建、Apply 前的個別編輯歷史消失、undo/redo 按鈕行為異常
3. 為了修正「Auto 模式下 undo 會意外跳出面板、吃掉面板開啟前的操作」，加了一個「碰到 mode 邊界就硬擋」的機制，結果又在「undo 復原歷史、順帶把面板重新打開」這個**完全正常**的情境下，把使用者卡在無法繼續 undo 的狀態

這三個問題的根源是同一件事：**子堆疊 scoped-undo 這套機制，是 PrusaSlicer 為 FDM 某些 gizmo（如 BrimEars）設計的，套用到 Support/Hollow/Drill 上跟現有的單一 stack 邏輯不相容**——每修一個邊界情況，就會在另一個邊界情況冒出新問題。決定**放棄 scoped 子堆疊**，退回單一 main stack：三個 mode 的每一次 Apply／新增／刪除／移動都直接記在 main stack 上，不再收斂成一筆；undo/redo 過程中面板被動打開/關閉視為正常、可預期的行為（就像復原任何其他操作一樣）。

代價是 undo 歷史變得比較「囉唆」（不會自動收斂），換來的是**行為完全可預測、不需要維護子堆疊與 main stack 之間的協調邏輯**——這幾週踩到的每一個 bug，追根究底都是雙堆疊狀態機本身的邊界情況，退回單一堆疊後這整類問題不復存在。

下方 Decision A-E 與其對應的 `resin-mode-scoped-undo-stack` capability spec 保留作為歷史紀錄（含完整實作、驗證、發現的問題），但**不是本 change 最終交付的行為**。最終行為見 Decision G 與新 capability `resin-mode-single-stack-undo-redo`。

## What Changes（最終版，見 Decision G）

- **三個 mode 統一使用單一 main stack**：不再開子堆疊。Hollow/Drill 的 Apply 按鈕、SlaSupports 的新增/移除/移動點/Apply，都各自是一筆獨立、恆落在 main stack 上的 `TakeSnapshot`，不收斂、不特殊處理。
- **SlaSupports 移除 Mechanism A**：`wants_enter_leave_snapshots()` 移除，面板整體開關不再額外包一層 `EnteringGizmo`/`LeavingGizmo` 主 stack 標記。
- **結構性變更安全，只保留 null-guard 兜底**：不再需要「強制收斂子堆疊」的 choke point（沒有子堆疊可收斂）；保留「聚焦的 ModelObject 消失時 gizmo 自我關閉」這個防護，跟堆疊架構無關、持續有效。
- **SLA 支撐 mesh 與 undo/redo 同步**：新增 `GLGizmoSlaSupports::resync_after_undo_redo()`——每次 undo/redo 落在 Support mode（非編輯模式）時，強制重新載入顯示快取並依目前檢視模式 invalidate 對應的 SLA 步驟，取代原本不可靠、掛在 `RECALCULATE_SLA_SUPPORTS` 旗標上的判斷。
- **離開面板強制顯示 Structure 檢視**：修正「全程只用 Auto 檢視、切到 Points 子檢視後離開，即使背景已成功生成支撐、一般畫面卻完全看不到」的問題。

## Capabilities

### New Capabilities

- `resin-mode-single-stack-undo-redo`：Generate support / Hollow / Drill 的所有可還原操作統一記錄在單一 main stack 上，不做 scoped 收斂；undo/redo 可能連帶開關面板，此為預期行為；離開 Support 面板時強制可見 Structure 檢視；undo/redo 落在 Support mode 時強制重新同步 SLA 背景運算結果。
- `resin-mode-structural-mutation-safety`（範圍縮減，見下）：僅保留「聚焦 ModelObject 消失時 gizmo 自我關閉」的 null-guard 保證。

### 歷史／已放棄

- ~~`resin-mode-scoped-undo-stack`~~：完整實作過、Section 7 驗證期間發現與現有單一 stack 架構不相容而放棄，spec 移除，不併入主 spec。決策過程見 design.md Decision G。

### Modified Capabilities

（無。`drill-apply-only-undo` 的 pending working set／`data_changed` 重建／exit 丟棄 pending 等既有 requirements 完全不變——本 change 最終只是讓 Apply 快照維持原本就落在 main stack 上，沒有任何延伸。）

## Impact

- **建立於已落地基礎**（非依賴未完成 change）：
  - `drill-apply-only-undo`（已 archive）：Drill 的 `m_working_holes` pending 模型、Apply-only 快照、`data_changed(is_serializing=true)` 重建 working set、exit 丟棄 pending——本 change 直接沿用並延伸。
  - Hollow `on_save/on_load` 的真序列化（`m_pending_*` + `m_pending_owner=nullptr` reset）已存在於原始碼（`fix-sla-undo-redo` 的 Hollow 半已落地），提供 in-mode undo 後 pending 重建的基礎。
  - **注意**：`fix-sla-undo-redo` proposal 描述的 Drill `begin_size_change/apply_size_change` 已被 revert（commit `040a240eee`）並由 `drill-apply-only-undo`（commit `0f302f003f`）取代；本 change 不依賴那套已失效的設計。
- **修改檔案（實際，Decision G 之後）**：
  - `src/slic3r/GUI/Gizmos/GLGizmoBase.hpp/.cpp` — 移除共用 enter/leave 子堆疊入口（`enter_mode_undo_stack`/`leave_mode_undo_stack`）與相關 virtual，已是死程式碼
  - `src/slic3r/GUI/Gizmos/GLGizmoHollow.*`、`GLGizmoDrill.*` — `on_set_state` 移除進出子堆疊呼叫，Apply 按鈕的 `TakeSnapshot` 維持原樣（本來就落在 main stack）
  - `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.*` — 移除子堆疊進出與 `wants_enter_leave_snapshots()`；新增 `resync_after_undo_redo()`；`on_set_state(Off)` 新增強制 Structure 檢視可見
  - `src/slic3r/GUI/Gizmos/GLGizmosManager.cpp` — `update_after_undo_redo()` 呼叫 `resync_after_undo_redo()` 取代舊的旗標判斷
  - `src/slic3r/GUI/Plater.cpp` — `leave_gizmos_stack()` 的 baseline-timestamp 修正保留（`GLGizmoBrimEars` 仍直接使用這組底層 API，此修正對它同樣正確）；~~結構性變更入口的 choke point~~ 未採用（不再需要）
- **不影響範圍**：FDM gizmo 系統、UndoRedo::Stack 核心資料結構、Model 序列化格式、`GLGizmoBrimEars`（仍使用原本的 `enter_gizmos_stack`/`leave_gizmos_stack`，不受影響）。
- **測試**：Layer 1 自動化維持（`test_sla_undo_redo_data.cpp` 序列化 round-trip，與堆疊架構無關）；Layer 2 手動矩陣改版，見 tasks.md 第 7 節。
- **範圍排除**：不採「mode 中禁止刪除 / 禁止 delete all」的 prevention 作法（該法有 `wxID_DELETE` 旁路且無法涵蓋非刪除的結構變更，僅屬 UX 補強，不在本 change）。
