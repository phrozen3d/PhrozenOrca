## 1. GLGizmoDrill.hpp — 新增成員變數

- [x] 1.1 在 `GLGizmoDrill.hpp` 的 `private` 區塊新增 `sla::DrainHoles m_working_holes`（型別與 `sla_drain_holes` 相同，初始為空）

## 2. Session Lifecycle — on_set_state 與 data_changed

- [x] 2.1 `on_set_state(Off)`：移除 `m_stash_initialized` 條件判斷與 `mo_restore->sla_drain_holes = m_holes_stash` 的 restore 寫回邏輯；改為 `m_working_holes.clear()`
- [x] 2.2 `on_set_state(Off)`：保留 `m_old_mo_id = ObjectID{}` 的清除（維持物件切換偵測正確性）與 force update / instances_hider 呼叫不變
- [x] 2.3 `data_changed()` 的 new object 分支（`m_old_mo_id != mo->id()`）：移除 `old_obj->sla_drain_holes = m_holes_stash` 的舊物件 restore（舊物件的 `sla_drain_holes` 已永遠是 applied 狀態，不需還原）
- [x] 2.4 `data_changed()` 的 new object 分支：將 `m_holes_stash = mo->sla_drain_holes; m_stash_initialized = true;` 改為 `m_working_holes = mo->sla_drain_holes;`，以新物件的 applied 孔初始化 working set
- [x] 2.5 `data_changed()` 的 `is_serializing` 分支（Undo/Redo callback）：將 `m_holes_stash = mo->sla_drain_holes;` 改為 `m_working_holes = mo->sla_drain_holes;`，以 cereal 已還原的 applied 孔重建 working set；確保後續仍執行 `reload_cache()` 和 raycaster unreg

## 3. 編輯操作重導向 — 寫入 m_working_holes

- [x] 3.1 `gizmo_event()` 新增孔路徑（LeftDown + selection empty）：移除 `Plater::TakeSnapshot`；將 `mo->sla_drain_holes.emplace_back(...)` 改為 `m_working_holes.emplace_back(...)`；確保後續的 `m_selected.push_back`、`unregister_hole_raycasters_for_picking()`、`register_hole_raycasters_for_picking()` 仍執行
- [x] 3.2 `gizmo_event()` 選取矩形路徑（LeftUp / ShiftUp / AltUp + is_dragging）：將迴圈中 `mo->sla_drain_holes[i].pos` 改為 `m_working_holes[i].pos` 以計算 selection points
- [x] 3.3 `delete_selected_points()`：移除 `Plater::TakeSnapshot`；將 `drain_holes`（`m_c->selection_info()->model_object()->sla_drain_holes`）的引用改為 `m_working_holes` 的引用；erase 邏輯不變
- [x] 3.4 `on_start_dragging()`：將 `m_hole_before_drag` 和 `m_hole_normal_before_drag` 的來源從 `mo->sla_drain_holes[m_hover_id]` 改為 `m_working_holes[m_hover_id]`
- [x] 3.5 `on_dragging()`：將 `drain_holes[m_hover_id].pos` 和 `drain_holes[m_hover_id].normal` 的寫入目標從 `mo->sla_drain_holes` 改為 `m_working_holes`
- [x] 3.6 `on_stop_dragging()`：將 `drain_holes` 引用從 `mo->sla_drain_holes` 改為 `m_working_holes`；移除 `Plater::TakeSnapshot`；最終 pos/normal 由 on_dragging() 已寫入 m_working_holes，on_stop_dragging 只清 backup 變數
- [x] 3.7 `begin_size_change()`：將 `m_holes_before_change = m_c->selection_info()->model_object()->sla_drain_holes` 改為 `m_holes_before_change = m_working_holes`（複製 working set 作為 drag 開始前的快照）
- [x] 3.8 `apply_size_change()`：移除 `Plater::TakeSnapshot` 及其前後的「還原 → snapshot → 重套」三步驟；m_working_holes 已有 slider per-frame 寫入的最終值；函式僅負責 set_as_dirty + 清除 before_change 快照；slider/InputFloat 的每幀寫入路徑也同步改為 `m_working_holes[idx].radius/height`
- [x] 3.9 `select_point()`：將 `m_c->selection_info()->model_object()->sla_drain_holes` 的引用改為 `m_working_holes`（size 檢查、radius / height 讀取）
- [x] 3.10 `reload_cache()`：將 `m_c->selection_info()->model_object()->sla_drain_holes.size()` 改為 `m_working_holes.size()`
- [x] 3.11 `on_set_hover_id()`：將 `sla_drain_holes.size()` 的 hover id clamp 改為 `m_working_holes.size()`

