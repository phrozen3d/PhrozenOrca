## ADDED Requirements

### Requirement: m_working_holes 作為 Drill session 的 pending working set

`GLGizmoDrill` SHALL 引入成員 `sla::DrainHoles m_working_holes`，作為 session 內所有孔編輯操作的唯一暫存空間。`m_working_holes` 代表尚未提交到正式切片資料的 pending preview 狀態。`ModelObject::sla_drain_holes` SHALL 永遠只保存最後一次 Apply 後的正式孔資料。

#### Scenario: 進入 Drill gizmo 時初始化 working set

- **WHEN** `on_set_state(On)` 或 `data_changed` 偵測到新物件（`m_old_mo_id != mo->id()`）
- **THEN** `m_working_holes` SHALL 被初始化為該物件當前的 `sla_drain_holes`
- **AND** `sla_drain_holes` SHALL 保持不變（不因進入 Drill 而修改）

#### Scenario: Add 孔只寫入 working set

- **WHEN** 使用者在 Drill gizmo 中左鍵點擊模型面新增一孔
- **THEN** 新孔 SHALL 被加入 `m_working_holes`
- **AND** `sla_drain_holes` SHALL 保持不變
- **AND** render_points() SHALL 顯示 m_working_holes 中的所有孔（包含新孔）

#### Scenario: Delete 孔只修改 working set

- **WHEN** 使用者刪除選取的孔（Delete 鍵、Remove selected 或 Remove all）
- **THEN** 孔 SHALL 從 `m_working_holes` 移除
- **AND** `sla_drain_holes` SHALL 保持不變

#### Scenario: Move 孔只修改 working set

- **WHEN** 使用者拖曳孔改變位置
- **THEN** `m_working_holes` 中對應孔的 pos / normal SHALL 更新
- **AND** `sla_drain_holes` SHALL 保持不變

#### Scenario: 改變孔大小只修改 working set

- **WHEN** 使用者透過 Diameter / Depth slider 或 InputFloat 修改選取孔的尺寸
- **THEN** `m_working_holes` 中對應孔的 radius / height SHALL 更新
- **AND** `sla_drain_holes` SHALL 保持不變

---

### Requirement: Apply 是唯一的 commit path

Apply 按鈕 SHALL 執行以下動作，且必須按此順序：
1. `Plater::TakeSnapshot(plater, "Apply drain holes")`（捕捉 Apply 前的 sla_drain_holes）
2. `mo->sla_drain_holes = m_working_holes`（將 pending working set 寫入正式資料）
3. 觸發必要的 reslice / refresh

Apply 本身 SHALL NOT 修改 `m_working_holes`。Apply 前的個別孔操作（Add / Delete / Move / Size）SHALL NOT 建立 TakeSnapshot。

#### Scenario: Apply 後 sla_drain_holes 更新為 working set 內容

- **WHEN** 使用者按下 Apply 按鈕（`m_working_holes` 有 3 個孔）
- **THEN** `TakeSnapshot("Apply drain holes")` SHALL 先被建立（捕捉 Apply 前的 sla_drain_holes）
- **AND** `mo->sla_drain_holes` SHALL 變為與 `m_working_holes` 相同的 3 個孔
- **AND** reslice SHALL 被觸發

#### Scenario: Apply 後切片使用正式孔資料

- **WHEN** Apply 完成後使用者執行切片匯出
- **THEN** slicer SHALL 使用已 Apply 的 `sla_drain_holes` 中的孔資料
- **AND** 切出的孔數量 SHALL 與 Apply 時的 `m_working_holes` 一致

#### Scenario: Apply 前的操作不觸發 TakeSnapshot

- **WHEN** 使用者在 session 內執行 Add / Delete / Move / Size 操作（未按 Apply）
- **THEN** Undo stack SHALL NOT 新增任何 snapshot
- **AND** Ctrl+Z 在 Apply 前無效果（Undo stack 無可還原項目，或回到上一個 Apply 邊界）

---

### Requirement: Exit without Apply 丟棄 pending working set

`on_set_state(Off)` SHALL 直接清空 `m_working_holes`，不將 pending 孔寫回 `sla_drain_holes`。`sla_drain_holes` 在 Exit 後 SHALL 保持最後一次 Apply 後的狀態。

#### Scenario: 新增孔後不 Apply 直接離開 gizmo

