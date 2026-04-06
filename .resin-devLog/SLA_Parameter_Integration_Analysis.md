# SLA 參數整合分析報告

**建立日期**: 2026-03-22
**分析範圍**: PhrozenOrca ↔ PrusaSlicer ↔ OrcaSlicer 三方 SLA 參數比較
**參考案例**: `supports_enable` → `generate_support` 整合

---

## 一、PhrozenOrca 新參數的位置問題

### 現況說明

PhrozenOrca 將約 **51 個 LCD 專屬 SLA 參數**放置在 `PrintConfig`（原 FDM 大型 config class）的尾端。
這個做法有以下問題：

1. **FDM 與 SLA 參數混雜**：`PrintConfig` 是 FDM config struct，SLA 參數夾雜在其中難以維護
2. **SLAPrint.cpp 讀不到**：SLAPrint.cpp 使用 `m_config`（型別 `SLAPrintObjectConfig`），PrintConfig 裡的參數無法直接透過成員存取
3. **重複定義**：部分參數同時存在 PrintConfig 與正確的 SLA config class

### 建議分類規則

| 參數性質 | 應放在的 Struct |
|---|---|
| 列印設定（支撐、底座、層高）| `SLAPrintObjectConfig` |
| 材料設定（曝光時間、升降速度、PWM）| `SLAMaterialConfig` |
| 印表機硬體（解析度、顯示器尺寸）| `SLAPrinterConfig` |
| FDM + SLA 共用 | `PrintConfig`（少數情況） |

### 誤置參數清單

| 參數 | 目前位置 | 應移至 | 說明 |
|---|---|---|---|
| `bottom_layer_count` | PrintConfig | `SLAPrintObjectConfig` | 底層層數，屬列印設定 |
| `exposure_time` | PrintConfig | `SLAMaterialConfig` | 曝光時間，屬材料設定，且與 SLAMaterialConfig 重複 |
| `bottom_exposure_time` | PrintConfig | `SLAMaterialConfig` | 底層曝光時間，同上 |
| `transition_layer_count` | PrintConfig | `SLAPrintObjectConfig` | 漸層層數 |
| `transition_type` | PrintConfig | `SLAPrintObjectConfig` | 漸層類型 |
| `transition_layer_interval_time_difference` | PrintConfig | `SLAPrintObjectConfig` | 漸層時間差 |
| `waiting_mode_during_printing` | PrintConfig | `SLAPrintObjectConfig` | 等待模式 |
| `rest_time_before_lift` | PrintConfig | `SLAMaterialConfig` | 抬升前等待（已被 AnycubicSLA.cpp 讀取）|
| `rest_time_after_lift` | PrintConfig | `SLAMaterialConfig` | 抬升後等待 |
| `rest_time_after_retract` | PrintConfig | `SLAMaterialConfig` | 縮回後等待 |
| `bottom_lift_distance` | PrintConfig | `SLAMaterialConfig` | 底層抬升距離 |
| `lifting_distance` | PrintConfig | `SLAMaterialConfig` | 抬升距離 |
| `lift_second_distance` 等 | PrintConfig | `SLAMaterialConfig` | 二段式升降參數 |
| `bottom_lift_speed` / `lifting_speed` 等 | PrintConfig | `SLAMaterialConfig` | 升降速度 |
| `retract_distance` / `retract_speed` 等 | PrintConfig | `SLAMaterialConfig` | 縮回參數 |
| `bottom_light_pwm` / `light_pwm` | PrintConfig | `SLAMaterialConfig` | 光源強度 |
| `picture_grayscale` / `gray_scale_level` | PrintConfig | `SLAPrinterConfig` | 印表機顯示設定 |
| `anti_aliasing`（重複） | PrintConfig + SLAPrinterConfig | `SLAPrinterConfig` 僅保留一份 | **重複**，PrintConfig 那份須移除 |
| `image_blur_enable` / `image_blur_pixel` | PrintConfig | `SLAPrinterConfig` | 影像模糊設定 |
| `anti_aliasing_level` | PrintConfig | `SLAPrinterConfig` | 反鋸齒等級 |
| `shrinkage_compensation*` | PrintConfig | `SLAPrintObjectConfig` | 收縮補償 |
| `tolerance_compensation*` | PrintConfig | `SLAPrintObjectConfig` | 公差補償 |
| `top_upper_diameter` / `top_contact_depth` / `pinhead_width` | PrintConfig | `SLAPrintObjectConfig` | 支撐頭幾何 |
| `pillar_diameter` / `support_bracing_angle` 等 | PrintConfig | `SLAPrintObjectConfig` | 支撐柱參數 |
| `pad_wall_slope_sla` | PrintConfig | `SLAPrintObjectConfig` | 底座坡度（已與 PrusaSlicer `pad_wall_slope` 衝突）|
| `support_points_density` | PrintConfig | `SLAPrintObjectConfig` | 支撐點密度 |
| `max_bridge_length_sla` / `max_pillar_linking_distance` | PrintConfig | `SLAPrintObjectConfig` | 支撐結構參數 |
| `area_fill`（重複）| PrintConfig + SLAMaterialConfig + SLAPrinterConfig | `SLAPrinterConfig` 僅保留一份 | **三重重複** |

