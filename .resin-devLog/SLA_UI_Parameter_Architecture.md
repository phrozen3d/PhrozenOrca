# SLA 參數 UI 架構分析

**建立日期**: 2026-03-04
**範圍**: 選擇 SLA 機台後，Profile 與各項參數 UI 所涉及的 class 架構，以及新增參數的完整流程

---

## 一、整體架構概覽

```
┌─────────────────────────────────────────────────────────────────┐
│                    使用者選擇 SLA 機台                            │
└───────────────────────────┬─────────────────────────────────────┘
                            │
          ┌─────────────────┼─────────────────┐
          ▼                 ▼                 ▼
   [Preset 管理層]    [Config 資料層]     [UI 渲染層]
   PresetBundle        PrintConfig        TabSLAPrint
   PresetCollection    SLAPrintObjectConfig TabSLAMaterial
   Preset              SLAMaterialConfig  Sidebar combos
                       SLAPrinterConfig
```

---

## 二、Preset 管理層

### PresetBundle（主要容器）

**檔案**：[PresetBundle.hpp](PhrozenOrca/src/libslic3r/PresetBundle.hpp)

```cpp
class PresetBundle {
public:
    PresetCollection prints;         // FDM process presets
    PresetCollection sla_prints;     // SLA process presets  ← SLA 專用
    PresetCollection filaments;      // FDM material presets
    PresetCollection sla_materials;  // SLA material presets ← SLA 專用

    // 技術路由：依 printer technology 選擇正確的 collection
    PresetCollection& materials(PrinterTechnology pt) {
        return pt == ptFFF ? this->filaments : this->sla_materials;
    }
};
```

### Preset::Type 枚舉

**檔案**：[Preset.hpp](PhrozenOrca/src/libslic3r/Preset.hpp)

```cpp
enum Type {
    TYPE_PRINT,           // FDM process preset
    TYPE_SLA_PRINT,       // SLA process preset    ← SLA 專用
    TYPE_FILAMENT,        // FDM material preset
    TYPE_SLA_MATERIAL,    // SLA material preset   ← SLA 專用
    TYPE_PRINTER,         // Printer preset (共用)
    // ...
};
```

### Preset 資料夾對應

| Type | 資料夾名稱 | 說明 |
|------|-----------|------|
| `TYPE_SLA_PRINT` | `sla_print/` | SLA 製程參數 profile |
| `TYPE_SLA_MATERIAL` | `sla_materials/` | SLA 材料 profile |
| `TYPE_PRINTER` | `printer/` | 機台設定（FDM + SLA 共用） |

---

## 三、Config 資料層

### Config Class 繼承關係

```
StaticPrintConfig (基類)
├── SLAPrintConfig           ← filename_format
├── SLAPrintObjectConfig     ← 50+ SLA 製程參數（支撐、中空等）
├── SLAMaterialConfig        ← 曝光時間、材料校正等
└── SLAPrinterConfig         ← 機台硬體參數（螢幕尺寸、解析度等）
    │
    └─── 合併類：SLAFullPrintConfig
         PRINT_CONFIG_CLASS_DERIVED_DEFINE0(
             SLAFullPrintConfig,
             (SLAPrinterConfig, SLAPrintConfig, SLAPrintObjectConfig, SLAMaterialConfig)
         )
```

**檔案**：[PrintConfig.hpp](PhrozenOrca/src/libslic3r/PrintConfig.hpp)

### SLAPrintObjectConfig 重要參數（製程 Tab）

| 參數名稱 | 類型 | 說明 |
|---------|------|------|
| `layer_height` | `ConfigOptionFloat` | 層厚 |
| `faded_layers` | `ConfigOptionInt` | 曝光過渡層數 |
| `supports_enable` | `ConfigOptionBool` | 是否啟用支撐 |
| `support_tree_type` | `ConfigOptionEnum<SLASupportTreeType>` | 支撐類型 |
| `support_head_front_diameter` | `ConfigOptionFloat` | 支撐頭直徑 |
| `support_pillar_diameter` | `ConfigOptionFloat` | 支撐柱直徑 |
| `support_pillar_connection_mode` | `ConfigOptionEnum<SLAPillarConnectionMode>` | 支撐柱連接方式 |
| `support_buildplate_only` | `ConfigOptionBool` | 只生成底板支撐 |
| `pad_enable` | `ConfigOptionBool` | 是否生成底墊 |

