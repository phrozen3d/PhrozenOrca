# Spec: SLA Manual Support Angle Bypass

## Purpose

手動放置的 SLA 支撐點不應受 `support_critical_angle`（overhang angle threshold）過濾影響，使使用者能在任何傾斜角度的面上強制生成支撐柱。自動支撐點的行為維持不變，仍受角度門檻約束。

## Requirements

### Requirement: Manual support points bypass overhang angle threshold
手動放置的支撐點（`SupportPointType::manual_add`）在 `filterfn` 中 SHALL 繞過 `overhang_angle_threshold` 過濾，無論其所在面的傾斜角度為何，皆強制進入幾何計算引擎嘗試生成支撐柱。

自動支撐點（`island`、`slope` 及任何未來新增類型）的行為 SHALL NOT 改變，仍受 `support_critical_angle` 設定值約束。

#### Scenario: Manual point on shallow slope generates support
- **WHEN** 使用者在傾斜角度低於 `support_critical_angle` 的面上手動放置支撐點
- **THEN** 切片後該點 SHALL 進入幾何引擎嘗試生成支撐柱（不被角度門檻靜默丟棄）

#### Scenario: Auto point on shallow slope is still filtered
- **WHEN** 演算法在傾斜角度低於 `support_critical_angle` 的面上自動生成支撐點
- **THEN** 該點 SHALL 被角度過濾丟棄，行為與修改前相同

#### Scenario: Both Standard and Branching Support modes apply the bypass
- **WHEN** 使用者手動放置支撐點，且支撐模式為 Standard Support 或 Branching Support
- **THEN** 兩種模式下手動點皆 SHALL 繞過角度過濾（共用同一 `filterfn` 咽喉點）

---

### Requirement: Geometry sanity check retained for all point types
`normal_cutoff_angle` 幾何合理性檢查 SHALL 無條件適用於所有支撐點，包含 `manual_add`。朝上平面（極角過小，方向不允許向下支撐柱）的點 SHALL 被靜默跳過，無論其為手動或自動。

#### Scenario: Manual point on upward-facing surface is not generated
- **WHEN** 使用者在朝上的面（極角 < `normal_cutoff_angle` 門檻）上放置手動支撐點
- **THEN** 該點 SHALL 被 `normal_cutoff_angle` 過濾丟棄，不生成支撐柱

---

### Requirement: Drag-to-promote points receive the same bypass
使用者透過拖曳將自動點升格為手動點（Gizmo drag-to-promote，type 升格為 `manual_add`）的點，其行為 SHALL 與全新手動放置的點完全等價，同樣繞過 `overhang_angle_threshold` 過濾。

#### Scenario: Dragged point on shallow slope generates support
- **WHEN** 使用者拖曳一個自動點到傾斜角度低於 `support_critical_angle` 的位置
- **THEN** 該點 SHALL 進入幾何引擎嘗試生成支撐柱
