# Step B：SLA 支撐密度修正 — 執行結果

**完成日期**: 2026-03-04
**分析文件**: `MergeLog/StepAnalyze/StepB_SupportDensityFix_Analyze.md`
**狀態**: ✅ 已完成（B1-1a, B1-1b, B1-2）

---

## 問題背景

Step A 完成後（Voronoi + NearPoints KD-tree 架構），PhrozenOrca 產生的支撐點仍比 PrusaSlicer 更密集，且每根支撐柱連接的 head 數量偏多。

分析確認：
- 支撐樹算法（pillar 分群、max_bridges_on_pillar）與 PrusaSlicer **完全相同**
- 「每柱連接更多點」是「點總數偏多」的因果結果，非獨立 bug
- 根本原因為 `SLAPrintSteps.cpp` 中兩個配置錯誤

---

## 修改內容

**修改檔案**：`src/libslic3r/SLAPrintSteps.cpp`

### B1-1a：新增 SampleConfigFactory include

```diff
+#include "libslic3r/SLA/SupportIslands/SampleConfigFactory.hpp"
```

**原因**：`SampleConfigFactory` 定義在 `SupportIslands/` 子目錄，
`SLAPrintSteps.cpp` 原本未 include，無法使用 `apply_density()` 和 `create()`。

**注意**：使用 `""` 引號格式（與 `SupportPointGenerator.cpp` line 23 一致）。

---

### B1-1b：island_configuration 套用 apply_density()

```diff
-config.island_configuration = sla::create_default_island_configuration(config.head_diameter);
+config.island_configuration = sla::SampleConfigFactory::apply_density(
+    sla::SampleConfigFactory::create(config.head_diameter),
+    config.density_relative);
```

**原因**：
- `create_default_island_configuration()` 等同於 `SampleConfigFactory::create()`，但**不套用 density 縮放**
- PrusaSlicer（`SLAPrintSteps.cpp` line 762）正確使用 `apply_density(create(...), density_relative)`
- PhrozenOrca 缺少此步驟，導致 density 設定值對 island 採樣間距無效

**效果**：
- density = 100%：`apply_density()` 為 no-op，行為不變
- density < 100%：採樣間距增大 → 支撐點數量減少（正確行為）
- density > 100%：採樣間距縮小 → 支撐點數量增加（正確行為）

**Namespace 注意**：`SampleConfigFactory` 位於 `Slic3r::sla` namespace，
程式碼在 `Slic3r` namespace，需明確使用 `sla::SampleConfigFactory::` 前綴。

---

### B1-2：allowed_move 改為層高計算

```diff
-const double allowed_move = double(cfg.support_head_front_diameter);
+const double allowed_move = (po.m_model_height_levels.size() > 1)
+    ? double(po.m_model_height_levels[1] - po.m_model_height_levels[0])
+      + double(std::numeric_limits<float>::epsilon())
+    : double(cfg.support_head_front_diameter); // fallback for single-layer
```

**原因**：
- `move_on_mesh_surface()` 使用 `allowed_move` 判斷射線投影的容許移動量
- PhrozenOrca 原用 `head_front_diameter = 0.4mm`（過大）
- PrusaSlicer 使用層高（0.025 ~ 0.1mm）+ float epsilon
- 0.4mm 容許範圍使支撐點可能投影到非預期的相鄰網格面（尤其薄壁處）

**效果**：投影更精確貼合同一層切面所在的網格面，減少誤投影

---

## 修改統計

| 步驟 | 修改說明 | +行 | -行 |
|------|---------|:---:|:---:|
| B1-1a | 新增 include | +1 | 0 |
| B1-1b | island_configuration apply_density | +3 | -1 |
| B1-2 | allowed_move 改為層高 | +4 | -1 |
| **合計** | 1 個檔案 | **+8** | **-2** |

---

## FDM 安全性確認

| 修改 | FDM 影響 |
|------|:--------:|
| 新增 SampleConfigFactory include | ✅ 僅增加 header，無執行時影響 |
| apply_density() 在 island_configuration | ✅ 只在 `supports_enable == true` 且 `sla_points_status != UserModified` 路徑執行 |
| allowed_move 改為層高 | ✅ 只在 `support_points()` SLA-only step 內 |

---

## Git Commit

```
[Phase B] Step B1: SLA 支撐密度修正 — 套用 density 縮放 + 修正 allowed_move
```

**Commit hash**: （見下方）

---

## 預期驗證測試

| # | 測試 | 預期結果 |
|---|------|---------|
| T1 | density=100%，比對 PrusaSlicer | 點數相近，分布均勻 |
| T2 | density=50%，比對 PrusaSlicer density=50% | PhrozenOrca 點數也對應減少 |
| T3 | density=150%，比對 PrusaSlicer density=150% | PhrozenOrca 點數也對應增加 |
| T4 | 支撐柱連接點數 | 點數正常後，每柱連接數自然正常 |
| T5 | FDM 切片 | 行為完全不變 |