### SLAMaterialConfig 重要參數（材料 Tab）

| 參數名稱 | 類型 | 說明 |
|---------|------|------|
| `initial_layer_height` | `ConfigOptionFloat` | 初始層厚 |
| `exposure_time` | `ConfigOptionFloat` | 一般層曝光時間 |
| `initial_exposure_time` | `ConfigOptionFloat` | 初始層曝光時間 |
| `material_correction_x/y/z` | `ConfigOptionFloat` | 材料尺寸補正 |
| `material_print_speed` | `ConfigOptionEnum<SLAMaterialSpeed>` | 列印速度模式 |
| `zcorrection_layers` | `ConfigOptionInt` | Z 軸補正層數 |

### SLAPrinterConfig 重要參數（機台設定）

| 參數名稱 | 類型 | 說明 |
|---------|------|------|
| `printer_technology` | `ConfigOptionEnum<PrinterTechnology>` | `ptSLA` or `ptFFF` |
| `display_width` / `display_height` | `ConfigOptionFloat` | LCD 螢幕物理尺寸（mm） |
| `display_pixels_x` / `display_pixels_y` | `ConfigOptionInt` | LCD 解析度（pixels） |
| `display_orientation` | `ConfigOptionEnum<SLADisplayOrientation>` | 螢幕方向 |
| `relative_correction_x/y/z` | `ConfigOptionFloat` | 相對尺寸補正 |
| `sla_archive_format` | `ConfigOptionString` | 輸出格式（sl1/phz 等） |

### 參數定義 Macro 格式

**檔案**：[PrintConfig.hpp](PhrozenOrca/src/libslic3r/PrintConfig.hpp) — `PRINT_CONFIG_CLASS_DEFINE`

```cpp
PRINT_CONFIG_CLASS_DEFINE(
    SLAPrintObjectConfig,
    ((ConfigOptionFloat,  layer_height))
    ((ConfigOptionInt,    faded_layers))
    ((ConfigOptionBool,   supports_enable))
    ((ConfigOptionEnum<SLASupportTreeType>, support_tree_type))
    // ... 每行一個參數
)
```

每個 `((Type, name))` 條目自動生成：Member 變數、hash、equality operator、序列化初始化。

---

## 四、UI 渲染層

### Tab Class 繼承關係

```
wxPanel (wxWidgets)
└── Tab (基底 class)  [Tab.hpp]
    ├── TabPrint              ← FDM 製程 Tab
    ├── TabFilament           ← FDM 材料 Tab
    ├── TabPrinter            ← 機台 Tab（共用）
    ├── TabSLAPrint           ← SLA 製程 Tab  ← 重點
    └── TabSLAMaterial        ← SLA 材料 Tab  ← 重點
```

**檔案**：[Tab.hpp](PhrozenOrca/src/slic3r/GUI/Tab.hpp)、[Tab.cpp](PhrozenOrca/src/slic3r/GUI/Tab.cpp)

### TabSLAPrint（製程 Tab）

```cpp
class TabSLAPrint : public Tab {
public:
    TabSLAPrint(ParamsPanel* parent) :
        Tab(parent, _(L("Process Settings")), Slic3r::Preset::TYPE_SLA_PRINT) {}

    void build() override;                    // 建立 UI 頁面與參數組
    void reload_config() override;            // 從 config 重載 UI
    void update_description_lines() override; // 更新說明文字
    void toggle_options() override;           // 依設定值顯示/隱藏選項
    void update() override;                   // 參數變更後更新
    void clear_pages() override;              // 清除頁面
    bool supports_printer_technology(const PrinterTechnology tech) const override {
        return tech == ptSLA;                 // 只對 SLA 機台顯示
    }
};
```

**build() 建立的參數頁面**（[Tab.cpp](PhrozenOrca/src/slic3r/GUI/Tab.cpp) lines 6662-6755）：

