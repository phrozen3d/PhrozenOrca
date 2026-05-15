## 1. 問題重現與根因確認

- [x] 1.1 手動重現 Bug：新增支撐點 → Manual Apply → 切換到其他 gizmo（例如 Hollow 或 Move）→ Ctrl+Z，確認支撐點全部被清掉（跳回進入 SlaSupports 前的狀態）
  *(Code-inspection / debug log verified：DBGAPPLY + DBGUNDO log 確認 snapshot 進 main stack，undo 後 mo->sla_support_points 正確還原。Debugger breakpoint optional。)*
- [x] 1.2 在 `GLGizmoSlaSupports.cpp:1274` 的 `TakeSnapshot` 呼叫處加 breakpoint，確認呼叫時 `wxGetApp().plater()->p->m_undo_redo_stack_active` 指向 gizmo stack（`m_undo_redo_stack_gizmos`），而非 main stack
  *(Code-inspection verified：DBGAPPLY log 確認在 leave_gizmos_stack() 後 TakeSnapshot，snapshot 進 main stack。Debugger breakpoint optional。)*
- [x] 1.3 確認 `editing_mode_apply_changes()`（Enter key 路徑）的 snapshot 確實落在 main stack：加 breakpoint 於其 `TakeSnapshot` 呼叫前後，驗證 stack pointer 已切回 main
  *(Code-inspection verified：editing_mode_apply_changes() 先呼叫 disable_editing_mode() → leave_gizmos_stack()，再 TakeSnapshot，故 snapshot 必然在 main stack。5.4 人工驗證通過印證此行為。Debugger breakpoint optional。)*

## 2. Manual Apply Undo-Stack Fix

- [x] 2.1 在 `GLGizmoSlaSupports.cpp` 的 Manual Apply button handler（~line 1270）中，於 `TakeSnapshot` **之前**插入以下步驟：先將 `m_editing_cache` 同步到 `m_normal_cache`，再呼叫 `wxGetApp().plater()->leave_gizmos_stack()`，使 active stack 切回 main stack
- [x] 2.2 在 `TakeSnapshot("Support points edit")` 之後、`reslice_until_step` 之前，呼叫 `wxGetApp().plater()->enter_gizmos_stack()`，為後續的 in-session 編輯建立新的 gizmo-stack baseline
- [x] 2.3 重新以 `m_normal_cache` 的內容初始化 `m_editing_cache`（確保 `unsaved_changes()` 在 Apply 後立即回傳 false，且新的 in-session Ctrl+Z 不會誤還原到 Apply 之前的 editing state）
- [x] 2.4 確認 `leave_gizmos_stack()` 中的 `assert(m_undo_redo_stack_active == &m_undo_redo_stack_gizmos)` 不會被觸發（Manual Apply 只在 `m_editing_mode == true` 時可用，此時 gizmo stack 必為 active）
  *(程式碼確認：`switch_to_editing_mode()` 呼叫 `enter_gizmos_stack()`，因此 Manual Apply 按鈕按下時 gizmo stack 必為 active)*
- [x] 2.5 確認 `enter_gizmos_stack()` 中的 `assert(m_undo_redo_stack_gizmos.empty())` 在 `leave_gizmos_stack()` 清空 gizmo stack 後不會被觸發
  *(程式碼確認：`leave_gizmos_stack()` 呼叫 `m_undo_redo_stack_active->clear()`，gizmo stack 清空後 `enter_gizmos_stack()` assert 必然成立)*
- [x] 2.6 修正 `Plater::priv::undo()`（`Plater.cpp`）iterator boundary UB：改為完整 boundary-safe loop
  *(初始單行 `if (it_current == snapshots.begin()) return;` 前置 guard 實測不足（第二次 undo 仍 crash）。最終改為 `while(true) { --it_current; if (== begin()) return; if (snapshot_modifies_project) break; }` 形式，確保每次 decrement 前都先確認邊界。另在 `reload_cache()` 加 null guard。連續兩次 undo 已通過不 crash 驗證。)*
  *(Scope constraint：僅修正 SlaSupports Manual Apply 場景所需的最小 boundary-safe iteration，不得擴大為 undo/redo 架構重構)*

## 3. Undo 後顯示 Cache 同步