---

## 二、三方參數比較

---

### Group 1：只有 PhrozenOrca 新增（無對應 PrusaSlicer 參數）

> 這些參數目前**大部分只在 AnycubicSLA.cpp（PhrozenPRZ 匯出）有讀取**，
> 未連接到核心 SLA 切片演算法（SLAPrintSteps.cpp）。
> 要讓切片結果反映這些設定，需要確認 AnycubicSLA/PhrozenPRZ 的讀取路徑是否正確。

| 參數 | 正確位置 | 已連接切片？ | 說明 |
|---|---|---|---|
| `transition_layer_count` | `SLAPrintObjectConfig` | ❌ 未連接 | 漸層層數 |
| `transition_type` | `SLAPrintObjectConfig` | ❌ 未連接 | 漸層類型 |
| `transition_layer_interval_time_difference` | `SLAMaterialConfig` | ❌ 未連接 | 漸層時間差 |
| `waiting_mode_during_printing` | `SLAMaterialConfig` | ❌ 未連接 | 等待模式 |
| `rest_time_before_lift` | `SLAMaterialConfig` | ✅ AnycubicSLA.cpp | 已讀取 |
| `rest_time_after_lift` | `SLAMaterialConfig` | ✅ AnycubicSLA.cpp | 已讀取 |
| `rest_time_after_retract` | `SLAMaterialConfig` | ✅ AnycubicSLA.cpp | 已讀取 |
| `bottom_lift_distance` | `SLAMaterialConfig` | ✅ AnycubicSLA.cpp | 已讀取 |
| `bottom_lift_second_distance` | `SLAMaterialConfig` | ❌ 未連接 | 二段抬升 |
| `lifting_distance` | `SLAMaterialConfig` | ✅ AnycubicSLA.cpp | 已讀取 |
| `lift_second_distance` | `SLAMaterialConfig` | ❌ 未連接 | 二段抬升 |
| `bottom_retract_distance` | `SLAMaterialConfig` | ❌ 未連接 | |
| `bottom_retract_second_distance` | `SLAMaterialConfig` | ❌ 未連接 | |
| `retract_distance` | `SLAMaterialConfig` | ✅ AnycubicSLA.cpp | 已讀取 |
| `retract_second_distance` | `SLAMaterialConfig` | ❌ 未連接 | |
| `bottom_lift_speed` | `SLAMaterialConfig` | ✅ AnycubicSLA.cpp | 已讀取 |
| `bottom_lift_second_speed` | `SLAMaterialConfig` | ❌ 未連接 | |
| `lifting_speed` | `SLAMaterialConfig` | ✅ AnycubicSLA.cpp | 已讀取 |
| `lift_second_speed` | `SLAMaterialConfig` | ❌ 未連接 | |
| `bottom_retract_speed` | `SLAMaterialConfig` | ❌ 未連接 | |
| `bottom_retract_second_speed` | `SLAMaterialConfig` | ❌ 未連接 | |
| `retract_speed` | `SLAMaterialConfig` | ✅ AnycubicSLA.cpp | 已讀取 |
| `retract_second_speed` | `SLAMaterialConfig` | ❌ 未連接 | |
| `bottom_light_pwm` | `SLAMaterialConfig` | ✅ AnycubicSLA.cpp | 已讀取 |
| `light_pwm` | `SLAMaterialConfig` | ✅ AnycubicSLA.cpp | 已讀取 |
| `picture_grayscale` | `SLAPrinterConfig` | ❌ 未連接 | |
| `gray_scale_level` | `SLAPrinterConfig` | ❌ 未連接 | |
| `image_blur_enable` | `SLAPrinterConfig` | ❌ 未連接 | |
| `image_blur_pixel` | `SLAPrinterConfig` | ❌ 未連接 | |
| `anti_aliasing_level` | `SLAPrinterConfig` | ❌ 未連接 | |
| `shrinkage_compensation*`（4 個）| `SLAPrintObjectConfig` | ❌ 未連接 | |
| `tolerance_compensation*`（4 個）| `SLAPrintObjectConfig` | ❌ 未連接 | |
| `bottom_tolerance_compensation*`（3 個）| `SLAPrintObjectConfig` | ❌ 未連接 | |
| `top_upper_diameter` | `SLAPrintObjectConfig` | ❌ 未連接 | |
| `top_contact_depth` | `SLAPrintObjectConfig` | ❌ 未連接 | |
| `pinhead_width` | `SLAPrintObjectConfig` | ❌ 未連接 | |
| `pillar_diameter` | `SLAPrintObjectConfig` | ❌ 未連接（PrusaSlicer 用 `support_pillar_diameter`）|
| `support_bracing_angle` | `SLAPrintObjectConfig` | ❌ 未連接 | |
| `support_bottom_diameter` | `SLAPrintObjectConfig` | ❌ 未連接 | |
| `support_boss_height` | `SLAPrintObjectConfig` | ❌ 未連接 | |
| `support_points_density` | `SLAPrintObjectConfig` | ❌ 未連接 | PrusaSlicer 用 `support_points_density_relative` |
| `max_bridge_length_sla` | `SLAPrintObjectConfig` | ❌ 未連接 | PrusaSlicer 用 `support_max_bridge_length` |
| `max_pillar_linking_distance` | `SLAPrintObjectConfig` | ❌ 未連接 | PrusaSlicer 用 `support_max_pillar_link_distance` |
| `generate_support` | `SLAPrintObjectConfig` | ✅ SLAPrint.cpp | 已整合（原 `supports_enable`）|