| 頁面名稱 | 包含參數群組 | 主要參數 |
|---------|-----------|---------|
| **Layer** | Layer & Exposure | `layer_height`, `exposure_time`, `bottom_exposure_time` |
| | Transition | `transition_layer_count`, `transition_type` |
| | Wait & Rest | `waiting_mode_during_printing`, `rest_time_before_lift` |
| **Distance** | Lift & Retract Distance | `bottom_lift_distance`, `lifting_distance`, `retract_distance` |
| **Speed** | Lift & Retract Speed | `bottom_lift_speed`, `lifting_speed`, `retract_speed` |
| **Advanced** | Advance | `anti_aliasing`, `gray_scale_level`, `image_blur_enable` |
| **Support** | Supports | `generate_support` |
| | Top / Main / Bottom | `top_upper_diameter`, `pillar_diameter`, `support_bottom_diameter` |
| | Raft Setting | `object_elevation` |
| | Automatic Generation | `support_points_density` |

### TabSLAMaterial（材料 Tab）

```cpp
class TabSLAMaterial : public Tab {
public:
    TabSLAMaterial(ParamsPanel* parent) :
        Tab(parent, _(_devL("Material Settings")), Slic3r::Preset::TYPE_SLA_MATERIAL) {}

    void build() override;
    void toggle_options() override;  // 依 material_print_speed 顯示不同選項
    void update() override;
    bool supports_printer_technology(const PrinterTechnology tech) const override {
        return tech == ptSLA;
    }
};
```

### Tab 在 MainFrame 的初始化

**檔案**：[MainFrame.cpp](PhrozenOrca/src/slic3r/GUI/MainFrame.cpp) lines ~1325-1326

```cpp
add_created_tab(new TabSLAPrint(m_param_panel));
add_created_tab(new TabSLAMaterial(m_param_panel));
```

### Sidebar Preset ComboBox

**檔案**：[Plater.hpp](PhrozenOrca/src/slic3r/GUI/Plater.hpp)、[Plater.cpp](PhrozenOrca/src/slic3r/GUI/Plater.cpp)

```cpp
class Sidebar {
    PlaterPresetComboBox *combo_sla_print;     // SLA 製程 preset 選擇框
    PlaterPresetComboBox *combo_sla_material;  // SLA 材料 preset 選擇框
};
```

初始化（Plater.cpp lines ~923-1013）：
```cpp
// SLA Print combo
PlaterPresetComboBox* combo_sla_print = new PlaterPresetComboBox(
    p->m_panel_sla_print_content, Preset::TYPE_SLA_PRINT);

// SLA Material combo
PlaterPresetComboBox* combo_sla_material = new PlaterPresetComboBox(
    p->m_panel_sla_material_content, Preset::TYPE_SLA_MATERIAL);
```

---

## 五、UI 建構流程：Page → Group → Option

### 建構層次結構

```
Tab::build()
└── add_options_page("Page Name", "icon_name")   → 產生 Page
    └── page->new_optgroup("Group Title", "icon") → 產生 ConfigOptionsGroup
        └── optgroup->append_single_option_line("param_key") → 產生 Line
            └── 讀取 PrintConfigDef 中的 opt_key 定義
                → 根據 opt.type 選擇 UI widget
                   (coFloat → SpinCtrl, coBool → CheckBox, coEnum → Choice, ...)
```

### 完整範例

```cpp
void TabSLAPrint::build() {
    m_presets = &m_preset_bundle->sla_prints;
    load_initial_data();

    // 1. 建立頁面
    auto page = add_options_page(L("Support"), "support");

    // 2. 建立參數群組
    auto optgroup = page->new_optgroup(L("Supports"), L"icon_supports");

    // 3. 加入參數（使用在 PrintConfig.cpp 中 def->key 定義的名稱）
    optgroup->append_single_option_line("supports_enable");
    optgroup->append_single_option_line("support_tree_type");

    optgroup = page->new_optgroup(L("Pillar"), L"icon_pillar");
    optgroup->append_single_option_line("support_pillar_diameter");
    optgroup->append_single_option_line("support_pillar_connection_mode");
}
```

---

## 六、ConfigManipulation — 選項可見性控制