## 4. Render 與 Raycaster 路徑重導向

- [x] 4.1 `render_points()`：將 `const sla::DrainHoles& drain_holes = m_c->selection_info()->model_object()->sla_drain_holes;` 改為 `const sla::DrainHoles& drain_holes = m_working_holes;`；其餘渲染邏輯不變
- [x] 4.2 `register_hole_raycasters_for_picking()`：孔資料來源已改用 `Selection` 取得 ModelObject（舊 crash fix），但孔數量與資料仍讀 `mo->sla_drain_holes`；改為讀取 `m_working_holes`（ModelObject 用於確認物件有效性，孔資料改用 working set）
  > Remove all guard 也同步改為 `!m_working_holes.empty()`
- [x] 4.3 `update_hole_raycasters_for_picking_transform()`：將 `const sla::DrainHoles& drain_holes = info->model_object()->sla_drain_holes;` 改為 `const sla::DrainHoles& drain_holes = m_working_holes;`；size guard 和 null guard 維持不變

## 5. Apply 按鈕 Handler

- [x] 5.1 `render_new_drill_panel()` 中的 Apply 按鈕 handler：在現有的 `reslice_until_step(slaposDrillHoles)` 之前，加入 `Plater::TakeSnapshot(wxGetApp().plater(), "Apply drain holes")`
  > 順序要求：TakeSnapshot 必須在 assign 之前，才能捕捉 Apply 前的 sla_drain_holes
- [x] 5.2 Apply 按鈕 handler：在 TakeSnapshot 之後加入 `mo->sla_drain_holes = m_working_holes;`（將 pending working set 寫入正式資料）
- [x] 5.3 Apply 按鈕 handler：移除 `m_holes_stash = mo->sla_drain_holes;`（stash 的 apply-baseline 角色已廢棄）
- [x] 5.4 Apply 按鈕 handler：確認 reslice 呼叫維持在 assign 之後（slicing 使用已更新的 sla_drain_holes）

## 6. m_holes_stash 清理

- [x] 6.1 確認 `m_stash_initialized` 是否仍有使用點；若 session lifecycle 已全部改用 `m_working_holes`，則從 `GLGizmoDrill.hpp` 和 `.cpp` 移除 `m_stash_initialized` 成員與所有使用處
  > cpp 無任何引用；hpp 宣告已移除。
- [x] 6.2 確認 `m_holes_stash` 是否仍有使用點；若 exit-restore 和 apply-baseline 責任已全部移除，則從 `GLGizmoDrill.hpp` 和 `.cpp` 移除 `m_holes_stash` 成員與所有使用處
  > cpp 無任何引用（Apply handler 在 Phase 5 已移除最後一個賦值）；hpp 宣告已移除。m_holes_before_change comment 同步更新（移除「restore before TakeSnapshot」舊說明）。

## 7. on_save / on_load 確認

- [x] 7.1 靜態確認：`on_save` / `on_load` 的序列化欄位（`m_new_hole_radius`、`m_new_hole_height`、`m_selected`、`m_selection_empty`）不含 `m_working_holes`，無需調整
- [x] 7.2 靜態確認：`data_changed(is_serializing=true)` 在 `on_load()` 之後執行，確保 `m_working_holes` 由 `sla_drain_holes` 重建的時機正確（不需在 `on_load` 中初始化 `m_working_holes`）
  > 確認完成：`StackImpl::load_snapshot()` L1011 先 restore Model（含 sla_drain_holes），L1016 才 load GLGizmosManager（含 on_load）；`data_changed(is_serializing=true)` 在後續 `update_after_undo_redo()` → `update_data()` 中才被呼叫，順序正確。

## 9. Undo/Redo 假切 Preview 同步（m_last_resliced_holes）