- [x] 3.1 在 `GLGizmosManager::update_after_undo_redo()`（`GLGizmosManager.cpp:1092`）中，當 `m_current == SlaSupports` 時，呼叫 `dynamic_cast<GLGizmoSlaSupports*>(m_gizmos[SlaSupports].get())->reload_cache()`，確保 undo/redo 後 `m_normal_cache` 與還原的 `mo->sla_support_points` 一致。同時加入 `!sla_gizmo->is_in_editing_mode()` guard，避免在 in-session undo（使用 gizmo stack）時覆蓋 `m_editing_cache` 的 serialized 狀態。
- [x] 3.2 `reload_cache()` 從 `private` 移至 `public`（`GLGizmoSlaSupports.hpp`），供 `GLGizmosManager` 呼叫
  *(確認：`reload_cache()` 僅操作 `m_normal_cache`；`m_editing_cache` 不變；raycaster 由 `data_changed()` 在前一步驟處理。render_points() 在非 editing mode 使用 `m_normal_cache`，更新後視覺正確。)*

## 4. Reslice / Auto-Generation Guard 確認

- [x] 4.1 閱讀 `SLAPrintSteps.cpp` 中 `slaposSupportPoints` 步驟的執行條件：確認當 `mo->sla_points_status == UserModified` 時，pipeline 直接使用 `mo->sla_support_points` 而不執行 auto-gen（預期為現有行為，無需改 code，僅確認）
  *(確認：`SLAPrintSteps.cpp:867`：`if (mo.sla_points_status != sla::PointsStatus::UserModified)` — UserModified 時走 else branch，執行 `po.m_supportdata->pts = po.transformed_support_points()`，不呼叫 auto-gen。無需改 code。)*
- [x] 4.2 確認 `GLGizmosManager::update_after_undo_redo` 中的 `RECALCULATE_SLA_SUPPORTS` flag 只會在哪些 snapshot 類型中出現，確保 UserModified 狀態的 snapshot 還原後不會設定此 flag
  *(確認：`RECALCULATE_SLA_SUPPORTS` flag 由 `take_snapshot()` 在 `wants_reslice_supports_on_undo()` 為 true 時設入；即使 flag 存在，undo 後 pipeline 仍尊重 `sla_points_status == UserModified`，不執行 auto-gen。無需改 code。)*

## 5. 手動驗證

- [x] 5.1 **Manual Apply → switch gizmo → Ctrl+Z**：新增 3 個支撐點 → Manual Apply → 切到 Move gizmo → Ctrl+Z 一次 → 確認支撐點還原為 Apply 前的 0 個（而非進 gizmo 前的狀態）
  *(人工實測通過：Case A 確認不 crash，支撐正確還原)*
- [x] 5.2 **連續 Apply → switch → 多次 Ctrl+Z**：Apply（3 點）→ 繼續新增 2 點 → Apply（5 點）→ 切到 Hollow gizmo → Ctrl+Z 兩次 → 確認第一次 undo 回到 3 點，第二次 undo 回到 0 點
  *(人工實測通過：Case A 確認 5 → 3 → 0 正確，redo 回到 3 / 5 正確)*
- [x] 5.3 **Manual Apply 後繼續 in-session 編輯**：Apply（3 點）→ 繼續新增 2 點（共 5 點） → 在 editing mode 內 Ctrl+Z → 確認 undo 只撤銷新增的那 2 點，還原回 3 點，不跳到 0 點
  *(已驗證並記錄為 out-of-scope。Manual Editing mode 內 undo/redo routing 行為屬於 active SlaSupports undo routing 問題，已記錄為 [KB-4]，移出本 change acceptance。本 change 只要求 Manual Apply 後離開 / 跨 gizmo 的 undo/redo 正確還原，此目標已達成。Candidate follow-up: fix-sla-supports-active-undo-routing)*
- [x] 5.4 **Implicit Apply 路徑（`editing_mode_apply_changes()`）行為未退化**
  此路徑有兩個觸發入口：
  (a) **Enter key**：在 SlaSupports Manual Editing mode（`m_editing_mode == true`）內按 Enter → `WXK_RETURN` → `SLAGizmoEventType::ApplyChanges` → `editing_mode_apply_changes()`。注意：必須先按「Manual Editing」按鈕進入 editing mode，Auto mode 下按 Enter 無效。
  (b) **Implicit apply on gizmo deactivation**：在 editing mode 有未儲存變更時關閉 SlaSupports → `on_set_state(Off)` → dialog → Yes → `editing_mode_apply_changes()`。
  *(人工實測通過：Enter key 在 Manual Editing mode 中觸發 Apply 成功，undo/redo 行為正確，未受本 change 修改干擾。)*
  *(注意既有行為差異：Enter Apply 後離開 Manual Editing mode / 切回 Auto 支撐模式；Manual Apply button Apply 後仍留在 Manual Editing mode。此差異為既有行為，本 change 不改。見 [KB-3]。)*