**檔案**：[ConfigManipulation.hpp](PhrozenOrca/src/slic3r/GUI/ConfigManipulation.hpp)、[ConfigManipulation.cpp](PhrozenOrca/src/slic3r/GUI/ConfigManipulation.cpp)

### toggle_print_sla_options()（lines 922-975）

控制 SLA 選項的顯示/隱藏：

```cpp
void ConfigManipulation::toggle_print_sla_options(DynamicPrintConfig* config) {
    // waiting_mode_during_printing → 控制 rest_time 選項是否顯示
    // anti_aliasing → 控制 gray_scale_level 或 anti_aliasing_level 顯示
    // image_blur_enable → 控制 image_blur_pixel 顯示
    // generate_support → 控制所有支撐幾何選項是否顯示
}
```

### update_print_sla_config()（lines 890-920）

在參數改變時進行合法性驗證：
```cpp
void ConfigManipulation::update_print_sla_config(DynamicPrintConfig* config, ...) {
    // 驗證：support_head_penetration ≤ support_head_width
    // 驗證：support_head_front_diameter ≤ support_pillar_diameter
    // 若不合法 → 顯示 wxMessageDialog 錯誤訊息
}
```

---

## 七、參數變更完整流程

```
使用者在 UI 修改數值
        │
        ▼
ConfigOptionsGroup::on_change_OG()
        │  修改 DynamicPrintConfig 的值
        ▼
Tab::on_value_change(opt_key, value)
        │  處理特定 key 的副作用
        ▼
Tab::update_dirty()
        │  標記 preset 為 dirty，更新 UI 裝飾
        ▼
Tab::update()  ← TabSLAPrint 覆寫
        │
        ├── ConfigManipulation::update_print_sla_config()  ← 驗證參數
        ├── update_description_lines()                      ← 更新說明文字
        ├── toggle_options()                                ← 顯示/隱藏相關選項
        │       └── ConfigManipulation::toggle_print_sla_options()
        └── wxGetApp().mainframe->on_config_changed()       ← 觸發重新切片
```

---

## 八、新增 SLA 參數的完整步驟

### 需修改的檔案與順序

| # | 檔案 | 修改內容 |
|---|------|---------|
| 1 | `PrintConfig.hpp` | 在 Config Class 中新增 member |
| 2 | `PrintConfig.cpp` | 新增 option 定義（label, tooltip, min/max, default） |
| 3 | `Tab.cpp` | 在 TabSLAPrint 或 TabSLAMaterial::build() 加入 UI 行 |
| 4 | `ConfigManipulation.cpp` | （選擇性）新增 toggle 邏輯或驗證邏輯 |
| 5 | `profile .ini 文件` | 新增預設值到相關 profile |

---

### Step 1：PrintConfig.hpp — 在 Config Class 新增 Member

決定參數屬於哪個 Config Class：

| 參數類型 | 放入的 Config Class |
|---------|-------------------|
| 支撐、中空、製程 | `SLAPrintObjectConfig` |
| 材料曝光、補正 | `SLAMaterialConfig` |
| 機台硬體 | `SLAPrinterConfig` |

在 PRINT_CONFIG_CLASS_DEFINE macro 中加入一行：

```cpp
PRINT_CONFIG_CLASS_DEFINE(
    SLAPrintObjectConfig,
    // ... 現有參數 ...
    ((ConfigOptionFloat, support_pillar_diameter))
    // ↓ 新增這一行
    ((ConfigOptionFloat, my_new_sla_param))   // ← 新增
)
```

**支援的型別**：
- `ConfigOptionFloat` — 浮點數（mm、sec 等）
- `ConfigOptionInt` — 整數
- `ConfigOptionBool` — 布林值（勾選框）
- `ConfigOptionPercent` — 百分比
- `ConfigOptionEnum<MyEnumType>` — 枚舉選項

---

### Step 2：PrintConfig.cpp — 新增 Option 定義

#### 數值型參數範例

