## 背景

`SupportTreeConfig` 位於 `src/libslic3r/SLA/SupportTree.hpp`，包含兩個 static constexpr 常數控制 NLopt 最佳化器的停止條件：

```cpp
// PhrozenOrca 現況
static const double constexpr optimizer_rel_score_diff = 1e-6;
static const unsigned constexpr optimizer_max_iterations = 1000;

// PrusaSlicer 對應值
static const double constexpr optimizer_rel_score_diff = 1e-10;
static const unsigned constexpr optimizer_max_iterations = 2000;
```

這兩個常數被傳入 `SupportTreeBuildsteps.cpp:20-21` 的 NLopt 設定鏈，影響以下兩個最佳化流程：
1. **Pinhead 位置最佳化**：在 `filter()` 中，當接頭碰撞時用 `AlgNLoptGenetic` 搜尋最佳角度
2. **地面路徑最佳化**：在 `create_ground_pillar()` 中，用 `AlgNLoptGenetic` 搜尋橋接方向

## 目標 / 非目標

**目標：**
- 對齊 PrusaSlicer 的最佳化器收斂精度
- 提升 SLA 支撐接觸點與橋接方向的定位品質

**非目標：**
- 修改任何演算法邏輯
- 修改任何資料結構或 API
- 影響 FDM 支撐或任何非 SLA 功能

## 決策

### D1：直接修改常數值，不加任何條件

這是純數值修改，不需要任何 SLA/FDM 分支判斷（`SupportTreeConfig` 本身只用於 SLA）。直接改兩行常數即可。

**考慮過的替代方案**：將精度設為可設定的 config 參數。已拒絕——這是內部實作細節，不應暴露給使用者；PrusaSlicer 也是 hardcode。

## 風險 / 取捨

- **執行時間增加**：迭代次數 2x、精度 10,000x 提升，最佳化每次可能多花時間。→ 兩個最佳化皆在支撐點數量範圍內執行（通常數十至數百個點），每個點獨立執行，整體影響可接受。實際影響取決於碰撞頻率。
- **結果可能改變**：現有 3mf 檔案重新切片後支撐點位置可能有微小位移。→ 屬預期行為（品質提升），不需遷移。
