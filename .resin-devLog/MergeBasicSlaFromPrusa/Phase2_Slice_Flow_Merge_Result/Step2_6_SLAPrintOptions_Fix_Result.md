# Step 2.6：s_Preset_sla_print_options 補全與 Startup Crash 修正

## 問題背景

Step 2.5 完成 SLA Layer Slider 後，進行 SLA Gizmo 操作時發生 Crash：

```
GLGizmoSlaSupports.cpp line 995:
m_new_point_head_diameter = static_cast<const ConfigOptionFloat*>(
    cfg.option("support_head_front_diameter"))->value;
// cfg.option() 回傳 nullptr → dereference → crash
```

### 根本原因

`s_Preset_sla_print_options` 是 `load_preset()` 的白名單，控制哪些 key 會被保留：

```cpp
// Preset.cpp line 1850（簡化）
cfg.apply_only(config, cfg.keys(), /*ignore_nonexistent=*/true);
// cfg.keys() = s_Preset_sla_print_options
// 不在此 list 的 key → 被 apply_only 跳過 → config 中該 key 無值
```

原始 `s_Preset_sla_print_options` 只包含 OrcaSlicer 舊名稱參數（如 `pillar_diameter`、`pinhead_width`），
**完全缺少 PrusaSlicer 命名的 SLA 支援參數**（如 `support_head_front_diameter`、`support_pillar_diameter`）。

因此 GLGizmoSlaSupports 讀取 PrusaSlicer 命名的 config key 時，得到的是 nullptr。

---

## 修正內容

### 修正 1：Preset.cpp — 補全 s_Preset_sla_print_options

**檔案**：`PhrozenOrca/src/libslic3r/Preset.cpp`（lines 973–1043）

在現有 `#pragma region Phrozen LCD Parameter` 之後，新增 `#pragma region PrusaSlicer SLA Parameters` 區塊，補入 67 個 PrusaSlicer 命名的 SLA 參數：

**新增參數分類**：
- 基礎切片：`faded_layers`, `slice_closing_radius`, `slicing_mode`, `supports_enable`
- 支撐結構（標準）：`support_head_front_diameter`（Gizmo crash 關鍵）、`support_pillar_diameter`、`support_head_penetration`、`support_head_width`、`support_small_pillar_diameter_percent`、`support_max_bridges_on_pillar`、`support_pillar_connection_mode`、`support_buildplate_only`、`support_enforcers_only`、`support_max_weight_on_model`、`support_pillar_widening_factor`、`support_base_diameter`、`support_base_height`、`support_base_safety_distance`、`support_critical_angle`、`support_max_bridge_length`、`support_max_pillar_link_distance`、`support_object_elevation`
- 支撐結構（分枝型）：以上所有 `support_*` 對應的 `branchingsupport_*` 版本（16 個）
- 支撐點生成：`support_points_density_relative`（GLGizmoSlaSupports line 820）、`support_points_minimal_distance`（GLGizmoSlaSupports line 822）
- 底墊（Pad）：`pad_enable`、`pad_wall_thickness`、`pad_wall_height`、`pad_brim_size`、`pad_max_merge_distance`、`pad_wall_slope`、`pad_around_object`、`pad_around_object_everywhere`、`pad_object_gap`、`pad_object_connector_stride`、`pad_object_connector_width`、`pad_object_connector_penetration`
- 鏤空（Hollowing）：`hollowing_enable`、`hollowing_min_thickness`、`hollowing_quality`、`hollowing_closing_distance`
- 中繼資料：`default_sla_print_profile`、`compatible_printers`、`compatible_printers_condition`、`inherits`

**關鍵 Crash 參數標注**（保留在程式碼中）：
```cpp
"support_head_front_diameter",  // GLGizmoSlaSupports line 995 — CRASH if missing
"support_pillar_diameter",      // GLGizmoSlaSupports line 751 — potential crash
"support_points_density_relative",  // GLGizmoSlaSupports line 820
"support_points_minimal_distance",  // GLGizmoSlaSupports line 822
```

