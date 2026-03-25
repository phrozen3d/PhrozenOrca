# SLA 支撐角度參數重構說明

> 移植來源：`D:\repos\web_slicer_core\third_party\prusaslicer_fork` commit `1d5d4c3`
> 實作日期：2026-03-25
> 分支：`phrozen-resin-dev`

---

## 一、背景與動機

### 原始問題

`support_critical_angle` 原本同時控制兩個語意不同的功能：

1. **支撐柱橋接坡度（bridge slope）**：兩個 pillar 之間橋接的最大角度
2. **懸空面過濾閾值（overhang filter）**：決定哪些表面需要放置支撐點

這造成語意混亂：調整「要在哪些面放支撐」會意外改變「支撐柱的橋接角度」。

### 修改目標

拆分職責：

| 參數 | 控制的功能 | 預設值 |
|------|-----------|--------|
| `support_bracing_angle` | 支撐柱橋接坡度（bridge slope） | 45° |
| `support_critical_angle` | 懸空面過濾閾值（overhang angle threshold） | 0°（所有懸空面都產生支撐） |

---

## 二、座標系說明

SLA 支撐引擎使用球面座標系（spherical coordinates）：

- `polar = π`（180°）→ 法線朝正下方（底面）
- `polar = π/2`（90°）→ 法線朝水平（垂直面）
- `polar = 0` → 法線朝正上方（頂面）

**懸空面**：`polar` 接近 `π` 的面（法線朝下，面朝上）

過濾條件公式（**使用加號**，已從原始 diff 驗證）：

```cpp
// 拒絕 polar 不夠大的點（表面不夠懸空）
if (polar < M_PI / 2.0 + threshold) return;
```

| `threshold`（overhang_angle_threshold） | 等效條件 | 效果 |
|-----------------------------------------|---------|------|
| `0`（0°，**預設**） | `polar < π/2` | 所有懸空面（polar ≥ π/2）都產生支撐 |
| `π/4`（45°） | `polar < 3π/4` | 只對懸空角 ≥ 45° 的面產生支撐 |
| `π/2`（90°） | `polar < π` | 幾乎不過濾（向後相容舊行為） |

---

## 三、修改的檔案清單

### 3.1 `src/libslic3r/SLA/SupportTree.hpp`

新增 `overhang_angle_threshold` 欄位至 `SupportTreeConfig`：

```cpp
// 原有
double bridge_slope = M_PI/4;

// 新增
// Overhang angle threshold in radians (measured from horizontal plane).
// Support heads will not be placed on surfaces whose angle from horizontal
// exceeds this value. PI/2 means all overhangs get support (no filtering).
double overhang_angle_threshold = M_PI / 2;
```

### 3.2 `src/libslic3r/PrintConfig.hpp`

在 `SLAPrintObjectConfig` macro 新增兩個欄位：

```cpp
// support_critical_angle 之後
((ConfigOptionFloat, support_bracing_angle))/*= 45*/

// branchingsupport_critical_angle 之後
((ConfigOptionFloat, branchingsupport_bracing_angle))/*= 45*/
```

> **注意**：`PrintConfig::support_bracing_angle`（行 ~1545，LCD 光柵參數）與
> `SLAPrintObjectConfig::support_bracing_angle` 是不同 C++ 類別的成員，
> `SLAFullPrintConfig` 不繼承 `PrintConfig`，無 C++ 衝突。

### 3.3 `src/libslic3r/PrintConfig.cpp`

**變更 A**（`init_sla_support_params` 函式，行 ~6354，對應 Branching/Organic Tree）：
- `branchingsupport_critical_angle` 預設值：`45` → `0`（所有懸空面都產生支撐）
- 新增 `branchingsupport_bracing_angle` 定義（預設 45°）

**變更 B**（非前綴 `support_critical_angle` 定義，行 ~7523，對應 Default Tree）：
- 預設值 `0`（所有懸空面都產生支撐，`overhang_angle_threshold = 0`）

> **注意**：`init_sla_support_params(prefix)` 只以 `prefix="branching"` 呼叫，
> 因此 `prefix + "support_bracing_angle"` 只會生成 `"branchingsupport_bracing_angle"` 一個 key。

### 3.4 `src/libslic3r/SLAPrint.cpp`

**`make_support_cfg` 函式** — 拆分單一賦值為兩個：

```cpp
// Default Tree（原）
scfg.bridge_slope = c.support_critical_angle.getFloat() * PI / 180.0;

// Default Tree（新）
scfg.bridge_slope             = c.support_bracing_angle.getFloat() * PI / 180.0;
scfg.overhang_angle_threshold = c.support_critical_angle.getFloat() * PI / 180.0;

// Branching/Organic Tree（原）
scfg.bridge_slope = c.branchingsupport_critical_angle.getFloat() * PI / 180.0;

// Branching/Organic Tree（新）
scfg.bridge_slope             = c.branchingsupport_bracing_angle.getFloat() * PI / 180.0;
scfg.overhang_angle_threshold = c.branchingsupport_critical_angle.getFloat() * PI / 180.0;
```

**`invalidate_state_by_config_options` 函式** — 新增三個觸發 key（修正既有 bug：`branchingsupport_critical_angle` 原本從未被監聽）：

```cpp
|| opt_key == "support_critical_angle"
|| opt_key == "support_bracing_angle"         // 新增
// ...
|| opt_key == "branchingsupport_critical_angle"  // 補上既有 bug
|| opt_key == "branchingsupport_bracing_angle"   // 新增
```

