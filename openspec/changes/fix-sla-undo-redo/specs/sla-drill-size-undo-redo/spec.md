## ADDED Requirements

### Requirement: SLA drain holes data serializes correctly through ModelObject

`ModelObject::sla_drain_holes` 的每個 `sla::DrainHole` 欄位（`pos`、`normal`、`radius`、`height`）SHALL 在 cereal BinaryArchive round-trip 後保持數值完整，此為 undo/redo stack 能正確還原 drill hole 狀態的資料層前提。此測試 SHALL 在無 GUI 依賴的環境（`tests/libslic3r/`）執行。

#### Scenario: DrainHole fields survive cereal round-trip

- **WHEN** `ModelObject` 含有一個 `sla::DrainHole{pos=(1,2,3), normal=(0,0,-1), radius=1.5, height=8.0}` 被序列化至 BinaryOutputArchive
- **WHEN** 從同一 archive 反序列化至新的 `ModelObject`
- **THEN** `sla_drain_holes[0].radius` SHALL 等於 1.5f
- **THEN** `sla_drain_holes[0].height` SHALL 等於 8.0f
- **THEN** `sla_drain_holes[0].pos` SHALL 等於 `Vec3f{1.f, 2.f, 3.f}`

#### Scenario: Multiple DrainHoles preserve order and count

- **WHEN** `ModelObject` 含有 3 個 DrainHoles 被序列化
- **WHEN** 從 archive 反序列化
- **THEN** 還原後的 `sla_drain_holes` 數量 SHALL 為 3
- **THEN** 各洞的欄位值 SHALL 與序列化前完全一致

---

### Requirement: Drain hole diameter change is undoable

GLGizmoDrill SHALL 在使用者透過 diameter slider 或 InputFloat 修改選取洞的 radius 並 commit 後，建立 undo snapshot，使 undo 能正確還原所有受影響洞的 radius 至修改前的值。

commit 的定義：
- Slider：`get_last_slider_status().deactivated_after_edit` 為 true（使用者放開 slider）
- InputFloat：`ImGui::IsItemDeactivatedAfterEdit()` 為 true（使用者按 Enter 或點擊他處）

Snapshot 的建立 SHALL 遵循 BrimEars begin/apply 模式：先還原舊值至 ModelObject → 呼叫 TakeSnapshot → 重新套用新值，確保 snapshot 捕捉的是「修改前」的 ModelObject 狀態。

#### Scenario: Undo after changing diameter restores all selected holes

- **WHEN** 使用者在 Drill gizmo 中選取 hole #1 和 hole #2
- **WHEN** 使用者將 diameter slider 從 2.0mm 拖曳至 4.0mm 並放開（commit）
- **WHEN** 使用者執行 undo
- **THEN** `mo->sla_drain_holes[0].radius` SHALL 還原為 1.0（diameter 2.0 / 2）
- **THEN** `mo->sla_drain_holes[1].radius` SHALL 還原為 1.0
- **THEN** 未選取的洞的 radius SHALL 不受影響

#### Scenario: Slider drag without release does not create intermediate snapshots

- **WHEN** 使用者拖曳 diameter slider（尚未放開）
- **THEN** undo stack 中 SHALL NOT 新增任何 snapshot
- **THEN** 洞的 radius SHALL 即時更新（視覺反饋）

#### Scenario: InputFloat commit creates undo snapshot

- **WHEN** 使用者在 diameter InputFloat 欄位輸入數值並按下 Enter
- **WHEN** 使用者執行 undo
- **THEN** `mo->sla_drain_holes` 的 radius SHALL 還原為輸入前的值

---

### Requirement: Drain hole depth change is undoable

GLGizmoDrill SHALL 在使用者透過 depth slider 或 InputFloat 修改選取洞的 height 並 commit 後，建立 undo snapshot，行為與 diameter 變更一致。

#### Scenario: Undo after changing depth restores all selected holes

- **WHEN** 使用者在 Drill gizmo 中選取 hole #0
- **WHEN** 使用者將 depth slider 從 5.0mm 拖曳至 8.0mm 並放開（commit）
- **WHEN** 使用者執行 undo
- **THEN** `mo->sla_drain_holes[0].height` SHALL 還原為 5.0
- **THEN** 未選取的洞的 height SHALL 不受影響