> **結論**：AnycubicSLA.cpp 已正確讀取部分 Phrozen 專屬參數用於 Phrozen 格式匯出。
> 尚未連接的參數中，「支撐幾何類」（pillar_diameter 等）需要和 SLA 支撐算法整合；
> 「公差補償/收縮補償類」需要在 SLAPrintSteps rasterize 步驟中讀取。

---

### Group 2：PhrozenOrca 有，PrusaSlicer 也有，需要整合

| PhrozenOrca 參數 | PhrozenOrca 位置 | PrusaSlicer 參數 | PrusaSlicer 位置 | 問題類型 | 建議動作 |
|---|---|---|---|---|---|
| `generate_support` | SLAPrintObjectConfig | `supports_enable` | SLAPrintObjectConfig | **已完成整合** ✅ | — |
| `pad_wall_slope_sla` | PrintConfig | `pad_wall_slope` | SLAPrintObjectConfig | 命名衝突 + 位置錯誤 | 移至 SLAPrintObjectConfig，統一命名 |
| `support_points_density` | PrintConfig | `support_points_density_relative` | SLAPrintObjectConfig | 命名不同，概念相近 | 確認語意是否相同，決定保留哪個 |
| `max_bridge_length_sla` | PrintConfig | `support_max_bridge_length` | SLAPrintObjectConfig | 命名不同，位置錯誤 | 移至 SLAPrintObjectConfig，統一命名 |
| `max_pillar_linking_distance` | PrintConfig | `support_max_pillar_link_distance` | SLAPrintObjectConfig | 命名不同，位置錯誤 | 移至 SLAPrintObjectConfig，統一命名 |
| `pillar_diameter` | PrintConfig | `support_pillar_diameter` | SLAPrintObjectConfig | 命名不同，位置錯誤 | 移至 SLAPrintObjectConfig，統一命名 |
| `anti_aliasing`（×2）| PrintConfig 和 SLAPrinterConfig | `anti_aliasing` | SLAPrinterConfig | **重複定義** | 移除 PrintConfig 那份 |
| `area_fill`（×3）| PrintConfig、SLAMaterialConfig、SLAPrinterConfig | `area_fill` | SLAPrinterConfig（被 comment）| **三重重複** | 保留 SLAPrinterConfig 一份，移除其餘 |
| `exposure_time`（×2）| PrintConfig 和 SLAMaterialConfig | `exposure_time` | SLAMaterialConfig | **重複定義** | 移除 PrintConfig 那份 |
| `bottom_layer_count` | PrintConfig | `faded_layers` | SLAPrintObjectConfig | 命名不同，概念相同 | 確認是否語意完全等價，整合或對應 |
| `material_ow_*`（10 個）| SLAMaterialConfig | `material_ow_*`（8 個）| SLAMaterialConfig | 數量不同 + 型別不同 | PhrozenOrca 用 Float，PrusaSlicer 用 FloatNullable；決定是否要改型別 |
| Tilt 參數（13 個）| SLAMaterialConfig（Int）| Tilt 參數（13 個）| SLAMaterialConfig（Enum 模板）| 型別不同 | PhrozenOrca 用 `ConfigOptionInts` 作為繞過模板限制的 workaround；確認是否可接受 |
| `printable_area` | SLAPrinterConfig | `bed_shape` | SLAPrinterConfig | 命名不同，功能相同 | 決定是否要對應 PrusaSlicer 命名 |
| `filename_format` | SLAPrintConfig | `output_filename_format` | SLAPrintConfig | 命名不同 | 確認是否需要統一 |