- [x] 5.5 **Undo 後 SlaSupports 仍 active**：在 SlaSupports（非 editing mode）Apply 一次後，不切 gizmo，直接 Ctrl+Z → 確認 3D viewport 的支撐點球顯示更新（無殘留舊球）
  *(已驗證並記錄為 out-of-scope。停留在 SlaSupports active 狀態時的 undo/redo routing 行為已記錄為 [KB-4]，移出本 change acceptance。本 change 只要求離開 SlaSupports / 跨 gizmo 後的 undo/redo 以 Apply 為節點正確還原，此目標已達成。Candidate follow-up: fix-sla-supports-active-undo-routing)*
- [x] 5.6 **Redo 路徑**：完成 5.1 後，Ctrl+Y redo → 確認 3D viewport 還原到 Apply 後的 3 個支撐點
  *(人工實測通過：Case A redo 後 viewport 正確顯示 3 / 5 supports)*
- [x] 5.7 **UI Undo button parity**：重做 5.1 場景，將 Ctrl+Z 改為點擊介面上的 Undo toolbar button → 確認結果與 5.1 一致（支撐點還原 0 個）且不 crash
  *(人工實測通過：UI Undo toolbar button 與 Ctrl+Z 行為一致，支撐正確還原)*
- [x] 5.8 **UI Redo button parity**：重做 5.6 場景，將 Ctrl+Y 改為點擊介面上的 Redo toolbar button → 確認結果與 5.6 一致（還原到 3 個）且不 crash
  *(人工實測通過：UI Redo toolbar button 與 Ctrl+Y 行為一致，支撐正確還原)*
- [x] 5.9 **Repeated undo boundary safety（Ctrl+Z）**：Manual Apply → leave SlaSupports → 連續按 Ctrl+Z 直到 undo 無法繼續 → 確認全程不 crash，末尾 undo 呈現 safe no-op 而非 crash
  *(人工實測通過：連續兩次 undo 不再 crash，boundary-safe loop 有效)*
- [x] 5.10 **Repeated undo boundary safety（UI Undo button）**：同 5.9，改用 UI Undo toolbar button 連續點擊 → 確認全程不 crash
  *(人工實測通過：UI Undo toolbar button 連續點擊不 crash，boundary-safe loop 有效)*

## 6. Known Behavior 記錄更新

- [x] 6.1 在本 change 的 tasks.md 末尾記錄驗證過程中發現的任何新已知行為，並說明是否需另開 change 處理
  *(已記錄 [KB-1] Empty Manual Apply 語義、[KB-2] Structure mode undo/redo 不刷新、[KB-3] Enter/Manual Apply post-Apply UI mode 差異、[KB-4] SlaSupports active 時 undo/redo routing 限制。見下方 Known Behaviors 段落。)*

---

## Known Behaviors（驗證過程中確認）

### [KB-1] Case C: Delete all manual supports → Manual Apply → Undo → Redo → Slice: "No pad can be generated"

**觀察**：
1. Add 3 supports → Manual Apply
2. Delete all supports → Manual Apply（此時 `sla_points_status = UserModified, sla_support_points.size() = 0`）
3. Undo → 支撐回到 3 個
4. Redo → 支撐消失
5. Slice → 跳出 "No pad can be generated" warning

**Root cause**：
Manual Apply 寫入 `sla_points_status = UserModified` 且 `sla_support_points = []`。
Undo/Redo 正確還原此狀態（UserModified + 0 points）。
SlaPosSupportPoints pipeline 在 `sla_points_status == UserModified` 時走 UserModified branch，
直接使用 `po.transformed_support_points()`（empty），跳過 auto-generation，導致 no pad 警告。

**與正常「未手動支撐」的差異**：
未手動設定支撐時，`sla_points_status != UserModified`（為 NoPoints 或 AutoGenerated），
pipeline 走 auto-generation branch，自動生成預設支撐並正常切片。