#### Scenario: Simultaneous diameter and depth changes each create separate snapshots

- **WHEN** 使用者修改 diameter 並 commit（snapshot X）
- **WHEN** 使用者修改 depth 並 commit（snapshot Y）
- **WHEN** 使用者執行 undo 兩次
- **THEN** 第一次 undo SHALL 還原 depth 變更（回到 snapshot X）
- **THEN** 第二次 undo SHALL 還原 diameter 變更（回到 snapshot X 前的狀態）

---

### Requirement: Drill gizmo size change state is correctly tracked across frames

GLGizmoDrill SHALL 維護 `m_radius_before_change`、`m_height_before_change`、`m_holes_before_change` 成員，於 slider 首次被 activated 時儲存舊值，於 commit 後清除。若使用者放棄編輯（未 commit），這些暫存值 SHALL 在下次 begin 時被覆蓋。

#### Scenario: State is cleared after apply

- **WHEN** 使用者拖曳 slider 並放開（apply_size_change 執行）
- **THEN** `m_radius_before_change` SHALL 為 0.f
- **THEN** `m_height_before_change` SHALL 為 0.f
- **THEN** `m_holes_before_change` SHALL 為空

#### Scenario: Consecutive size changes each create independent snapshots

- **WHEN** 使用者將 diameter 從 2.0 改為 4.0 並 commit
- **WHEN** 使用者再將 diameter 從 4.0 改為 6.0 並 commit
- **WHEN** 使用者執行 undo 兩次
- **THEN** undo stack SHALL 包含兩個獨立的 diameter 變更 snapshot

---

### Requirement: Apply is not an Undo stack entry; it updates session baseline only

GLGizmoDrill 的 Apply 操作 SHALL NOT 建立 Undo snapshot。Apply 的語意是將目前 Drill session 的孔狀態設為新的「已套用基準」（`m_holes_stash`），而非新增可 Undo 的模型操作。Apply 仍觸發 `reslice_until_step(slaposDrillHoles)` 假切流程，不做真正 boolean。

Drill 操作與 snapshot 的對應關係（commit 514d02748 時間點）：

| 操作 | 是否建立 Snapshot |
|------|-----------------|
| 新增孔 | 是（"Add drainage hole"）|
| 刪除孔 | 是（"Delete drainage hole"）|
| 拖曳孔位置 | 是（"Move drainage hole"）|
| Apply（原 Preview） | 否 |
| Diameter / Depth 修改選中孔 | 是（本次 task 新增）|

`m_holes_stash` 的生命週期語意：
- 進入 Drill 時：備份當下 `mo->sla_drain_holes` 作為初始 baseline
- 按 Apply 時：將目前 `mo->sla_drain_holes` 更新為新 baseline
- 離開 Drill / 切換工具 / 切換物件時：還原至 baseline（若未 Apply 則還原至進入時狀態）

若未來要支援「Undo Apply」，需先定義 Undo Apply 的語意（撤回假切？撤回 session baseline？撤回孔資料？），屬新架構設計，不在本 task 範圍。

#### Scenario: Apply does not appear in undo stack

- **WHEN** 使用者在 Drill gizmo 中新增孔 H1 並按下 Apply
- **THEN** undo stack SHALL NOT 新增任何以 Apply 為名的 snapshot
- **WHEN** 使用者執行 Undo
- **THEN** Undo SHALL 還原至新增 H1 之前的狀態（"Add drainage hole" snapshot）

---

### Requirement: Undo/Redo must sync session baseline (m_holes_stash)

GLGizmoDrill SHALL 在 `data_changed(is_serializing=true)` 且仍為同一 ModelObject 時，將 `m_holes_stash` 同步為目前 `mo->sla_drain_holes`，確保 Undo/Redo 後離開 Drill 不會以舊 baseline 覆蓋已還原的孔資料。

同步條件：`data_changed` 以 `is_serializing=true` 被呼叫，且 `m_stash_initialized` 為 true。

```cpp
} else if (is_serializing && m_stash_initialized) {
    m_holes_stash = mo->sla_drain_holes;
}
```

#### Scenario: Undo after Apply does not get overwritten on exit