- [x] 9.1 `GLGizmoDrill.hpp` 新增成員：`sla::DrainHoles m_last_resliced_holes`（代表最後一次排程 DrillHoles reslice 時的 applied holes）
- [x] 9.2 `data_changed()` new object 分支：在 `m_working_holes = mo->sla_drain_holes` 後，同步 `m_last_resliced_holes = mo->sla_drain_holes`
- [x] 9.3 Apply 按鈕 handler：reslice 呼叫後加 `m_last_resliced_holes = mo->sla_drain_holes`（= m_working_holes）
- [x] 9.4 `data_changed()` is_serializing 分支：在現有 `m_working_holes = mo->sla_drain_holes` / `reload_cache()` / raycaster unreg 邏輯後，以 `mo->sla_drain_holes != m_last_resliced_holes` 條件判斷是否呼叫 `reslice_until_step(slaposDrillHoles, true)` 並更新 baseline
- [x] 9.5 `on_set_state(Off)`：Exit 時加 `m_last_resliced_holes.clear()`（與 `m_working_holes.clear()` 並列）

## 10. Ctrl+Z pending discard 攔截

- [x] 10.1 `GLGizmoDrill.hpp` 新增公開方法宣告：`bool has_pending_changes() const` 與 `bool discard_pending_changes()`
- [x] 10.2 實作 `has_pending_changes()`：null-guard + 回傳 `m_working_holes != mo->sla_drain_holes`
- [x] 10.3 實作 `discard_pending_changes()`：`m_working_holes = sla_drain_holes` + `reload_cache()` + raycaster unreg/re-reg + `set_as_dirty()`；若無 pending 則回傳 false（不 discard）
- [x] 10.4 `GLGizmosManager::on_char()`：在 Ctrl+Z case（`WXK_CONTROL_Z` / `'z'` / `'Z'`）加入 Drill-specific 攔截：`m_current == Drill && drill->discard_pending_changes()` 成功時設 `processed = true`；不攔截 Ctrl+Y

## 8. 手動驗證 — 測試矩陣

