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
