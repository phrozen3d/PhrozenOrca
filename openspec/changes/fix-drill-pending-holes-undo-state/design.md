## Context

`GLGizmoDrill` 的 UI 語意是「按 Apply 才正式提交孔資料」，但目前程式碼是 incoherent hybrid：

**根本衝突**：

```
Fine-grained Undo（現行實作）           Apply-as-commit（UI 語意）
──────────────────────────────         ────────────────────────────
Add/Delete/Move/Size → snapshot        Apply = 唯一的提交邊界
Undo 可回到任意孔操作前的狀態         Exit without Apply → 還原到進入時狀態
sla_drain_holes = 當前操作狀態        Slicing 應只看已 Apply 的孔
↓                                      ↓
Undo 後 sla_drain_holes 剩 2 孔       使用者認為「未 Apply = pending」
slicer 仍切出這 2 孔                  但切片結果與 Apply 語意不符
```

**stash-sync 的副作用**：`data_changed(is_serializing=true)` 中執行 `m_holes_stash = mo->sla_drain_holes`，目的是防止 Undo → Exit 時 stash 覆蓋 Undo 結果（已解決的舊 bug）。但這個 sync 使 Apply 設定的 baseline 在 Undo 後立即被破壞，「Apply 語意」不復存在。

**修正架構（Design B）**：

```
Session 內（Gizmo 內部）               Slicing pipeline（ModelObject）
──────────────────────────             ────────────────────────────────
m_working_holes                        sla_drain_holes
= pending preview working set          = applied / committed state only
↑ Add/Delete/Move/Size 寫入這裡        ↑ 只有 Apply 按鈕才更新這裡
↑ render_points() 從這裡讀            ↑ Slicer 從這裡讀
↑ raycaster / drag / select 從這裡讀  ↑ Undo/Redo 由 cereal 還原這裡

Apply:  TakeSnapshot("Apply drain holes")
        → sla_drain_holes = m_working_holes
        → reslice

Undo:   cereal 還原 sla_drain_holes
        → data_changed(is_serializing) 重建 m_working_holes = sla_drain_holes
        → reload_cache() 同步 m_selected

Exit:   m_working_holes 丟棄；sla_drain_holes 保持不變（無需 restore）
Enter:  m_working_holes = sla_drain_holes（以正式孔初始化工作集）
```

參考先例：`GLGizmoSlaSupports` 的 `m_editing_cache` 是相同的「session working set」模式。

## Goals / Non-Goals

**Goals:**
- `sla_drain_holes` 永遠代表已 Apply 的正式孔資料，slicing pipeline 不需任何修改
- `m_working_holes` 是 session 內的 pending preview，Exit without Apply 時丟棄
- Apply 是唯一的 commit path：`TakeSnapshot` → `sla_drain_holes = m_working_holes` → reslice
- Undo/Redo 後，從 `sla_drain_holes`（cereal 已還原）重建 `m_working_holes`，selection cache 同步重設
- Object switch 時，丟棄當前 `m_working_holes`，以新物件的 `sla_drain_holes` 初始化
- 廢棄 `m_holes_stash` 的 exit-restore 責任

**Non-Goals:**
- 不修改 SLA slicing pipeline
- 不在 `ModelObject` 新增欄位
- 不修改 3MF / cereal 序列化格式
- 不提供 session 內的 fine-grained Undo（Add/Delete/Move/Size 不建立 snapshot）；Ctrl+Z 在 pending dirty 時只 discard pending，不還原個別孔操作
- 不修改 `GLGizmoHollow` / `GLGizmoSlaSupports`
- 不強制一次性完全刪除 `m_holes_stash`；但會移除其 exit-restore / Apply baseline 責任，並優先嘗試完全刪除（參見 Decision 5）

## Decisions

### 決策 1：新增 m_working_holes 作為 gizmo 成員

**選擇**：在 `GLGizmoDrill.hpp` 新增 `sla::DrainHoles m_working_holes`，型別與 `sla_drain_holes` 一致。

**理由**：型別相同使得現有 Add/Delete/Move/Size 邏輯只需將讀寫目標從 `sla_drain_holes` 改為 `m_working_holes`，改動量最小。`GLGizmoSlaSupports::m_editing_cache` 是同一模式的現成先例。

**排除的替代方案（Design A）**：讓 sla_drain_holes 繼續作為 pending，改由另一欄位存 applied 狀態。
**排除理由**：需修改 ModelObject 資料模型、slicing pipeline、3MF 格式，遠超出本 change 範圍。