---

### 修正 2：Startup Crash — support_max_weight_on_model 未定義

**症狀**：加入 `"support_max_weight_on_model"` 後，編譯成功但**程式啟動即 crash，主視窗未出現**。

**根本原因**：

`add_default_preset()` 呼叫 `apply_only` 時 `ignore_nonexistent=false`：

```cpp
// Preset.cpp line 1160-1167
void PresetCollection::add_default_preset(...)
{
    m_presets.back().config.apply_only(defaults, keys.empty() ? defaults.keys() : keys);
    // ↑ ignore_nonexistent 預設 false！
}
```

`apply_only` 在 `ignore_nonexistent=false` 時，若 key 不在 `PrintConfigDef` 中：

```cpp
// Config.cpp line 443-464
ConfigOption *my_opt = this->option(opt_key, /*create=*/true);
if (my_opt == nullptr) {
    if (ignore_nonexistent) continue;
    throw UnknownOptionException(opt_key);  // ← 啟動時 crash
}
```

**問題根源**：`support_max_weight_on_model` 從未在 `init_sla_params()` 中定義。
PhrozenOrca 的 `init_sla_support_params(prefix)` 只被呼叫一次，`prefix="branching"`，
生成的是 `branchingsupport_max_weight_on_model`（注意：prefix = `"branching"`，不是 `"branchingsupport_"`）。
標準（無 prefix）版本 `support_max_weight_on_model` 從未存在。

**修正**：在 `PhrozenOrca/src/libslic3r/PrintConfig.cpp` 的 `init_sla_params()` 中補入定義：

```cpp
// 位置：line 7350，在 support_max_bridges_on_pillar 之後
def = this->add("support_max_weight_on_model", coFloat);
//def->label = L("Max weight on model");
//def->category = L("Supports");
//def->tooltip = L("Maximum weight of sub-trees that terminate on the model instead of the print bed.");
//def->sidetext = L("mm");
def->min = 0;
def->mode = comAdvanced;
def->set_default_value(new ConfigOptionFloat(10.));
```

**注意**：初版誤用 `comExpert`（PrusaSlicer 的 mode 值），OrcaSlicer `ConfigOptionMode` 只有
`comSimple`(0)、`comAdvanced`、`comDevelop`，需改為 `comAdvanced`。

---

## 與 PrusaSlicer 的一致性比對

| 比對項目 | PrusaSlicer 數量 | PhrozenOrca 修正後 | 備注 |
|---------|:--------------:|:-----------------:|------|
| SLA Print Options 總數 | 63 | 62 | 差 1 個（output_filename_format，見下） |
| GLGizmoSlaSupports 讀取的 key | 4 | 4（全部補入） | ✅ |

**刻意省略**：`output_filename_format`
- PrusaSlicer 有此參數（`[input_filename_base].sl1`）
- OrcaSlicer 架構不使用，輸出格式由其他機制控制
- 省略不影響功能

**新增至 PrintConfigDef**：`support_max_weight_on_model`
- PrusaSlicer 在 `init_sla_params()` 中定義（作為標準 SLA 參數）
- PhrozenOrca 原本只有 `branchingsupport_max_weight_on_model`（透過 branching prefix）
- 修正後兩者一致

---

### 修正 3：process_sla() Thumbnail Crash — coPoints vs coString 型別不符

**症狀**：SLA 切片完成後輸出 `.sla` 檔案時發生 Access Violation crash：

```
BackgroundSlicingProcess::process_sla() line 291（修正前）
ThumbnailsParams{ current_print()->full_print_config()
    .option<ConfigOptionPoints>("thumbnails")->values, ... }
// option<ConfigOptionPoints>() 回傳 nullptr（型別不符）
// nullptr->values → Access Violation crash
```

**根本原因**：

