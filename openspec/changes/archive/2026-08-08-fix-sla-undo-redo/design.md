## Context

PhrozenOrca 的 undo/redo 系統以 `UndoRedo::Stack`（`src/slic3r/Utils/UndoRedo.hpp/.cpp`）為核心，透過 cereal binary archive 序列化以下四個物件：`Model`（含 ModelObject 全樹）、`Selection`、`GLGizmosManager`（當前 gizmo + 其狀態）、`PartPlateList`。FDM 與 SLA 共用同一個 Stack，差異僅在於各 gizmo 的 `on_save`/`on_load` 實作品質。

**現況問題：**

1. **GLGizmoHollow::on_save/on_load（`GLGizmoHollow.cpp:525-540`）**
   - `on_save` 寫入四個固定 dummy 值（`float 0.f`, `float 0.f`, `vector<bool>{}`, `bool true`）
   - `on_load` 讀取後全部丟棄，`m_pending_offset/quality/closing_d` 從未被還原
   - 歷史原因：這是從舊版 hole-editor 保留的 stream-position 佔位符，已失去實際意義
   - 現行 `on_save` 與 `on_load` 的欄位順序（4 個欄位）必須維持，以免 cereal stream 錯位

2. **GLGizmoDrill slider 無 snapshot（`GLGizmoDrill.cpp:607-658`）**
   - Diameter/Depth slider 每幀直接寫入 `mo->sla_drain_holes[idx]`，沒有 `TakeSnapshot`
   - InputFloat (`IsItemDeactivatedAfterEdit`) 也沒有 snapshot
   - Add/Delete/Move 洞已有正確 snapshot（分別在 233, 343, 841 行），只有大小修改遺漏

**既有正確模式（參考基準）：**
- `GLGizmoBrimEars::begin_radius_change` / `apply_radius_change`（`GLGizmoBrimEars.cpp:593-625`）：slider clicked → 儲存舊值；drag 期間 → 即時套用；`deactivated_after_edit` → restore old → TakeSnapshot → re-apply new
- `GLGizmoSlaSupports::on_save/on_load`（`GLGizmoSlaSupports.cpp:1443-1463`）：正確序列化 `m_normal_cache`、`m_editing_cache`、UI 參數

## Goals / Non-Goals

**Goals:**
- GLGizmoHollow 的 `on_save/on_load` 正確序列化 pending 參數，undo/redo 後 gizmo UI 狀態一致
- GLGizmoDrill 的 diameter/depth 操作納入 undo/redo，行為與 add/delete/move 洞一致
- 保持 cereal stream 格式相容（欄位數不變，僅替換假值為真值）
- 不引入新 crash 路徑

**Non-Goals:**
- 不修改 UndoRedo::Stack 核心
- 不為 GLGizmoHollow 加入 `wants_enter_leave_snapshots`（其操作模式不適合 sub-stack）
- 不修改 GLGizmoSlaSupports（已正確實作）
- 不修改 FDM gizmo 任何邏輯

## Decisions

### 決策 1：GLGizmoHollow 繼續使用 inline snapshot，不加 wants_enter_leave_snapshots

**選擇**：維持現有的 `TakeSnapshot(plater, "Hollow")` in-place 模式（`GLGizmoHollow.cpp:429`）。

**理由**：Hollow gizmo 的操作模式是「調整 slider → 按 Hollow 按鈕 commit」，每次 commit 是獨立的使用者意圖，應各自形成一個 undo 步驟。若加入 `wants_enter_leave_snapshots`，多次按 Hollow 會被 `reduce_noisy_snapshots()` 合併為一個步驟，違反使用者預期。GLGizmoDrill 也採用相同的 inline 模式，兩者一致。

**替代方案**：加入 `wants_enter_leave_snapshots = true` + GizmoAction type。
**排除理由**：Hollow 的 commit 操作次數少（通常 1-2 次），合併 undo 步驟對使用者無益且容易造成困惑。

---

### 決策 2：GLGizmoDrill 採用 BrimEars 的 begin/apply 三步驟模式 — ⚠️ 已作廢（2026-08-08 補述）

**現況**：本決策當初有落地（commit `2da76fa12`，2026-05-07），但 6 天後即被獨立 change 的 commit `0f302f003`（`fix: apply-only undo boundary for Drill holes`，2026-05-13，對應已 archive 的 `drill-apply-only-undo`）整個取代為 `m_working_holes` pending-apply 模型。目前程式碼中已不存在 `begin_size_change`/`apply_size_change`，本決策僅保留作歷史紀錄，不代表現行架構。Drill 的 undo/redo 現況與後續演進見 `drill-apply-only-undo`（已 archive）與 `resin-mode-scoped-undo-redo`（進行中，`phrozen-resin-dev-scoped-undo-redo` 分支）。

以下為原始決策內容（保留供歷史對照）：

**選擇**：新增 `begin_size_change(old_radius, old_height)` 和 `apply_size_change(snapshot_name)` 方法，追蹤 `m_holes_before_change`（整批洞的舊值）。

