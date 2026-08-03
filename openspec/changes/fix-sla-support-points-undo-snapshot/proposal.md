## Why

SLA 支撐點的 undo/redo 在自動生成的情境下無法正確運作。兩個實測案例：

**案例 1（純自動模式）**：密度 100% 按一次 Apply、密度 150% 按一次 Apply → undo 一次**沒有任何反應**，undo 第二次時**所有支撐點一次全部消失**。

**案例 2（自動 + 手動）**：auto apply 一次 → 切到手動模式加三個點 Apply → 再加三個點 Apply → 手動模式下 undo 沒反應；切回自動模式後 undo 一次消失 3 個點，undo 第二次時新增的點都消失，**但第一次 auto generate 的那些點也一起消失了**。

### 根因

`auto_generate()`（`GLGizmoSlaSupports.cpp:2353`）在**點還不存在時**就拍下快照：

```cpp
Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Autogenerate support points");
sync_generate_support_for_object(mo, true);
mo->sla_points_status = sla::PointsStatus::Generating;
reslice_until_step(...);          // ← 點是之後由 backend 非同步算出來的
```

點算完後由 `get_data_from_backend()`（`:2312`）拉進 `m_normal_cache`，而該函式結尾明寫：

> `// We don't copy the data into ModelObject, as this would stop the background processing.`

**自動生成的點永遠不寫進 `mo->sla_support_points`，因此永遠不在 undo 快照的資料裡。**

兩個案例都由此解釋：

- **案例 1**：兩次 Apply 各拍一次快照，但兩次拍下時 `sla_support_points` 都還是舊的（空的）。undo 一次回到第二次 Apply 之前——資料看起來一樣，所以沒反應；undo 兩次回到第一次 Apply 之前——`sla_support_points` 為空，全部消失。
- **案例 2**：手動 commit 走 `commit_manual_edits_keep_editing()`（`:2145`），它**會**寫 `mo->sla_support_points = m_normal_cache`（此時已包含從 backend 拉來的 auto 點）。因此 undo 越過第一次手動 commit 時，`sla_support_points` 被還原到「手動編輯前」——而那個狀態是空的，於是 auto 點跟著一起不見。

「手動模式下 undo 沒反應」是另一個相鄰症狀：編輯模式在 gizmo stack 上（`switch_to_editing_mode()` → `enter_gizmos_stack()`），`"Add support point"`（`:879`）等操作確實有拍 gizmo-local 快照，但作用中 gizmo 的 undo 路由行為已由既有 capability `sla-supports-apply-undo-stack` 標記為 out of scope（見其 [KB-4] 註記與候選 change `fix-sla-supports-active-undo-routing`）。本 change 不重複處理該部分。

### 為什麼需要先定義契約

單純「把 auto 點寫進 `mo->sla_support_points`」會踩到那行註解所警告的問題——中斷背景運算。因此本 change 的第一階段是**定義支撐點的 undo 契約**：哪些操作應該進 undo、快照該在什麼時點拍、auto 點是否要持久化。這是產品決策，不是純技術選擇。

本缺陷於 `perf-sla-support-points-preview-render` 的驗收（任務 7.9）期間發現，經比對確認與該 change 無關：其 diff 未觸碰任何 `TakeSnapshot`、gizmo stack 進出、或 `mo->sla_support_points` 的寫入點。渲染路徑忠實反映 `m_normal_cache` 的當下內容，不會產生過期幾何——錯的是 undo 還原出來的資料本身。

另外，既有 change `fix-sla-undo-redo` 的 Impact 段落記載「不影響範圍：…GLGizmoSlaSupports（已正確實作）」。本 change 的發現推翻該前提，兩者需相互參照。

## What Changes

- **第一階段（契約定義，無程式碼變更）**：明確列出支撐點相關操作的 undo 契約——`auto_generate`、密度／權重變更、手動增刪改、Apply、Remove All 各自是否進 undo、快照時點、以及還原後 `sla_points_status` 應為何。
- **第二階段（實作）**：依契約修正快照時點與資料持久化。候選方向於 design 中列出並比較，實際採用何者取決於第一階段的結論。
- 修正 `auto_generate()` 的快照時點，使還原後的狀態與使用者的心智模型一致（undo 一次應回到前一次 Apply 的結果，而非「什麼都沒有」）。
- 確保手動 commit 之後的 undo 不會連帶抹除先前 auto 生成的點。

### Non-goals

- **不處理作用中 gizmo 的 undo 路由**（編輯模式內 Ctrl+Z 的行為與顯示刷新）。該範圍已由 `sla-supports-apply-undo-stack` 明列為 out of scope，並指向候選 change `fix-sla-supports-active-undo-routing`。
- 不改變 `sla-supports-apply-undo-stack` 已定義的 Manual Apply 快照行為——本 change 補的是它未涵蓋的 auto 生成情境。
- 不改變 Structure 模式的 undo/redo 顯示刷新（該 capability 亦已標記為 out of scope）。
- 不改變 Points preview 的渲染路徑或效能特性。
- 不改變 backend 支撐點生成演算法。
- 不處理 `sla_trafo` 變換後的快取失效（→ `fix-sla-support-points-invalidate-on-trafo-change`）。

## Capabilities

### New Capabilities

- `sla-auto-support-points-undo`：自動生成之支撐點的 undo/redo 契約。涵蓋 auto generate 與密度／權重變更的快照時點、auto 點在 `ModelObject` 中的持久化時機、以及「undo 不得抹除非該次操作產生的點」的不變式。

### Modified Capabilities

<!-- 無。`sla-supports-apply-undo-stack` 規範 Manual Apply 流程，其 requirement 皆不變更；本 change 補的是該 capability 未涵蓋的 auto 生成情境。 -->

## Impact

- **Primary**：`src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp`
  - `auto_generate()`（`:2353`）— 快照時點
  - `get_data_from_backend()`（`:2312`）— auto 點是否寫回 `ModelObject`
  - `commit_manual_edits_keep_editing()`（`:2145`）／ `editing_mode_apply_changes()`（`:2228`）— 與 auto 點持久化的互動
  - 密度變更快照（`:1437`）、Remove All（`:1482`）
- **相互參照**：既有 change `fix-sla-undo-redo`（其 Impact 段落的「GLGizmoSlaSupports 已正確實作」前提需更正）
- 不影響切片輸出、檔案格式或 profile
- 無 public API 變更