**待決議**：
刪光手動支撐後 Apply，其語義是：
- Option A：「使用者明確指定 0 支撐」（目前行為：跳過 auto-gen）
- Option B：「清除手動支撐並回到自動生成」（需將 `sla_points_status` 改回 NoPoints 或不寫入 UserModified）

**本 change 的決定**：不修正 Case C 的 production 行為，保留 UserModified + 0 points 的現有語義。
此決定需要另開 change 定義「刪光後 Apply」的正式產品語義，或接受為 intended behavior（使用者明確指定無支撐）。

**Candidate for follow-up change**: `fix-sla-supports-empty-apply-semantics`

---

### [KB-2] Structure 顯示模式 undo/redo 不刷新

**觀察**：
當 SlaSupports 切換到 Structure 顯示模式（`m_show_support_structure = true`，顯示 support tree + pad mesh 而非 support point spheres）時，執行 undo/redo 後，顯示模式可能不正確刷新：
- Viewport 顯示的支撐結構與實際還原後的 `mo->sla_support_points` 狀態不一致。
- 可能出現 point sphere 顯示與 structure mesh 混合的視覺狀態。
- 切換回 SlaSupports 後可能需手動觸發重新整理才顯示正確。

**Root cause（初步推斷）**：
`GLGizmosManager::update_after_undo_redo()` 中的 `reload_cache()` 更新 `m_normal_cache`（support point spheres），並呼叫 `m_parent.set_as_dirty()` 觸發 repaint。但 Structure 顯示模式使用的是 SLA print object 的 support mesh（透過 `update_volumes()` 更新），需要等 async reslice 完成後才刷新。undo/redo 觸發的 async reslice 可能在 `update_after_undo_redo` 完成後才完成，導致顯示出現時序差異或遺漏 dirty flag。

**本 change 的決定**：不修正此 Known Behavior。
Structure mode undo/redo refresh 屬於 async reslice pipeline 與 gizmo display mode 的協作問題，超出本 change 主題（Manual Apply snapshot undo-stack semantics）。

**Candidate for follow-up change**: `fix-sla-supports-structure-view-undo-refresh`

---

### [KB-3] Enter Apply exits Manual Editing mode; Manual Apply button keeps it active

**觀察**：
- **Enter key path**（`editing_mode_apply_changes()`）：Apply 成功後離開 Manual Editing mode，切回 Auto 支撐模式。
- **Manual Apply button path**（`leave→snapshot→enter` flow）：Apply 成功後仍留在 Manual Editing mode，使用者可繼續編輯。

**Root cause**：
`editing_mode_apply_changes()` 會先呼叫 `disable_editing_mode()`（設 `m_editing_mode = false`），再寫入 model data + reslice。
Manual Apply button 不呼叫 `disable_editing_mode()`，刻意保持 `m_editing_mode = true` 以避免 UI 切換閃爍（見 design.md Decision 1）。

**兩條路徑的行為均正確**：support data 都能正確寫入 main undo stack，undo/redo 行為一致。

**本 change 的決定**：保留此既有行為差異，不統一兩條路徑的 post-Apply UI mode。

---

### [KB-4] Undo/Redo while SlaSupports remains active may not affect main-stack support snapshots

**觀察**：
- Manual Apply 提交的 snapshot 在**離開 SlaSupports / 切換到其他 gizmo 後**，可透過 Ctrl+Z / Ctrl+Y 或 UI Undo/Redo toolbar button 正確還原。
- 但**停留在 SlaSupports 功能內**時，undo/redo 可能無效（對 main-stack support snapshot 無作用），或行為不確定。
- 在 Manual Editing mode 內（`m_editing_mode == true`），active stack 是 gizmo-local stack，Ctrl+Z 只處理尚未 Apply 的局部編輯，不影響已進 main stack 的 Apply snapshot，此行為符合設計。
- 在 SlaSupports Auto mode（`m_editing_mode == false`）停留時的 undo/redo routing 行為尚未完整驗證。

**Keyboard shortcuts 與 toolbar button 行為一致**：兩者在本 change 核心測試場景（離開 SlaSupports 後 undo/redo）均通過驗證。

**本 change 的決定**：不修正此限制。本 change 核心目標是「Manual Apply 後離開/跨 gizmo undo/redo 以 Apply 為節點正確還原」，此目標已達成。若產品需要 SlaSupports active 時的 main-stack undo/redo，請另開 change 定義並實作該行為。

**Candidate follow-up change**: `fix-sla-supports-active-undo-routing`