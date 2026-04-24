## Why

`support_critical_angle` 目前在 `filterfn` 對所有支撐點無差別套用角度門檻，包含使用者手動放置的點（`SupportPointType::manual_add`），導致手動支撐若放在傾斜度不足的面上，會被靜默丟棄而無法生成。此參數的語義應僅限制自動支撐；手動支撐代表使用者明確的幾何覆寫，一律應強制嘗試生成。

## What Changes

- 修改 `SLA/SupportTreeBuildsteps.cpp::filterfn()` 的角度門檻邏輯，使 `SupportPointType::manual_add` 的點繞過 `overhang_angle_threshold` 過濾
- 自動支撐（`island`、`slope` 及未來新增類型）行為不變，仍受 `support_critical_angle` 約束
- 手動點仍受 `normal_cutoff_angle` 幾何合理性保護（朝上平面不可生成向下支撐柱）
- 透過拖曳升格（drag-to-promote）為 `manual_add` 的點同樣享有此豁免

## Capabilities

### New Capabilities

- `sla-manual-support-angle-bypass`: 定義手動支撐點繞過 `overhang_angle_threshold` 過濾的行為規格，包含與 `normal_cutoff_angle` 的交互語義、對兩種支撐模式（Standard / Branching）的適用範圍，及已知限制（cluster 去重、optimizer 失敗）

### Modified Capabilities

<!-- 本次為純實作修正，無現有 spec 層級的行為需求變更 -->

## Impact

- **修改檔案：** `src/libslic3r/SLA/SupportTreeBuildsteps.cpp` line 712（唯一改動點）
- **參考但不修改：** `src/libslic3r/SLAPrintSteps.cpp`（自動支撐過濾路徑）、`src/libslic3r/SLA/SupportPoint.hpp`（type enum 定義）、`src/libslic3r/SLAPrint.cpp`（config 映射）
- **影響範圍：** SLA Standard Support 與 Branching Support 兩種模式（共用 `filterfn`）
- **無 API 或檔案格式變更**
