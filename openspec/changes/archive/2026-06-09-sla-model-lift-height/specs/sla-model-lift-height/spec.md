## ADDED Requirements

### Requirement: Raft Setting 顯示 Model Lift Height

系統 SHALL 在 Resin Support 設定頁的 Raft Setting 區塊中，於 Raft Thickness（`pad_wall_thickness`）欄位上方顯示 **Model Lift Height** 參數，綁定至既有 config key `support_object_elevation`。

欄位屬性 SHALL 為：
- Label: `Model Lift Height`
- Tooltip: `The vertical offset of the lowest point on the model from the print platform. Notice that this offset would overwrite any original model offset in the Z direction.`
- 單位: mm
- 最小值: 0

#### Scenario: Raft Setting 欄位順序

- **WHEN** 使用者在 Resin 模式下開啟 Support 設定頁的 Raft Setting 區塊
- **THEN** Model Lift Height 欄位 SHALL 顯示在 Raft Thickness 上方

#### Scenario: 參數值持久化

- **WHEN** 使用者將 Model Lift Height 設為 7 mm 並儲存 process preset
- **THEN** preset config 的 `support_object_elevation` SHALL 為 7

### Requirement: pad_around_object 時隱藏 Model Lift Height

當 `pad_enable` 與 `pad_around_object` 同時啟用（zero-elevation 模式）時，系統 SHALL 隱藏或禁用 Model Lift Height 欄位，且有效 elevation SHALL 為 0。

#### Scenario: Around object pad 啟用

- **WHEN** `pad_enable` 為 true 且 `pad_around_object` 為 true
- **THEN** Model Lift Height 欄位 SHALL 不可編輯或不可見
- **THEN** 支撐管線使用的 `object_elevation_mm` SHALL 為 0

### Requirement: SLA Support Point gizmo 進入時抬升模型

系統 SHALL 在 SLA Support Point gizmo 開啟期間，依目前 `support_object_elevation`（經 `is_zero_elevation` 守衛）對選取模型套用視覺 Z shift，使模型最低點離平台距離與參數一致。

此抬升 SHALL 僅為 3D 預覽（`sla_shift_z` / `SelectionInfo::get_sla_shift()`），SHALL NOT 永久修改 `ModelInstance` 的 Z 變換。

#### Scenario: 進入 gizmo 抬升

- **WHEN** `support_object_elevation` 為 5 mm 且使用者開啟 SLA Support Point gizmo（支撐樹尚未切片）
- **THEN** 選取模型在 3D 視圖中 SHALL 較平台抬高 5 mm

#### Scenario: 退出 gizmo 貼地

- **WHEN** 使用者關閉 SLA Support Point gizmo
- **THEN** 選取模型的視覺 Z shift SHALL 恢復為 0（貼回地面）

### Requirement: Gizmo 期間即時更新抬升高度

當 SLA Support Point gizmo 開啟時，系統 SHALL 在使用者修改 Model Lift Height 後即時更新 3D 預覽中的模型抬升高度。

#### Scenario: 參數變更即時反映

- **WHEN** SLA Support Point gizmo 已開啟且 Model Lift Height 從 5 mm 改為 8 mm
- **THEN** 3D 視圖中模型抬升高度 SHALL 在無需重開 gizmo 的情況下更新為 8 mm