---

### Group 3：PrusaSlicer / OrcaSlicer 有，PhrozenOrca 缺少

| PrusaSlicer 參數 | PrusaSlicer 位置 | 優先度 | 說明 |
|---|---|---|---|
| `support_points_minimal_distance` | SLAPrintObjectConfig | ⚠️ 高 | 支撐點最小間距，SLA 支撐算法重要參數 |
| `max_print_height` | SLAPrinterConfig | 中 | 印表機 Z 軸最大高度限制 |
| `printer_model` | SLAPrinterConfig | 中 | 機器型號識別，用於 config 繼承和 profile 匹配 |
| `supports_enable`（作為整合後的名稱）| SLAPrintObjectConfig | ✅ 已以 `generate_support` 整合 | — |

---

## 三、整合建議優先順序

### P1：影響切片正確性（高優先）

1. **移除重複定義**：`anti_aliasing`、`area_fill`、`exposure_time` 從 PrintConfig 移除，只保留正確 struct 版本
2. **`pad_wall_slope_sla` → `pad_wall_slope`**：命名衝突且影響 pad 生成，SLAPrint.cpp 已在讀 SLAPrintObjectConfig 版本
3. **將「已連接 AnycubicSLA.cpp」的參數移至正確 struct**：確保 AnycubicSLA.cpp 讀取路徑不因 struct 移動而中斷

### P2：維護性改善（中優先）

4. **`max_bridge_length_sla` → `support_max_bridge_length`**
5. **`max_pillar_linking_distance` → `support_max_pillar_link_distance`**
6. **`support_points_density` 與 `support_points_density_relative` 確認整合**
7. **`bottom_layer_count` vs `faded_layers` 確認語意統一**

### P3：功能補齊（低優先）

8. 新增 `support_points_minimal_distance` 到 PhrozenOrca
9. 新增 `max_print_height` 到 PhrozenOrca SLAPrinterConfig
10. 連接未接通的 Phrozen 專屬參數到 Rasterizer（shrinkage_compensation、公差補償等）

---

## 相關檔案路徑

| 檔案 | 關鍵行數 |
|---|---|
| `PhrozenOrca/src/libslic3r/PrintConfig.hpp` | 1332-1556（SLA in PrintConfig）、1574-1741（SLAPrintObjectConfig）、1745-1828（SLAMaterialConfig、SLAPrinterConfig）|
| `PhrozenOrca/src/libslic3r/PrintConfig.cpp` | `init_sla_params()` 起始 line 6539 |
| `PhrozenOrca/src/libslic3r/SLAPrint.cpp` | line 32-135（config 存取）|
| `PhrozenOrca/src/libslic3r/SLAPrintSteps.cpp` | rasterize 步驟 |
| `PhrozenOrca/src/libslic3r/Format/AnycubicSLA.cpp` | line 300-376（Phrozen 格式讀取參數）|
| `PhrozenOrca/src/libslic3r/Format/PhrozenPRZ.cpp` | Phrozen PRZ 格式匯出 |
| `PhrozenOrca/src/slic3r/GUI/Tab.hpp` | `TabSLAPrint`（line 654）、`TabSLAMaterial`（line 638）— 實際使用的 SLA UI Tab |
| `PhrozenOrca/src/slic3r/GUI/Tab.cpp` | TabSLAPrint / TabSLAMaterial 實作 |
| `PrusaSlicer/src/libslic3r/PrintConfig.hpp` | 1029-1326（SLA config classes）|
| `PrusaSlicer/src/libslic3r/PrintConfig.cpp` | `init_sla_params()` 起始 line 4266 |

> **注意**：`PhrozenLCDTab.hpp` / `PhrozenLCDTab.cpp` 已移除。
> 實際運作的 SLA UI Tab 為 `TabSLAPrint` 和 `TabSLAMaterial`（定義於 `Tab.hpp`，由 `MainFrame.cpp` 實例化）。
