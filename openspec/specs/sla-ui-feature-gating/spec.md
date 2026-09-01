# sla-ui-feature-gating Specification

## Purpose
TBD - created by archiving change hide-sla-inapplicable-ui-actions. Update Purpose after archive.
## Requirements
### Requirement: SLA 模式下 gizmo toolbar 隱藏 FDM-only 與 assembly-only 工具

當 active printer profile 使用 `ptSLA` 時，gizmo toolbar SHALL NOT 顯示 Brim Ears、Mesh Boolean、Assembly View 的 gizmo slot。

#### Scenario: SLA printer — Brim Ears slot 不出現在 toolbar
- **WHEN** active printer technology 為 SLA
- **THEN** Brim Ears gizmo slot SHALL NOT 出現在 gizmo toolbar 中

#### Scenario: SLA printer — Mesh Boolean slot 不出現在 toolbar
- **WHEN** active printer technology 為 SLA
- **THEN** Mesh Boolean gizmo slot SHALL NOT 出現在 gizmo toolbar 中

#### Scenario: SLA printer — Assembly View gizmo slot 不出現在 toolbar
- **WHEN** active printer technology 為 SLA
- **THEN** Assembly View gizmo slot SHALL NOT 出現在 gizmo toolbar 中

#### Scenario: FDM printer — 所有 gizmo slot 正常顯示且可用
- **WHEN** active printer technology 為 FDM (ptFFF)
- **THEN** Brim Ears、Mesh Boolean、Assembly View gizmo slot SHALL 正常顯示於 toolbar

---

### Requirement: SLA 模式下 Assembly View toolbar group 不殘留任何可見結果

當 active printer technology 為 SLA 時，Assembly View toolbar button、右側 separator 線及 toolbar 背景矩形 SHALL NOT 被渲染。

#### Scenario: SLA printer — Assembly View button 不可見
- **WHEN** active printer technology 為 SLA
- **THEN** Assembly View toolbar button SHALL NOT 可見

#### Scenario: SLA printer — 右側 separator 線不可見
- **WHEN** active printer technology 為 SLA
- **THEN** gizmo toolbar 右側的 separator 線 SHALL NOT 可見

#### Scenario: SLA printer — Assembly View toolbar 背景不可見
- **WHEN** active printer technology 為 SLA
- **THEN** Assembly View 位置 SHALL NOT 出現任何 toolbar 背景矩形

#### Scenario: FDM printer — Assembly View group 正常渲染
- **WHEN** active printer technology 為 FDM
- **THEN** Assembly View button、separator 線及 toolbar 背景 SHALL 在正常位置渲染

---

### Requirement: SLA 模式下 Text (Emboss) menu 項目為 disabled

right-click context menu 的 Add Primitive 下，**Text** 項目在 active printer technology 為 SLA 時 SHALL 為 disabled（灰色且不可互動）。項目 SHALL 保持可見，讓使用者知道此功能在 FDM 模式下可用。

#### Scenario: SLA printer — menu 開啟時 Text 項目為 disabled
- **WHEN** active printer technology 為 SLA
- **AND** 使用者開啟 Add Primitive submenu
- **THEN** Text 項目 SHALL 存在但為 disabled（灰色）
- **AND** 點擊 Text 項目 SHALL NOT 有任何效果

#### Scenario: FDM printer — Text 項目為 enabled
- **WHEN** active printer technology 為 FDM
- **THEN** Text 項目 SHALL 為 enabled 且可互動

#### Scenario: 切換 printer profile 後 Text 狀態正確更新
- **WHEN** 使用者在應用程式啟動後從 SLA 切換至 FDM，或從 FDM 切換至 SLA
- **THEN** 下次開啟 menu 時，Text 項目的 enabled/disabled 狀態 SHALL 反映目前的 printer technology
- **AND** 狀態 SHALL NOT 固定為應用程式啟動時的 printer technology

---

### Requirement: SLA 模式下 Filament menu 項目不出現

當 active printer technology 為 SLA 時，**Change Filament** 與 **Set Filament for selected items** SHALL NOT 出現在 right-click context menu 中。

#### Scenario: SLA printer — filament 項目不出現
- **WHEN** active printer technology 為 SLA
- **AND** 使用者開啟物件的 right-click context menu
- **THEN** "Change Filament" 與 "Set Filament for selected items" SHALL NOT 出現

#### Scenario: FDM printer — filament 項目正常顯示
- **WHEN** active printer technology 為 FDM
- **THEN** filament menu 項目 SHALL 正常出現

---

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