- [x] 8.1 **Add × 3，不 Apply，Exit**：進入 Drill → 新增 3 孔 → 直接關閉 Drill gizmo → 確認 `sla_drain_holes` 為 0 孔（進入前狀態），切片結果無孔
- [x] 8.2 **Add × 3，Apply，切片**：新增 3 孔 → Apply → 切片匯出 → 確認切出 3 孔
- [x] 8.3 **Add × 3，Apply，Undo，切片**：Apply 後 Ctrl+Z → 確認 `sla_drain_holes` 回到 Apply 前（0 孔）→ 切片 → 確認無孔
- [x] 8.4 **Add × 3，Apply，Undo，Redo，切片**：Undo 後 Ctrl+Y → 確認 `sla_drain_holes` 回到 3 孔 → 切片 → 確認 3 孔
- [x] 8.5 **Add × 3，Apply，再 Add × 1，不 Apply，Exit**：Apply 3 孔後再加第 4 孔但不 Apply → Exit → 確認 `sla_drain_holes` = 3 孔，第 4 孔未保留
- [x] 8.6 **Add × 3，Apply，再 Add × 2，Apply，Undo**：兩次 Apply 後 Ctrl+Z → 確認 `sla_drain_holes` 回到第一次 Apply 的 3 孔狀態
- [x] 8.7 **Delete pending 後不 Apply，Exit**：進入 Drill（已有 3 孔）→ 刪除 1 孔（pending，未 Apply）→ Exit → 確認 `sla_drain_holes` 仍為 3 孔
- [x] 8.8 **Move pending 後不 Apply，Exit**：拖曳孔位置（pending，未 Apply）→ Exit → 確認 `sla_drain_holes` 孔位置不變
- [x] 8.9 **Size 修改 pending 後不 Apply，Exit**：調整 Diameter/Depth（pending，未 Apply）→ Exit → 確認 `sla_drain_holes` 孔大小不變
- [x] 8.10 **Delete pending 後 Apply，Undo**：刪除孔 → Apply → Ctrl+Z → 確認 `sla_drain_holes` 回到刪孔前狀態（被刪孔恢復）
- [x] 8.11 **Move pending 後 Apply，Undo**：拖曳孔 → Apply → Ctrl+Z → 確認 `sla_drain_holes` 孔位置回到拖曳前
- [x] 8.12 **Size 修改 pending 後 Apply，Undo**：改孔大小 → Apply → Ctrl+Z → 確認 `sla_drain_holes` 孔大小回到修改前
- [x] 8.13 **Undo/Redo 後重新進入 Drill**：Undo 後打開 Drill gizmo → 確認 render 顯示與 `sla_drain_holes` 一致，`m_selected` 不越界（`m_selected.size() == m_working_holes.size()`）
- [x] 8.14 **切換物件後切回**：A 物件 pending 3 孔（未 Apply）→ 切換到 B 物件 → 切換回 A → 確認 A 物件的 `sla_drain_holes` 為 Apply 前狀態，B 物件的 `sla_drain_holes` 不受影響
- [x] 8.15 **多次 Apply 後 Undo 多次**：Apply 3 次（每次加孔）→ Undo 3 次 → 確認每次 Undo 正確回到對應的 Apply 前狀態，slicing 一致
- [x] 8.16 **Raycaster picking 正確性**：Add 孔到 `m_working_holes` 後，確認可以點選到新孔（raycaster 已更新）；Exit 後重新進入，確認 raycaster 以 `sla_drain_holes` 重建（applied 狀態的孔才可 pick）
- [x] 8.17 **Undo/Redo 後假切 preview 更新（m_last_resliced_holes）**：Apply 3 孔 → Ctrl+Z Undo → 確認 SLA preview mesh 更新為無孔狀態（DrillHoles reslice 有被觸發）；接著 Ctrl+Y Redo → 確認 preview 回到 3 孔。Undo 與 Redo 中間不得再次 Apply，因為新的 Apply 會清除 Redo stack。
- [x] 8.18 **非 drill Undo 不觸發多餘 reslice**：Apply 3 孔 → 用其他方式取得一個非 drill 的 Undo step（例如進入 Hollow 再回 Drill，或確認 m_last_resliced_holes == sla_drain_holes 後 Undo 無 reslice 觸發）
- [x] 8.19 **Ctrl+Z discard pending（有 pending 時）**：Add 2 孔但不 Apply → Ctrl+Z → 確認 working set 回到 0 孔（applied 狀態），global undo 未觸發（先前 undo stack 不變）
- [x] 8.20 **Ctrl+Z global undo（無 pending 時）**：Apply 3 孔後不做任何 pending 操作 → Ctrl+Z → 確認觸發 global undo，sla_drain_holes 回到 Apply 前狀態（0 孔）
- [x] 8.21 **Ctrl+Z 多次：pending → discard → global undo 序列**：Add 2 孔（pending）→ 第一次 Ctrl+Z（discard pending，holes 回 0）→ 第二次 Ctrl+Z（global undo，若 stack 有可 undo 的操作則執行）

## 11. Future Work（本 change 不實作）

- [ ] 11.1 **[FW] 非 Drill gizmo 下 Undo/Redo 後 DrillHoles preview 自動刷新**
  > **建議另開 change：`fix-drill-preview-refresh-after-undo-redo`**
  > 
  > **問題**：Undo/Redo 在 Drill gizmo 以外發生時，`data_changed(is_serializing=true)` 不會被呼叫，`m_last_resliced_holes` 比較邏輯不執行。`sla_drain_holes` 與正式 pipeline cache 皆正確（`SLAPrint::apply()` 已 invalidate `slaposDrillHoles`），但 PhrozenOrca on-demand 模式下不自動重啟 pipeline，3D 假切 preview mesh 可能暫時 stale。export / slicing 結果不受影響。
  >
  > **精確修正方案（snapshot flag）**：在 `SnapshotData::Flags` 新增 `RECALCULATE_SLA_DRILL_HOLES = 32`；Apply drain holes 的 `TakeSnapshot` 時嵌入此 flag；`GLGizmosManager::update_after_undo_redo()` 讀取 flag 後無條件呼叫 `reslice_until_step(slaposDrillHoles, true)`（無論 current gizmo）。需修改 `UndoRedo.hpp`、`Plater.cpp`（take_snapshot flag logic）、`GLGizmosManager.cpp`（update_after_undo_redo handler），超出本 change 範疇。
  >
  > **不採用粗略方案**：「每次 SLA Undo/Redo 都呼叫 DrillHoles reslice」有過度 reslice 風險（sla_drain_holes 未改變時也觸發 CGAL）。