`option<T>()` 有嚴格的型別檢查（Config.hpp line 2029-2033）：
```cpp
template<typename TYPE>
const TYPE* option(const t_config_option_key& opt_key) const {
    const ConfigOption* opt = this->optptr(opt_key);
    return (opt == nullptr || opt->type() != TYPE::static_type())
        ? nullptr   // ← 型別不符 → nullptr
        : static_cast<const TYPE*>(opt);
}
```

`thumbnails` 在 PrintConfigDef 中定義為 `coString`（string format：`"48x48/PNG,300x300/PNG"`），
但程式碼要求 `ConfigOptionPoints`（= `coPoints`）→ 型別不符 → 回傳 `nullptr`。

**歷史原因**：此段程式碼源自 BambuStudio，當時 `thumbnails` 為 `coPoints`。
OrcaSlicer 改為 `coString` 後，`process_sla()` 未同步更新。

**修正**（`BackgroundSlicingProcess.cpp`）：

1. 新增 include（line 23）：
```cpp
#include "libslic3r/GCode/Thumbnails.hpp"
```

2. 替換 thumbnail 擷取邏輯（lines 291-301）：
```cpp
// 舊（crash）：
ThumbnailsParams{ current_print()->full_print_config()
    .option<ConfigOptionPoints>("thumbnails")->values, ... }

// 新（正確）：與 FDM 路徑（GCode.cpp line 2011）相同模式
auto [thumbnails_def, thumb_errors] = GCodeThumbnails::make_and_check_thumbnail_list(
    current_print()->full_print_config());
Vec2ds thumb_sizes;
thumb_sizes.reserve(thumbnails_def.size());
for (const auto& [format, size] : thumbnails_def)
    thumb_sizes.emplace_back(size);
ThumbnailsList thumbnails = this->render_thumbnails(
    ThumbnailsParams{ thumb_sizes, true, true, true, true, 0 });
```

`GCodeThumbnailDefinitionsList` = `std::vector<std::pair<GCodeThumbnailsFormat, Vec2d>>`，
`make_and_check_thumbnail_list(ConfigBase&)` 內部正確使用 `option<ConfigOptionString>("thumbnails")`。

---

## 修改清單

| 檔案 | 修改說明 | 行數 |
|------|---------|:----:|
| `PhrozenOrca/src/libslic3r/Preset.cpp` | 新增 `#pragma region PrusaSlicer SLA Parameters`，補入 67 個 PrusaSlicer 命名的 SLA 參數 | +73 行 |
| `PhrozenOrca/src/libslic3r/PrintConfig.cpp` | 在 `init_sla_params()` 補入 `support_max_weight_on_model` 定義（line 7350） | +8 行 |
| `PhrozenOrca/src/slic3r/GUI/BackgroundSlicingProcess.cpp` | 修正 process_sla() thumbnail 型別 crash；新增 Thumbnails.hpp include | +11 行 |

---

## 測試結果

| # | 測試項目 | 結果 |
|---|----------|:----:|
| T1 | 啟動 PhrozenOrca（選 SL1 printer） | ✅ 不再 startup crash |
| T2 | SL1 切片後進入 Preview，啟動 SLA Supports Gizmo | ✅ 不再 nullptr crash |
| T3 | SLA Gizmo 可正常讀取 support_head_front_diameter | ✅ |
| T4 | SLA 切片完成後輸出 .sla 檔案 | ✅ 不再 Access Violation crash |
| T5 | FDM printer 操作不受影響 | ✅ FDM 路徑無 SLA 參數干擾 |

---

## 相關檔案

| 路徑 | 說明 |
|------|------|
| `PhrozenOrca/src/libslic3r/Preset.cpp` | s_Preset_sla_print_options 補全（lines 973–1043） |
| `PhrozenOrca/src/libslic3r/PrintConfig.cpp` | support_max_weight_on_model 定義補入（line 7350） |
| `PhrozenOrca/src/slic3r/GUI/BackgroundSlicingProcess.cpp` | process_sla() thumbnail crash 修正（lines 23, 291–301） |