- **WHEN** 使用者新增孔 H1 並按 Apply（`m_holes_stash = [H1]`）
- **WHEN** 使用者執行 Undo（`mo->sla_drain_holes` 還原為空）
- **WHEN** 使用者離開 Drill gizmo
- **THEN** `mo->sla_drain_holes` SHALL 維持為空，不被 `m_holes_stash` 中的 [H1] 覆蓋

#### Scenario: Undo of Remove after Apply is correctly preserved on exit

- **WHEN** 原本有孔 H1、使用者執行 Remove 並按 Apply（`m_holes_stash = []`）
- **WHEN** 使用者執行 Undo（`mo->sla_drain_holes` 還原為 [H1]）
- **WHEN** 使用者離開 Drill
- **THEN** `mo->sla_drain_holes` SHALL 維持為 [H1]，不被舊 stash [] 覆蓋

#### Scenario: Multiple Undos past Apply point preserve correct holes on exit

- **WHEN** 使用者 Apply 後繼續新增孔（未再次 Apply），執行多次 Undo 越過 Apply 點
- **WHEN** 使用者離開 Drill
- **THEN** `mo->sla_drain_holes` SHALL 反映最後一次 Undo 後的實際狀態，不被 Apply 時的 stash 帶回

---

### Requirement: Undo/Redo must sync selection cache and raycasters

GLGizmoDrill SHALL 在 Undo/Redo 的 `data_changed(is_serializing=true)` path 中，於 stash 同步後執行：
1. `reload_cache()`：讓 `m_selected` 的大小對齊還原後的 `sla_drain_holes.size()`
2. `unregister_hole_raycasters_for_picking()`：清除 stale raycasters，讓 common path 依照目前 holes 重新 register

#### Scenario: No crash when undoing multiple times in same gizmo

- **WHEN** 使用者在 Drill gizmo 中連續執行多次 Undo
- **THEN** 應用程式 SHALL NOT crash
- **THEN** 孔的視覺顯示 SHALL 與 Undo 後的 `sla_drain_holes` 一致

#### Scenario: No crash when switching gizmo then undoing back to Drill

- **WHEN** 使用者在 Drill gizmo 中新增孔後切換至其他 gizmo
- **WHEN** 使用者執行 Undo（回到包含 Drill 狀態的 snapshot）
- **THEN** 應用程式 SHALL NOT crash
- **THEN** `m_selected` 的大小 SHALL 與 `sla_drain_holes.size()` 一致

---

### Requirement: get_first_volume() null guard in raycaster update

`update_hole_raycasters_for_picking_transform()` SHALL 在呼叫 `m_parent.get_selection().get_first_volume()` 後，若回傳值為 `nullptr`，SHALL 立即 return，不繼續存取 volume 資料。此保護針對 Undo/Redo lifecycle 中 selection 尚未完成更新的時間窗口。

```cpp
const GLVolume* vol = m_parent.get_selection().get_first_volume();
if (!vol)
    return;
```

#### Scenario: Undo does not crash when selection is not yet ready

- **WHEN** Undo/Redo lifecycle 呼叫 `update_hole_raycasters_for_picking_transform()`
- **WHEN** 此時 `get_first_volume()` 回傳 nullptr（selection 尚未同步）
- **THEN** 函式 SHALL 安全 return，不發生 null dereference crash

---

### Requirement: m_old_mo_id is preserved through On state for Off restore fallback

GLGizmoDrill `on_set_state(On)` SHALL NOT 清除 `m_old_mo_id`。`m_old_mo_id` SHALL 保留至 Off restore 完成後，以確保當 `selection_info()->model_object()` 在切換時為 nullptr 時，仍可透過舊物件 ID 找回正確 ModelObject 並完成 restore，避免靜默跳過。

#### Scenario: Holes are correctly restored on exit when selection_info is unreliable

- **WHEN** 使用者在 Drill gizmo 中新增孔（未 Apply），切換至其他工具
- **WHEN** 切換時 `selection_info()->model_object()` 為 nullptr
- **THEN** `mo->sla_drain_holes` SHALL 仍還原為進入 Drill 時（或最後一次 Apply 時）的狀態
- **THEN** restore SHALL NOT 被靜默跳過