```cpp
// 在 init_sla_support_params() 或 init_sla_params() 中加入
def = this->add("my_new_sla_param", coFloat);
def->label = L("My New Parameter");
def->category = L("Supports");
def->tooltip = L("Detailed description of what this parameter does.");
def->sidetext = L("mm");
def->min = 0;
def->max = 10;
def->mode = comSimple;   // comSimple 或 comAdvanced
def->set_default_value(new ConfigOptionFloat(1.0));
```

#### 枚舉型參數範例

```cpp
// 1. 先在 PrintConfig.hpp 定義 enum
enum MyNewEnum { mneOption1, mneOption2, mneOption3 };

// 2. 在 PrintConfig.cpp 加入 enum map
static constexpr auto MyNewEnumDef = Slic3r::ConfigOptionEnumDef {
    { "option1", mneOption1 },
    { "option2", mneOption2 },
    { "option3", mneOption3 },
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(MyNewEnum)

// 3. 加入 option 定義
def = this->add("my_enum_param", coEnum);
def->label = L("My Enum Parameter");
def->enum_keys_map = &ConfigOptionEnum<MyNewEnum>::get_enum_values();
def->enum_values.push_back("option1");
def->enum_values.push_back("option2");
def->enum_values.push_back("option3");
def->enum_labels.push_back(L("Option 1"));
def->enum_labels.push_back(L("Option 2"));
def->enum_labels.push_back(L("Option 3"));
def->mode = comSimple;
def->set_default_value(new ConfigOptionEnum<MyNewEnum>(mneOption1));
```

**`mode` 選項**：
- `comSimple`：在 Simple 和 Advanced 模式下都顯示
- `comAdvanced`：只在 Advanced 模式下顯示

---

### Step 3：Tab.cpp — 在 build() 中加入 UI 行

在 TabSLAPrint::build() 中找到適當的頁面和群組：

```cpp
// 選項 A：加入到現有頁面和群組
// 找到對應 page 和 optgroup，直接加入
optgroup->append_single_option_line("my_new_sla_param");

// 選項 B：在現有頁面建立新群組
auto optgroup = page->new_optgroup(L("My New Group"), L"icon_name");
optgroup->append_single_option_line("my_new_sla_param");

// 選項 C：建立全新頁面
auto page = add_options_page(L("My New Page"), "icon_name");
auto optgroup = page->new_optgroup(L("My Group"));
optgroup->append_single_option_line("my_new_sla_param");
```

---

### Step 4：ConfigManipulation.cpp — 選擇性處理（依條件顯示）

若新參數需要根據其他參數值 **顯示或隱藏**：

```cpp
void ConfigManipulation::toggle_print_sla_options(DynamicPrintConfig* config) {
    // 現有 toggle 邏輯 ...

    // 新增：當 supports_enable = false 時隱藏 my_new_sla_param
    bool supports_enable = config->opt_bool("supports_enable");
    toggle_field("my_new_sla_param", supports_enable);
}
```

若需要參數值**合法性驗證**：

```cpp
void ConfigManipulation::update_print_sla_config(DynamicPrintConfig* config, ...) {
    // 現有驗證 ...

    // 新增驗證
    double new_param = config->opt_float("my_new_sla_param");
    double related_param = config->opt_float("related_param");
    if (new_param > related_param) {
        // 顯示錯誤或自動修正
        config->set_key_value("my_new_sla_param",
            new ConfigOptionFloat(related_param));
    }
}
```

---

### Step 5：Profile .ini 文件 — 新增預設值

在所有相關的 `.ini` profile 文件中加入預設值（位於 `resources/profiles/`）：

```ini
# sla_print/SL1_common.ini 或其他相關 profile
my_new_sla_param = 1.0
```

若參數屬於特定機台的 override，也需在機台 profile 加入。

---

### Step 6（選擇性）：SLAPrint.cpp — 在 slicing pipeline 使用新參數

若新參數需要影響切片計算，在 SLAPrint.cpp 中讀取：

```cpp
void SLAPrint::Steps::some_step(SLAPrintObject &po) {
    const SLAPrintObjectConfig& cfg = po.config();
    double my_param = cfg.my_new_sla_param.value;
    // 使用 my_param 進行計算
}
```

---

## 九、測試驗證清單

新增參數後，依序執行以下測試：