- **WHEN** 使用者在 session 中新增 3 孔但未按 Apply，然後離開 Drill gizmo
- **THEN** `sla_drain_holes` SHALL 保持進入 Drill 前的狀態（0 孔，若進入前無孔）
- **AND** `m_working_holes` SHALL 被清空

#### Scenario: Apply 後再編輯但未再次 Apply 就離開

- **WHEN** 使用者先 Apply 3 孔（sla_drain_holes = [H1,H2,H3]），再新增 H4 但未 Apply，然後離開
- **THEN** `sla_drain_holes` SHALL 保持 [H1,H2,H3]
- **AND** H4 SHALL 不出現在 sla_drain_holes 中

#### Scenario: Object switch 時 pending working set 不污染其他物件

- **WHEN** 使用者切換到另一個物件
- **THEN** 舊物件的 `sla_drain_holes` SHALL 保持最後一次 Apply 後的狀態（不受 m_working_holes 影響）
- **AND** 新物件的 `m_working_holes` SHALL 以新物件的 `sla_drain_holes` 初始化

---

### Requirement: Undo/Redo 後重建 working set

`data_changed(is_serializing=true)` 被呼叫時（Undo/Redo 後），SHALL 以 cereal 已還原的 `mo->sla_drain_holes` 重建 `m_working_holes`，並同步 selection cache 和 raycasters。

#### Scenario: Undo 後 working set 與 applied state 一致

- **WHEN** 使用者 Apply 3 孔後執行 Ctrl+Z
- **THEN** cereal 將 `sla_drain_holes` 還原至 Apply 前狀態（0 孔，若 Apply 是第一個操作）
- **AND** `data_changed(is_serializing=true)` SHALL 將 `m_working_holes` 設為還原後的 `sla_drain_holes`
- **AND** render_points() SHALL 顯示 0 孔

#### Scenario: Redo 後 working set 與 applied state 一致

- **WHEN** 使用者在 Undo 後執行 Ctrl+Y（Redo）
- **THEN** cereal 將 `sla_drain_holes` 還原至 Apply 後狀態（3 孔）
- **AND** `data_changed(is_serializing=true)` SHALL 將 `m_working_holes` 設為還原後的 `sla_drain_holes`
- **AND** render_points() SHALL 顯示 3 孔

#### Scenario: Undo/Redo 後 selection cache 不越界

- **WHEN** Undo/Redo 後 `sla_drain_holes` 的孔數量少於 Undo 前
- **THEN** `m_selected` SHALL 被 `reload_cache()` 重設為與 `m_working_holes.size()` 相符的大小
- **AND** 不應發生 index out-of-range 存取

---

### Requirement: Undo/Redo 後 DrillHoles reslice 只在 applied holes 改變時觸發

`GLGizmoDrill` SHALL 維護一個 `m_last_resliced_holes` baseline，代表最後一次成功排程 `reslice_until_step(slaposDrillHoles)` 時的 `sla_drain_holes` 內容。`data_changed(is_serializing=true)` 中，SHALL 以 `mo->sla_drain_holes != m_last_resliced_holes` 為條件，只有不同時才觸發 reslice 並同步 baseline。此機制避免與 drill holes 無關的 Undo/Redo 操作觸發不必要的 CGAL boolean 重算。

#### Scenario: Apply 後 Undo 觸發 DrillHoles reslice

- **WHEN** 使用者 Apply 3 孔後執行 Ctrl+Z（Undo Apply）
- **THEN** `sla_drain_holes` cereal 還原為 0 孔
- **AND** `sla_drain_holes（0 孔）!= m_last_resliced_holes（3 孔）`
- **THEN** `reslice_until_step(slaposDrillHoles, true)` SHALL 被呼叫（DrillHoles reslice 排程）
- **AND** `m_last_resliced_holes` SHALL 更新為 0 孔
- **AND** SLA preview mesh SHALL 在 reslice 完成後反映 0 孔狀態

#### Scenario: 非 drill Undo/Redo 不觸發 DrillHoles reslice

- **WHEN** Drill gizmo active 時，使用者執行與 drill holes 無關的 Undo/Redo（`sla_drain_holes` 未改變）
- **THEN** `sla_drain_holes == m_last_resliced_holes`（相同）
- **THEN** `reslice_until_step` SHALL NOT 被呼叫
- **AND** background SLA pipeline SHALL NOT 重新計算 DrillHoles step

#### Scenario: Drill baseline 在 Enter / Apply / Exit 時正確維護

