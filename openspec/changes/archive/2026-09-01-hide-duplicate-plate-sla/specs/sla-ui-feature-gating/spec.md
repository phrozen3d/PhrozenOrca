## ADDED Requirements

### Requirement: SLA 模式下 Edit menu 的 Duplicate Current Plate 項目不出現

當 active printer technology 為 SLA 時，Edit menu（左上角選單 Edit → Duplicate Current Plate）SHALL NOT 顯示 "Duplicate Current Plate" 項目。當 active printer technology 為 FDM (ptFFF) 時，此項目 SHALL 正常顯示並可正常使用，行為與現況相同。

#### Scenario: SLA printer — Duplicate Current Plate 不出現在 Edit menu
- **WHEN** active printer technology 為 SLA
- **AND** 使用者開啟 Edit menu
- **THEN** "Duplicate Current Plate" 項目 SHALL NOT 出現在 Edit menu 中

#### Scenario: FDM printer — Duplicate Current Plate 正常顯示且可用
- **WHEN** active printer technology 為 FDM (ptFFF)
- **AND** 使用者開啟 Edit menu
- **THEN** "Duplicate Current Plate" 項目 SHALL 正常顯示於 Edit menu 中
- **AND** 點擊該項目 SHALL 正常觸發 duplicate current plate 的行為

#### Scenario: 切換 printer technology 後 Duplicate Current Plate 顯示狀態正確更新
- **WHEN** 使用者在應用程式啟動後從 FDM 切換至 SLA，或從 SLA 切換至 FDM
- **THEN** 下次開啟 Edit menu 時，"Duplicate Current Plate" 項目的顯示/隱藏狀態 SHALL 反映目前的 printer technology
- **AND** 狀態 SHALL NOT 固定為應用程式啟動時的 printer technology

#### Scenario: Edit menu 其他項目與其他入口不受影響
- **WHEN** active printer technology 為 SLA 或 FDM
- **THEN** Edit menu 中除 "Duplicate Current Plate" 以外的其他項目（如 Undo、Redo、Cut、Copy、Paste、Delete selected、Delete all、Clone selected、Select all、Deselect all）SHALL 維持現有的顯示與可用行為，不受此項目顯示條件變更影響
- **AND** duplicate current plate 功能本身（`Plater::duplicate_plate()`）與其他呼叫路徑 SHALL NOT 被移除或修改