### 編譯測試
- [ ] 清除並重新編譯，確認無編譯錯誤
- [ ] 確認 PrintConfig.hpp 的 macro 展開無語法錯誤

### UI 顯示測試
- [ ] 啟動程式，切換到 SLA 機台
- [ ] 確認新參數出現在正確的 Tab 頁面和群組中
- [ ] 確認 label、tooltip、單位文字正確顯示
- [ ] 確認 min/max 邊界值有正確的 UI 限制

### Profile 讀寫測試
- [ ] 讀取含新參數的 profile 文件，確認值正確載入
- [ ] 修改參數值後儲存，確認 .ini 文件有寫入正確值
- [ ] 讀取舊版不含新參數的 profile，確認 fallback 到 default 值
- [ ] 確認新參數出現在 preset dirty 比較中（修改後 preset 名稱顯示 *）

### 切換 Toggle 測試（如有條件顯示）
- [ ] 依賴條件為 false 時，確認新參數被隱藏
- [ ] 依賴條件為 true 時，確認新參數正確顯示

### 切片功能測試
- [ ] 若參數影響 slicing，確認不同值產生預期不同的切片結果
- [ ] 確認 FDM 機台切片完全不受影響

### Preset 繼承測試
- [ ] 新參數在 material override 中正確覆寫 print 值（如適用）

---

## 十、常見問題與注意事項

### 1. 參數名稱規範
- 使用 PrusaSlicer 的命名（遵循 CLAUDE.md 規範）
- 若 PhrozenOrca 已有不同名稱，保留舊名並加 `//[TODO]` 註解

### 2. SLA vs FDM 隔離
- `TabSLAPrint::supports_printer_technology()` 返回 `tech == ptSLA`，確保 Tab 只在 SLA 機台顯示
- 在 PrintConfig.cpp 定義時可加 `def->printer_technology = ptSLA` 標記

### 3. Config Class 選擇
- 不確定放哪個 Config Class → 參考 PrusaSlicer 原版決定
- 製程參數（與機台無關）→ `SLAPrintObjectConfig`
- 材料依賴參數 → `SLAMaterialConfig`

### 4. Profile 文件同步
- 新增參數後，所有現有 SL1 profile 文件都需要加入預設值
- 否則讀取舊 profile 時會使用 code 中的 default 值（通常 OK，但需確認）

### 5. init_sla_support_params vs init_sla_params
- 支撐相關參數 → `init_sla_support_params()` 中定義
- 其他 SLA 參數 → `init_sla_params()` 中定義
- 兩者都在 [PrintConfig.cpp](PhrozenOrca/src/libslic3r/PrintConfig.cpp) 中

---

## 十一、關鍵檔案速查

| 用途 | 檔案 |
|------|------|
| Config Class 定義（member 宣告） | [PrintConfig.hpp](PhrozenOrca/src/libslic3r/PrintConfig.hpp) |
| Option 詳細定義（label, tooltip, default） | [PrintConfig.cpp](PhrozenOrca/src/libslic3r/PrintConfig.cpp) |
| SLA 製程 Tab UI | [Tab.cpp](PhrozenOrca/src/slic3r/GUI/Tab.cpp) `TabSLAPrint::build()` line ~6662 |
| SLA 材料 Tab UI | [Tab.cpp](PhrozenOrca/src/slic3r/GUI/Tab.cpp) `TabSLAMaterial::build()` line ~6540 |
| Tab 基底 class | [Tab.hpp](PhrozenOrca/src/slic3r/GUI/Tab.hpp) |
| 選項顯示/隱藏控制 | [ConfigManipulation.cpp](PhrozenOrca/src/slic3r/GUI/ConfigManipulation.cpp) line ~922 |
| 參數驗證 | [ConfigManipulation.cpp](PhrozenOrca/src/slic3r/GUI/ConfigManipulation.cpp) line ~890 |
| Sidebar Combo Box | [Plater.cpp](PhrozenOrca/src/slic3r/GUI/Plater.cpp) line ~923 |
| Preset 管理 | [PresetBundle.hpp](PhrozenOrca/src/libslic3r/PresetBundle.hpp) |
| Profile 文件目錄 | `resources/profiles/` |
