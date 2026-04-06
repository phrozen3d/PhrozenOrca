## 為何

PhrozenOrca 的 SLA 支撐樹最佳化器精度常數（`optimizer_rel_score_diff = 1e-6`，`optimizer_max_iterations = 1000`）遠低於 PrusaSlicer 的對應值（`1e-10`，`2000`），導致 pinhead 接觸點位置與橋接方向的最佳化品質較差，可能陷入局部最優而非全局最優解。

## 變更內容

- 將 `SupportTreeConfig::optimizer_rel_score_diff` 從 `1e-6` 改為 `1e-10`
- 將 `SupportTreeConfig::optimizer_max_iterations` 從 `1000` 改為 `2000`

## 功能範疇

### 新增功能
- `optimizer-precision`: SLA 支撐樹最佳化器以更高精度收斂，pinhead 與橋接方向定位更精準

### 修改功能
（無 spec 層級的行為變更，僅為內部精度提升）

## 影響範圍

**修改的檔案：**
- `src/libslic3r/SLA/SupportTree.hpp` — 修改兩個 static constexpr 常數

**不需修改：**
- 任何 API、資料結構、檔案格式
- FDM 程式碼
- PhrozenOrca 自訂功能