---

### 決策 2：Apply 是唯一的 TakeSnapshot 點

**選擇**：移除 Add / Delete / Move / Size 各自的 `TakeSnapshot`；Apply 按鈕 handler 改為先 `TakeSnapshot("Apply drain holes")` 再寫入 `sla_drain_holes`。

**理由**：Undo stack 只有 Apply 邊界，語意與 UI 一致。Ctrl+Z 回到上一次 Apply 前的 `sla_drain_holes` 狀態（由 cereal 還原），不存在「Undo 到 pending 孔仍被切出」的問題。

**排除的替代方案**：保留個別孔操作的 snapshot，同時讓 slicing 只看 applied 孔（Design A 的變形）。
**排除理由**：需要 slicing pipeline 修改，超出範圍。

---

### 決策 3：data_changed(is_serializing=true) 重建 m_working_holes

**選擇**：Undo/Redo callback 中執行 `m_working_holes = mo->sla_drain_holes`（cereal 已還原），再執行 `reload_cache()` 和 raycaster unreg/re-reg。

**理由**：Undo/Redo 後 `sla_drain_holes` 已是正確的 applied 狀態，以它重建 `m_working_holes` 使得 session 的新編輯起點永遠與 applied 狀態一致，不會出現「stash 覆蓋 Undo 結果」的舊問題。

**排除的替代方案**：繼續使用 `m_holes_stash` 的 sync 機制。
**排除理由**：stash sync 是「Apply baseline 被 Undo 破壞」問題的根本原因。

---

### 決策 4：on_set_state(Off) 移除 restore 邏輯

**選擇**：移除 `m_stash_initialized` 檢查和 `mo_restore->sla_drain_holes = m_holes_stash` 的 restore。Exit 時只清空 `m_working_holes`，`sla_drain_holes` 保持不變。

**理由**：`sla_drain_holes` 已永遠是 applied 狀態，Exit 時無需 restore。原有的 restore 邏輯反而有將 pending 孔誤寫回 `sla_drain_holes` 的風險。

**排除的替代方案**：保留 restore 邏輯但加 is_dirty 條件。
**排除理由**：dirty flag 在 Undo/Redo 後同樣面臨同步問題（探索階段已評估）。

---

### 決策 5：m_holes_stash 的處理

**選擇（優先順序）**：

1. **優先完全刪除** `m_holes_stash` 與 `m_stash_initialized`。只要 object-switch path 可以直接以 `sla_drain_holes`（本身即 applied state）正確運作，就應完全移除這兩個成員。
2. **若刪除後有 object-switch path 或編譯錯誤需要保留**，則暫時保留 `m_holes_stash`，但必須在宣告處加上明確註解，說明：
   - 不再代表 Apply baseline
   - 不得用於 exit restore（`on_set_state(Off)` 中不得對 `sla_drain_holes` 寫回）
   - 僅作 object-switch 過渡期的臨時 buffer，未來應移除
3. `m_stash_initialized` 若 `m_holes_stash` 完全刪除，則一同移除；若暫時保留，仍需移除其 exit-restore 相關的條件判斷。

**理由**：保留空殼有混淆未來開發者的風險（可能誤將 exit-restore 邏輯加回）；優先刪除可讓程式碼語意與 Design B 架構完全一致。

---

### 決策 6：引入 m_last_resliced_holes baseline，Undo/Redo 後只在 applied holes 改變時才 reslice

**選擇**：新增 `sla::DrainHoles m_last_resliced_holes` 成員，代表最後一次成功排程 `reslice_until_step(slaposDrillHoles)` 時的 `sla_drain_holes` 內容。在 `data_changed(is_serializing=true)` 中，以 `mo->sla_drain_holes != m_last_resliced_holes` 做條件判斷；成立才觸發 reslice 並同步 baseline。

**更新時機**：
- Enter Drill / new object：`m_last_resliced_holes = mo->sla_drain_holes`
- Apply 按鈕成功：`m_last_resliced_holes = mo->sla_drain_holes`（= m_working_holes）
- Undo/Redo 後 applied holes 改變：`m_last_resliced_holes = mo->sla_drain_holes`（觸發 reslice 後）
- Exit Drill：`m_last_resliced_holes.clear()`