### 3.5 `src/libslic3r/SLA/SupportTreeBuildsteps.cpp`

在 `filter()` 的 `filterfn` lambda（行 ~682）插入懸空過濾：

```cpp
// 原有（sanity check）
if (polar < PI - m_cfg.normal_cutoff_angle) return;

// 新增（overhang filter）
// skip if the surface is not steep enough to need support
// overhang_angle_threshold measured from horizontal: 0=only flat, PI/2=all overhangs
if (polar < M_PI / 2.0 + m_cfg.overhang_angle_threshold) return;

// 原有（saturate）
polar = std::max(polar, PI - m_cfg.bridge_slope);
```

### 3.6 `src/libslic3r/SLA/SupportTreeUtils.hpp`

在 `optimize_pinhead_placement`（行 ~274）插入相同過濾：

```cpp
if (polar < PI - m.cfg.normal_cutoff_angle) return false;

// 新增
if (polar < M_PI / 2.0 + m.cfg.overhang_angle_threshold) return false;

polar = std::max(polar, PI - m.cfg.bridge_slope);
```

### 3.7 `src/libslic3r/Preset.cpp`

在 `s_Preset_sla_print_options` 向量新增 1 個 key：

```cpp
"branchingsupport_critical_angle",
"branchingsupport_bracing_angle",   // 新增
```

> `"support_bracing_angle"` 已在行 ~973 存在（LCD 光柵用途共用此 key），不重複新增。

### 3.8 `src/slic3r/GUI/ConfigManipulation.cpp`

在 `toggle_print_sla_options` 新增 1 個 toggle：

```cpp
toggle_field("support_bracing_angle", is_support);
toggle_field("branchingsupport_bracing_angle", is_support);  // 新增
```

### 3.9 `src/libslic3r/SLAPrintSteps.cpp`（額外修正）

**問題**：UI 顯示的支撐點（`SupportPointGenerator` 輸出）與支撐結構（`SupportTreeBuildsteps::filterfn` 過濾後）不一致，因兩者使用不同的過濾邏輯。

**解法**：在 `support_points()` 的 Phase 2 之後新增 Phase 3 過濾，使用相同的 `support_critical_angle` 閾值：

```cpp
// Phase 3: Filter support points by overhang angle threshold,
// matching the same condition used in SupportTreeBuildsteps::filterfn.
{
    const double critical_angle = cfg.support_critical_angle.getFloat() * PI / 180.0;
    if (critical_angle < M_PI / 2.0) {  // guard: 0° 預設時 critical_angle=0 < π/2，會執行過濾
        // 計算法線 → dir_to_spheric → 過濾 polar < π/2 + critical_angle 的點
    }
}
```

新增 includes：
```cpp
#include <libslic3r/SLA/SupportTreeBuildsteps.hpp>   // dir_to_spheric
#include <libslic3r/SLA/IndexedMesh.hpp>              // sla::normals
```

---

## 四、向後相容性

| 情境 | 結果 |
|------|------|
| 舊 profile 不含 `support_bracing_angle` | 使用預設值 45°，`bridge_slope = π/4`，與原 `support_critical_angle = 45°` 相同 |
| 舊 profile 不含 `support_critical_angle`（分支版）| 使用預設值 0°，`overhang_angle_threshold = 0`，過濾條件 `polar < π/2`，所有懸空面都產生支撐 |
| `support_critical_angle = 0°`（預設） | Phase 3 執行（`0 < π/2`），過濾 `polar < π/2` 的點（即非懸空面） |
| `support_critical_angle = 90°` | Phase 3 guard 短路（`π/2 < π/2` 為 false），不執行額外計算 |

---

## 五、已修正的既有 Bug

`branchingsupport_critical_angle` 原本從未加入 `invalidate_state_by_config_options`，導致在 UI 修改此值後不會觸發 `slaposSupportTree` 重算。本次一併修正。

---

## 六、關鍵檔案索引

| 檔案 | 修改說明 |
|------|---------|
| [SLA/SupportTree.hpp](../src/libslic3r/SLA/SupportTree.hpp) | 新增 `overhang_angle_threshold` 欄位 |
| [PrintConfig.hpp](../src/libslic3r/PrintConfig.hpp) | 新增 2 個 ConfigOption |
| [PrintConfig.cpp](../src/libslic3r/PrintConfig.cpp) | 更新預設值、新增 bracing_angle 定義 |
| [SLAPrint.cpp](../src/libslic3r/SLAPrint.cpp) | 更新 make_support_cfg + invalidation |
| [SLA/SupportTreeBuildsteps.cpp](../src/libslic3r/SLA/SupportTreeBuildsteps.cpp) | 插入 overhang filter |
| [SLA/SupportTreeUtils.hpp](../src/libslic3r/SLA/SupportTreeUtils.hpp) | 插入 overhang filter |
| [Preset.cpp](../src/libslic3r/Preset.cpp) | 新增 preset key |
| [GUI/ConfigManipulation.cpp](../src/slic3r/GUI/ConfigManipulation.cpp) | 新增 UI toggle |
| [SLAPrintSteps.cpp](../src/libslic3r/SLAPrintSteps.cpp) | 新增 Phase 3 overhang filter，同步 UI 與支撐結構 |