**理由**：
- Slider 拖曳期間每幀都寫入 ModelObject，必須在 commit 時能還原「拖曳前」的完整狀態
- BrimEars 已驗證此模式在相同 codebase 可靠運作
- `m_holes_before_change` 儲存所有洞（非只有選取中的），確保還原時不遺漏邊界情況

**替代方案**：只在 slider start 時 TakeSnapshot（捕捉拖曳前狀態）。
**排除理由**：TakeSnapshot 會立即呼叫 `suppress_snapshots()`，導致拖曳期間的每幀更新不能取消，且 snapshot 時機在 drag 開始而非結束，undo stack 中間狀態會充斥冗餘 snapshot。

---

### 決策 3：GLGizmoHollow on_save/on_load 維持 4 個欄位

**選擇**：用真實值替換假值，欄位順序與數量不變（`float offset`, `float quality`, `float closing_d`, `bool enable_hollowing`），對應原本的 `(float, float, vector<bool>, bool)`。

**理由**：cereal BinaryArchive 是無標籤格式，讀取端必須和寫入端欄位數一致。若改變欄位數，舊版本儲存的 snapshot 在 undo 時會 stream 錯位，導致後續所有欄位錯誤反序列化（潛在 crash）。

**注意**：`m_pending_owner` 不序列化（見風險 1）。

---

### 決策 4：m_pending_owner 在 on_load 中重置為 nullptr

**選擇**：`on_load` 結束時執行 `m_pending_owner = nullptr`，強制下次 `data_changed()` 從 config 重新初始化。

**理由**：undo 後 ModelObject 會被新物件取代，舊指標在語義上已失效。設為 nullptr 讓 `data_changed()` 必定走「新物件」路徑（`mo != m_pending_owner` 永遠成立），從還原後的 config 讀取值。這比序列化 ObjectID 改動更小且同樣安全。

## Risks / Trade-offs

**[Risk 1] m_pending_owner 指標比較的 C++ UB**
→ `data_changed()` 中的 `mo != m_pending_owner` 比較已失效指標的值（dangling pointer）在 C++ 標準上是 UB。實務上因為只做整數位址比較不解引用，在主流編譯器上安全。
→ Mitigation：本次以 `on_load` 重置 nullptr 解決。長期可改為 ObjectID 比較（`mo->id() != m_pending_owner_id`），但不在本次範圍。

**[Risk 2] cereal stream 相容性**
→ GLGizmoHollow on_save 由假值改為真值，欄位數和型別維持相同，不影響 stream 格式。
→ Mitigation：改動前後 on_save/on_load 欄位順序需嚴格對齊，靜態驗證：on_save 和 on_load 欄位列表應完全對應。

**[Risk 3] m_holes_before_change 在大型模型的記憶體消耗**
→ DrainHoles 是簡單的 struct（pos: Vec3f, normal: Vec3f, radius: float, height: float ≈ 32 bytes/洞），即使 1000 個洞也只有 32KB，可忽略。
→ Mitigation：不需要特別處理。

## Migration Plan

本次改動純屬 additive bug fix，無需 migration：
- 不改變 project file 格式（sla_drain_holes 序列化不變）
- cereal snapshot 格式維持相容（Hollow on_save 欄位數不變）
- 無 API 變動

Rollback：直接 revert 相關 cpp/hpp 改動。

## Testing Strategy

### 測試層級與 TDD 可行性

本次修改採用單層自動化測試（Layer 1），Layer 2 gizmo 序列化合約測試因效益不足而捨棄。

**Layer 1 — 資料序列化（已實作，作為永久回歸基線）**

測試 `sla::DrainHoles` 的 cereal round-trip，確保 undo/redo 所捕捉的 drain hole 資料不會退化。

測試位置：`tests/libslic3r/test_sla_undo_redo_data.cpp`
連結目標：`libslic3r`（無 GUI 依賴）
涵蓋欄位：`sla::DrainHole`（pos, normal, radius, height, failed）

**Layer 2 捨棄原因：**

原計畫以 stub struct + cereal round-trip 測試 `GLGizmoHollow`/`GLGizmoDrill` 的 `on_save/on_load` 欄位格式。但 stub struct 本身就是我們自己寫的，測試永遠 pass——它測的是 stub，不是 gizmo 的實際程式碼。要直接測 gizmo 程式碼需要 `GLGizmosManager` 實例，而後者依賴 `GLCanvas3D → wxWidgets → OpenGL`，headless 環境無法執行。兩個限制加總後效益不足，捨棄。

`on_save/on_load` 的正確性改由靜態 code review（task 3.4）和手動行為驗證（task 9）確保。

### 建議的開發順序

```
Step 1  Layer 1 測試已完成，作為回歸基線 ✅
Step 2  實作 GLGizmoHollow on_save/on_load
Step 3  實作 GLGizmoDrill begin/apply_size_change
Step 4  ctest 重跑 Layer 1 確認無退化
Step 5  手動驗證行為（見 tasks.md 第 8 節）
```

## Open Questions

（無待解決問題，架構決策已在上方確定）