- **WHEN** 使用者進入 Drill gizmo 或切換到新物件
- **THEN** `m_last_resliced_holes` SHALL 被初始化為當時的 `sla_drain_holes`
- **WHEN** Apply 按鈕被按下並成功 reslice
- **THEN** `m_last_resliced_holes` SHALL 更新為 apply 後的 `sla_drain_holes`（= m_working_holes）
- **WHEN** 使用者離開 Drill gizmo（on_set_state(Off)）
- **THEN** `m_last_resliced_holes` SHALL 被清空

---

### Requirement: pending dirty 時 Ctrl+Z 先 discard pending changes

當 Drill gizmo active 且存在未 Apply 的 pending changes（`m_working_holes != sla_drain_holes`）時，Ctrl+Z SHALL 優先 discard pending changes 而非觸發 global undo。若無 pending changes，Ctrl+Z SHALL 正常觸發 global undo。Ctrl+Y（Redo）不受此機制影響，永遠正常執行。

#### Scenario: 有 pending 時 Ctrl+Z discard pending

- **WHEN** Drill gizmo active 且 `m_working_holes != mo->sla_drain_holes`（有 pending）
- **AND** 使用者按 Ctrl+Z
- **THEN** `m_working_holes` SHALL 回到 `mo->sla_drain_holes`（applied state）
- **AND** raycasters 與 selection cache SHALL 同步重建
- **AND** render SHALL 更新為 applied 孔狀態
- **AND** global undo stack SHALL NOT 被觸發（不回到更前面的 snapshot）

#### Scenario: 無 pending 時 Ctrl+Z 觸發 global undo

- **WHEN** Drill gizmo active 且 `m_working_holes == mo->sla_drain_holes`（無 pending）
- **AND** 使用者按 Ctrl+Z
- **THEN** global undo SHALL 正常執行（`EVT_GLCANVAS_UNDO` 觸發）
- **AND** `sla_drain_holes` 回到上一個 Apply snapshot 的狀態

#### Scenario: Ctrl+Z 第二次在 discard 後觸發 global undo

- **WHEN** 使用者第一次 Ctrl+Z 執行了 discard（無 pending 剩餘）
- **AND** 使用者第二次按 Ctrl+Z
- **THEN** `has_pending_changes()` 回傳 false
- **THEN** global undo SHALL 正常執行

---

### Requirement: on_save / on_load 不序列化 m_working_holes

`on_save` / `on_load` 的序列化欄位 SHALL 維持現有的 `m_new_hole_radius`、`m_new_hole_height`、`m_selected`、`m_selection_empty` 四個欄位，不加入 `m_working_holes`。`m_working_holes` 在 Undo/Redo callback 中由 `sla_drain_holes` 重建，不需獨立序列化。

#### Scenario: Undo/Redo 後 on_load 呼叫不破壞 working set

- **WHEN** Undo/Redo 觸發 `on_load()` 讀取序列化狀態
- **THEN** `on_load` SHALL 還原 m_new_hole_radius、m_new_hole_height、m_selected、m_selection_empty
- **AND** `m_working_holes` SHALL 由後續的 `data_changed(is_serializing=true)` 重建，而非由 `on_load` 設定

---

## Non-Goals

### Non-Goal: 非 Drill gizmo 下 Undo/Redo 後 DrillHoles preview mesh 立即刷新

本 change 不保證在 Drill gizmo 以外（例如 Move、Hollow、或無 active gizmo）執行 Undo/Redo 後，3D 假切 preview mesh 立即反映 `sla_drain_holes` 的變更。

**此限制不影響正式資料與 export 正確性**：
- `sla_drain_holes` 由 cereal 正確還原
- `SLAPrint::apply()` 正確標記 `slaposDrillHoles` pipeline cache 為 invalid
- 切片 / export 時 pipeline 從正確狀態重算，結果正確

**3D preview mesh 更新時機**（在 PhrozenOrca on-demand 模式下）：
- 使用者重新進入 Drill gizmo（`data_changed` + `m_last_resliced_holes` 機制觸發 reslice）
- 手動按 Slice 按鈕
- Apply 操作觸發 `reslice_until_step(slaposDrillHoles)`

精確的非 Drill 模式 preview 刷新支援（`SnapshotData::RECALCULATE_SLA_DRILL_HOLES` snapshot flag 方案）為後續 change 的範疇（`fix-drill-preview-refresh-after-undo-redo`）。