**理由**：`reslice_until_step(slaposDrillHoles)` 雖不重算 Hollowing（hollow interior 有 cache），但仍需執行 O(N) CGAL boolean 操作。多孔物件（20+ 孔）每次 Undo/Redo 都觸發 CGAL 有明顯效能代價。使用 baseline 比較可將 reslice 精確限定在「applied holes 因 Undo/Redo 真的改變」的情境。

**排除的替代方案**：`SlaSupports-style snapshot flag`（`RECALCULATE_SLA_DRILL_HOLES` in `SnapshotData::Flags`）。
**排除理由**：需修改 `UndoRedo.hpp`、`Plater.cpp`、`GLGizmosManager.cpp` 三個檔案；而 `m_last_resliced_holes` 方案完全自包含於 `GLGizmoDrill`，效果相近，改動範圍更小。

---

### 決策 7：pending dirty 時 Ctrl+Z 在 Drill gizmo 層先 discard pending changes

**選擇**：
- `GLGizmoDrill` 公開 `has_pending_changes()` 與 `discard_pending_changes()` 兩個方法
- `GLGizmosManager::on_char()` 在 `m_current == Drill` 且 Ctrl+Z 時：若 `discard_pending_changes()` 回傳 true（代表有 pending 且已 discard）→ 回傳 `processed = true`（攔截，不觸發全域 Undo）
- 若 `discard_pending_changes()` 回傳 false（無 pending）→ 不攔截，全域 Undo 正常執行

**理由**：Design B 的 session 內無 fine-grained Undo，使用者在有 pending 孔的狀態下按 Ctrl+Z，直覺上應先丟棄 pending，而非直接跳過所有 pending 孔去 Undo 更前面的全域操作（可能導致物件意外消失等令人驚嚇的結果）。此方案影響最小：只在 Drill active 且 has_pending 時才攔截。

**非目標**：
- 不實作 Drill session-level local undo stack（可回到每個孔操作前的狀態）
- 不攔截 Ctrl+Y（Redo 不受影響）
- Discard 後無 Redo（「丟棄草稿」不是可逆操作，語意上正確）

**排除的替代方案**：Drill local undo stack（選項 B）。
**排除理由**：需要獨立 session stack 的序列化與 Redo 支援，改動量顯著，應另開 change。

## Risks / Trade-offs

**[Risk 1] Session 內無 Undo 的 UX 變化**
→ 使用者在 Apply 前無法 Ctrl+Z 取消個別孔操作（如誤加一孔）。
→ Mitigation：Apply 是輕量操作（假切，不做完整 reslice），使用者可以頻繁 Apply；UI tooltip 或 panel 說明 Apply 後才可 Undo。未來可選擇性追加 session-level local undo stack（本 change 範圍外）。

**[Risk 2] sla_drain_holes 讀取點未完全替換**
→ `render_points()`、`update_hole_raycasters_for_picking_transform()`、`on_dragging()`、`on_stop_dragging()`、`delete_selected_points()`、`apply_size_change()` 等多處讀寫 `sla_drain_holes`，若有遺漏則視覺與 slicing 狀態不一致。
→ Mitigation：tasks 逐函式列出所有需改寫的位置，手動驗證測試矩陣中的 render / drag / delete / size 路徑。

**[Risk 3] m_selected 大小與 m_working_holes 不同步**
→ Undo/Redo 後若 `m_selected.size()` 超過 `m_working_holes.size()`，存取會越界。
→ Mitigation：data_changed(is_serializing) 重建 m_working_holes 後立即 `reload_cache()`（已在舊版本中做過，維持此習慣）。

**[Risk 4] Apply TakeSnapshot 順序錯誤**
→ 若 assign `sla_drain_holes = m_working_holes` 後才 TakeSnapshot，snapshot 會捕捉到已更新的狀態，Undo 無法回到 Apply 前。
→ Mitigation：tasks 明確要求順序為「TakeSnapshot → assign → reslice」，TakeSnapshot 捕捉 Apply 前的 sla_drain_holes。

**[Risk 5] m_holes_stash 殘留程式碼引起混淆**
→ 若 m_holes_stash 保留但職責不明，未來開發者可能誤用。
→ Mitigation：加上明確註解說明其職責已移除，或在實作階段完全刪除。

