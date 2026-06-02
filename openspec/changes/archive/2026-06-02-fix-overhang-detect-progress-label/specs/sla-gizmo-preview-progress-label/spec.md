## MODIFIED Requirements

### Requirement: Gizmo preview 成功完成後顯示功能別完成通知

（擴展既有需求：補入 Overhang Detection gizmo）

| Preview 類型 | 成功完成顯示文字 |
|---|---|
| Support gizmo | "Support complete" |
| Hollow gizmo | "Hollow/Drill complete" |
| Drill gizmo | "Hollow/Drill complete" |
| Overhang Detection gizmo | "Overhang detection complete" |

#### Scenario: Overhang Detection preview 成功完成顯示 "Overhang detection complete"
- **WHEN** Overhang Detection gizmo（`GLGizmoLcdOverhangDetection`）的「Detect selected」觸發的 partial reslice 成功完成時
- **THEN** 進度通知顯示「Overhang detection complete」（或其翻譯），**不顯示**「Slice complete」，**不顯示**「Hollow/Drill complete」

---

### Requirement: Gizmo preview 取消後顯示功能別取消通知

（擴展既有需求：補入 Overhang Detection gizmo）

| Preview 類型 | 取消顯示文字 |
|---|---|
| Support gizmo | "Support cancelled" |
| Hollow gizmo | "Hollow/Drill cancelled" |
| Drill gizmo | "Hollow/Drill cancelled" |
| Overhang Detection gizmo | "Overhang detection cancelled" |

#### Scenario: Overhang Detection preview 取消顯示 "Overhang detection cancelled"
- **WHEN** Overhang Detection gizmo 的 partial reslice 在完成前被使用者取消時
- **THEN** 進度通知顯示「Overhang detection cancelled」（或其翻譯），**不顯示**「Slicing Canceled」，**不顯示**「Hollow/Drill cancelled」，**不顯示**「Overhang detection complete」

---

## ADDED Requirements

### Requirement: Overhang Detection preview 運作中顯示固定「懸空偵測」文字，不顯示 pipeline step label

當 Overhang Detection gizmo 觸發 partial reslice 時，SLA pipeline 內部會依序執行 `slaposHollowing`、`slaposDrillHoles`、`slaposObjectSlice` 等步驟，各步驟對應的 `OBJ_STEP_LABELS()` 輸出（"Hollowing model"、"Drilling holes into model"、"Slicing model"）與懸空偵測操作無關。

右下角進度通知**在整個 partial reslice 執行期間**應固定顯示「Overhang Detecting」（或其翻譯），**不得**顯示任何 SLA pipeline step label，**不得**附加「Slicing: 」前綴。

實作方式：`on_slicing_update()` 中當 `m_sla_gizmo_preview_type == OverhangDetect` 時，完整覆蓋 `evt.status.text = _u8L("Overhang Detecting")`，不拼接 step label。

#### Scenario: Overhang Detection preview 運作中僅顯示 "Overhang Detecting"
- **WHEN** 使用者觸發 Overhang Detection gizmo 的「Detect selected」操作而啟動 partial reslice 時
- **THEN** 進度通知在整個執行期間顯示「Overhang Detecting」（或其翻譯），**不顯示**「Hollowing model」，**不顯示**「Drilling holes into model」，**不顯示**「Slicing model」，**不附加**「Slicing: 」前綴

#### Scenario: Overhang Detection preview 後的完整切片不受影響
- **WHEN** Overhang Detection preview 完成（成功或取消）後，使用者啟動完整切片時
- **THEN** 完整切片的進度通知正確顯示「Slicing: 」前綴；完成後顯示「Slice complete」；取消後顯示「Slicing Canceled」（preview 狀態無殘留）