**[Risk 6] Undo/Redo 後 DrillHoles reslice 過度觸發**
→ 若在 `data_changed(is_serializing=true)` 無條件呼叫 `reslice_until_step(slaposDrillHoles)`，任何 Undo/Redo 操作（含與 drill holes 無關者）都會重跑 CGAL boolean，對多孔 hollow 物件有效能衝擊。
→ Mitigation（決策 6）：引入 `m_last_resliced_holes` baseline，只在 `sla_drain_holes != m_last_resliced_holes` 時才觸發 reslice，避免對同一孔資料的重複 reslice。

**[Risk 7] Ctrl+Z 在無 pending changes 時被誤攔截**
→ 若 `has_pending_changes()` 判斷有誤，全域 Undo 可能被阻斷。
→ Mitigation（決策 7）：`has_pending_changes()` 使用精確的 vector 比較（`m_working_holes != mo->sla_drain_holes`）；`discard_pending_changes()` 回傳 bool，只有真正執行 discard 才回傳 true，GLGizmosManager 再決定是否攔截。

## Migration Plan

- 修改檔案：`src/slic3r/GUI/Gizmos/GLGizmoDrill.hpp` / `.cpp`、`src/slic3r/GUI/Gizmos/GLGizmosManager.cpp`（Ctrl+Z 攔截）
- Project 檔案相容性：`sla_drain_holes` 的 cereal 格式不變，現有 `.orca` / `.3mf` 檔案不受影響
- Rollback：revert 上述三個檔案即可，無 DB migration、無協定版本變更

## Open Questions

**[實作前需確認] `on_load()` 與 `data_changed(is_serializing=true)` 的呼叫順序**

需靜態確認：Undo/Redo 觸發時，cereal 還原 `sla_drain_holes` 的時機是否早於 `data_changed(is_serializing=true)` 被呼叫。

- 若順序正確（cereal restore → `data_changed`），則 `data_changed` 中的 `m_working_holes = mo->sla_drain_holes` 可讀到已還原的正確資料。
- 若順序相反（`data_changed` 先於 cereal restore），則 `m_working_holes` 會被初始化為舊資料，導致重建失敗。

確認來源：`GLGizmosManager::load_state()` → `on_load()` → `update_after_undo_redo()` → `update_data()` → `data_changed(is_serializing=true)` 的呼叫鏈。現有程式碼行為（舊版的 stash sync 可正確工作）暗示順序應正確，但實作任務 7.2 要求靜態確認，確認後此問題關閉。

## Known Limitations / Future Work

### [KL-1] 非 Drill gizmo 下 Undo/Redo 後假切 preview 可能暫時 stale

**現象**：使用者在 Drill gizmo 以外的狀態（例如 Move、Hollow、或無 gizmo）執行 Undo/Redo，跳回一個包含不同 `sla_drain_holes` 的 snapshot 時，正式資料（`sla_drain_holes`）已由 cereal 正確還原，`SLAPrint::apply()` 也已正確將 `slaposDrillHoles` 標為 invalid（SLAPrint.cpp:560-563），但 **PhrozenOrca on-demand 模式下（`background_processing_enabled()` = false），SLA pipeline 不會自動重啟**，導致 3D 假切 preview mesh 暫時停留在舊狀態。

**影響**：
- ✅ 正式 `sla_drain_holes` 資料：正確（cereal 還原正確）
- ✅ 實際 slicing / export 結果：正確（pipeline cache 已 invalidated，export 時從頭計算）
- ⚠️ 3D preview mesh：可能暫時 stale，直到下列任一事件發生：
  - 使用者重新進入 Drill gizmo（`data_changed` + `m_last_resliced_holes` 機制觸發 reslice）
  - 手動按 Slice 按鈕
  - Apply 操作觸發 `reslice_until_step(slaposDrillHoles)`

**後續精確修正方向（另開 change `fix-drill-preview-refresh-after-undo-redo`）**：
在 Apply drain holes 的 TakeSnapshot 時嵌入 `SnapshotData::RECALCULATE_SLA_DRILL_HOLES` 旗標（類似 SlaSupports 的 `RECALCULATE_SLA_SUPPORTS`）。`GLGizmosManager::update_after_undo_redo()` 讀取旗標，無論 current gizmo 是否為 Drill，都呼叫 `reslice_until_step(slaposDrillHoles, true)`（僅重算 DrillHoles step，不觸及 Hollowing cache，代價為 O(N) CGAL 孔操作）。需修改 `UndoRedo.hpp`、`Plater.cpp`（take_snapshot）、`GLGizmosManager.cpp`（update_after_undo_redo），超出本 change Drill pending-apply 的範疇，故暫不實作。