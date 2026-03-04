# Step 1.1 執行結果: PrintConfig SLA 參數同步

**分析日期**: 2026-01-26
**執行日期**: 2026-02-08
**狀態**: ✅ 完成 (編譯通過、執行正常)

---

## 1. 決策記錄

### D1: 參數命名
**決策**: 維持 PhrozenOrca 命名，不改回 PrusaSlicer 命名

| PrusaSlicer 名稱 | PhrozenOrca 名稱 | 動作 |
|------------------|-----------------|------|
| `output_filename_format` | `filename_format` | 保留 PhrozenOrca |
| `bed_shape` | `printable_area` | 保留 PhrozenOrca |
| `max_print_height` | `printable_height` | 保留 PhrozenOrca |

### D2: Tilt 參數
**決策**: 完整新增所有 17 個 Tilt 參數 + TowerSpeeds/TiltSpeeds Enum

### D3: area_fill
**決策 (修正)**: 保留在 SLAPrinterConfig 原位置 + 同時新增到 SLAMaterialConfig
- SLAPrinterConfig 中**保留** `area_fill`，預設值維持 PhrozenOrca 的 50%
- SLAMaterialConfig 中**新增** `area_fill`，加註 `// PrusaSlicer 新增到 Material, TODO: 確認是否保留`
- **原因**: 移除會破壞 `SLAPrintSteps.cpp`、`SLAPrint.cpp`、`Preset.cpp` 中的 `printer_config.area_fill` 引用

### D4: support_pillar_widening_factor
**決策 (修正)**: 保留 PhrozenOrca 預設值 0.0
- 非前綴版 (default support) 保留 PhrozenOrca 原值 0.0
- branching 前綴版使用 PrusaSlicer 值 0.5
- **原因**: 遵守「不修改 PhrozenOrca 客製化」原則

### D5: SLAPillarConnectionMode Enum 型別
**決策**: 保留 PhrozenOrca 的直接 enum 定義，不改為 type alias

### D6: UI 本地化 (L() macro)
**決策**: 保留 PhrozenOrca 現狀 (SLA UI 標籤已註解)
- 新增的函式 (`init_sla_support_params`, `init_sla_tilt_params`) 也使用註解風格

### D7: support_points_minimal_distance
**決策**: 保留此 PhrozenOrca 特有參數，不移除

---

## 2. 型別相容處理

PhrozenOrca 的 Config.hpp 缺少 PrusaSlicer 的某些型別。為遵守「不修改非 SLA 程式碼」原則，使用相容替代型別：

| PrusaSlicer 型別 | PhrozenOrca 替代 | 影響 | 使用處 |
|-----------------|-----------------|------|--------|
| `ConfigOptionFloatNullable` | `ConfigOptionFloat` (以 -1 表示未設定) | nullable 語義降級 | 10 個 `material_ow_*` |
| `ConfigOptionIntNullable` | `ConfigOptionInt` (以 -1 表示未設定) | nullable 語義降級 | 1 個 `material_ow_*` |
| `ConfigOptionEnums<TowerSpeeds>` | `ConfigOptionInts` | 型別安全降級 | 1 個 tilt 參數 |
| `ConfigOptionEnums<TiltSpeeds>` | `ConfigOptionInts` | 型別安全降級 | 4 個 tilt 參數 |
| `def->set_enum<T>()` API | 舊式 `enum_keys_map` + `enum_values` + `enum_labels` | 無功能差異 | enum 參數定義 |
| `comExpert` (ConfigOptionMode) | `comAdvanced` | 最高可見等級從 Expert 降為 Advanced | 23 個新增參數 |

---

## 3. 實際修改內容

### 3.1 PrintConfig.hpp

**檔案**: `PhrozenOrca\src\libslic3r\PrintConfig.hpp`

#### 新增 Enum 定義 (Phase A)

| Enum | 值數量 | 說明 |
|------|:------:|------|
| `SLASupportTreeType` | 2 (Default, Branching) | 本地 enum，非 sla:: namespace |
| `TowerSpeeds` | 11 | 升降速度選項 (plain enum) |
| `TiltSpeeds` | 14 | 傾斜速度選項 (plain enum) |
| `SLAMaterialSpeed` +1 | `slamsHighViscosity` | 末尾新增，不影響現有值 |

新增 4 個 `CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS`:
`SLASupportTreeType`, `SLAMaterialSpeed`, `TowerSpeeds`, `TiltSpeeds`

#### 新增函式宣告

```cpp
void init_sla_support_params(const std::string &prefix);
void init_sla_tilt_params();
```

#### SLAPrintObjectConfig 新增成員 (Phase B)

| 成員 | 類型 | 說明 |
|------|------|------|
| `slicing_mode` | `ConfigOptionEnum<SlicingMode>` | 切片模式 (SlicingMode 已存在) |
| `support_tree_type` | `ConfigOptionEnum<SLASupportTreeType>` | 支撐樹類型 |
| `support_max_weight_on_model` | `ConfigOptionFloat` | 模型上最大支撐重量 |
| `support_enforcers_only` | `ConfigOptionBool` | 僅生成強制區域支撐 |
| 17 個 `branchingsupport_*` | 各型別 | 分支支撐參數 (完整鏡像 support_*) |

**總計新增**: 21 個成員

#### SLAMaterialConfig 新增成員

| 成員 | 類型 | 說明 |
|------|------|------|
| `zcorrection_layers` | `ConfigOptionInt` | Z校正層數 |
| 10 個 `material_ow_*` (float) | `ConfigOptionFloat` | 材料覆蓋 (用 -1 表示未設定) |
| `material_ow_support_points_density_relative` | `ConfigOptionInt` | 材料覆蓋:密度 |
| `area_fill` | `ConfigOptionFloat` | PrusaSlicer 新增, TODO: 確認是否保留 |
| 17 個 tilt params | `ConfigOptionInts/Floats/Bools` | 傾斜參數 |

**總計新增**: 30 個成員

#### SLAPrinterConfig 變更

| 動作 | 成員 | 類型 | 說明 |
|------|------|------|------|
| **保留** | `area_fill` | ConfigOptionFloat | 補回原位 (預設 50%) |
| 新增 | `high_viscosity_tilt_time` | ConfigOptionFloat | 高黏度傾斜時間 |
| 新增 | `sla_archive_format` | ConfigOptionString | 輸出格式 |
| 新增 | `sla_output_precision` | ConfigOptionFloat | 輸出精度 |

### 3.2 PrintConfig.cpp

**檔案**: `PhrozenOrca\src\libslic3r\PrintConfig.cpp`

#### 新增 Enum 對應表 (Phase C)

| Enum Map | 說明 |
|----------|------|
| `s_keys_map_SLASupportTreeType` | default, branching |
| `s_keys_map_TowerSpeeds` | 11 個速度值 |
| `s_keys_map_TiltSpeeds` | 14 個速度值 |
| 修改 `s_keys_map_SLAMaterialSpeed` | 新增 `high_viscosity` |

#### 新增函式 (Phase D, E)

| 函式 | 說明 |
|------|------|
| `init_sla_support_params(const std::string &prefix)` | 支撐參數初始化 (prefix 機制，用於 branching) |
| `init_sla_tilt_params()` | Tilt/Tower 參數初始化，使用 `coInts` 配合 enum 值 |

兩個函式均使用 PhrozenOrca 程式碼風格：
- 註解掉的 `//def->label`、`//def->tooltip`
- 舊式 enum API (`enum_keys_map` + `enum_values` + `enum_labels`)

#### 修改 init_sla_params() (Phase F)

在現有程式碼**末尾**新增（不動現有定義）：

| 新增項目 | 說明 |
|----------|------|
| `material_print_speed` 加入 `"high_viscosity"` | 新增 enum 值和 label |
| `support_tree_type` 定義 | 使用舊式 enum API |
| `init_sla_support_params("branching")` 呼叫 | 產生所有 `branchingsupport_*` 參數 |
| `support_enforcers_only` 定義 | 新 SLA 參數 |
| `zcorrection_layers` 定義 | 新 SLA 材料參數 |
| `material_ow_*` 循環建立 (11 個) | 使用 `coFloat`/`coInt`，預設 -1 |
| `high_viscosity_tilt_time` 定義 | 新印表機參數 |
| `sla_archive_format` 定義 | 新印表機參數 |
| `sla_output_precision` 定義 | 新印表機參數 |
| `init_sla_tilt_params()` 呼叫 | 產生所有 tilt/tower 參數 |

**未修改的現有定義**:
- 所有非前綴 support 參數保持原樣 (不替換為 `init_sla_support_params("")`)
- `area_fill` 定義 (line ~6577) 保持原樣 (default 50%)
- `support_pillar_widening_factor` 定義保持原樣 (default 0.0)

---

## 4. 規則遵守確認

### 保護項目

| 保護項目 | 所在位置 | 狀態 |
|----------|----------|------|
| `filename_format` | SLAPrintConfig | ✅ 不改名 |
| `printable_area` | SLAPrinterConfig | ✅ 不改名 |
| `printable_height` | SLAPrinterConfig | ✅ 不改名 |
| `support_points_minimal_distance` | SLAPrintObjectConfig | ✅ 保留 |
| `area_fill` 預設值 50% | SLAPrinterConfig | ✅ 保留原位和原值 |
| `support_pillar_widening_factor` 預設值 0.0 | init_sla_params | ✅ 不修改 |
| `BUILD_PHROZEN_ORCA` | PrintHostType enum | ✅ 不動 |
| SLA UI 標籤註解 | init_sla_params() | ✅ 不恢復 L() |
| Config.hpp (非 SLA 核心) | 型別系統 | ✅ 未修改 |
| FDM 相關程式碼 | 各處 | ✅ 未觸及 |

### 規則違反修正歷史

| 問題 | 原始行為 | 修正 |
|------|----------|------|
| `area_fill` 從 SLAPrinterConfig 移除 | 會破壞 SLAPrintSteps.cpp 等引用 | 補回原位，兩處都保留 |
| `ConfigOptionFloatNullable` 不存在 | 編譯失敗 | 改用 `ConfigOptionFloat` |
| `ConfigOptionEnums<T>` 不存在 | 編譯失敗 | 改用 `ConfigOptionInts` |
| `SLAMaterialSpeed` enum 前向引用 | `DECLARE_STATIC_MAPS` (line 506) 在 enum 定義 (line 1666) 之前，編譯失敗 | 將 enum 定義移至 line 279 (TiltSpeeds 之後、BrimType 之前)，原位置移除 |
| `comExpert` 未宣告 | PhrozenOrca `ConfigOptionMode` 只有 `comSimple`/`comAdvanced`，無 `comExpert` | 23 處 `comExpert` → `comAdvanced` (不修改 Config.hpp) |
| `lattice_angle_1` 既有 bug | PhrozenLCDTab.cpp 用 `lattice_angle_1` 但定義為 `lateral_lattice_angle_1` | 修正為 `lateral_lattice_angle_1` / `_2` (經使用者同意修改 Phrozen 客製化檔案) |

---

## 5. 影響範圍

### 直接影響
| 檔案 | 影響 | 程度 |
|------|------|------|
| `PrintConfig.hpp` | 新增 enum、class 成員、函式宣告 | 高 |
| `PrintConfig.cpp` | 新增函式、新增參數定義、修改 material_print_speed | 高 |

### 間接影響 (需後續檢查)
| 檔案/模組 | 影響說明 | 程度 |
|-----------|----------|------|
| `SLAPrintSteps.cpp` | `area_fill` 維持 `printer_config.area_fill` 不變 | 無 |
| `Preset.cpp` | 新參數需納入 preset 讀寫 | 低 |
| `SLAPrint.cpp` | `area_fill` 引用不受影響 | 無 |
| `SLA/SupportTree*` | `support_tree_type` 的實際使用需等 Phase 2 | 低 |
| Config 檔案 (.ini) | 向後相容: 舊 config 缺少新參數時使用預設值 | 低 |

### 不受影響
| 項目 | 原因 |
|------|------|
| PhrozenConnect 功能 | SLA 參數不涉及 |
| PartPlateList / IMToolbar | UI 層面，不涉及 |
| GCodeProcessor | FDM 功能，不涉及 |
| Config.hpp | 未修改核心型別系統 |
| FDM PrintConfig 定義 | 未觸及 |

---

## 6. 變更統計

| 類別 | 數量 |
|------|:----:|
| 新增 Enum 定義 | 3 |
| 修改 Enum 定義 | 1 (SLAMaterialSpeed +1值) |
| 新增 Class 成員 (SLAPrintObjectConfig) | 21 |
| 新增 Class 成員 (SLAMaterialConfig) | 30 |
| 新增 Class 成員 (SLAPrinterConfig) | 3 (+1 補回) |
| 新增函式 | 2 (init_sla_support_params, init_sla_tilt_params) |
| 修改函式 (init_sla_params 末尾新增) | 1 |
| 新增 Enum Map | 3 |
| 修改 Enum Map | 1 |
| **總計新增參數** | **~54** |

---

## 7. 已完成的執行階段

| Phase | 說明 | 狀態 |
|-------|------|:----:|
| A | 新增 Enum 定義到 PrintConfig.hpp | ✅ 完成 |
| B | 新增 Class 成員到 PrintConfig.hpp | ✅ 完成 (含型別修正) |
| C | 新增 Enum Map 到 PrintConfig.cpp | ✅ 完成 |
| D | 新增 `init_sla_support_params()` 函式 | ✅ 完成 |
| E | 新增 `init_sla_tilt_params()` 函式 | ✅ 完成 |
| F | 修改 `init_sla_params()` 末尾新增 | ✅ 完成 |
| G | `area_fill` 處理 (補回 + 雙位置保留) | ✅ 完成 |
| H1 | 編譯驗證 #1: `SLAMaterialSpeed` 前向引用修正 | ✅ 完成 |
| H2 | 編譯驗證 #2: `comExpert` → `comAdvanced` 修正 | ✅ 完成 |
| H3 | 編譯驗證 #3: PhrozenLCDTab.cpp `lattice_angle_1` → `lateral_lattice_angle_1` 既有 bug 修正 | ✅ 完成 |
| H4 | 編譯+執行驗證通過 | ✅ 完成 |

---

## 8. 待處理項目

### 下一步: 編譯驗證 (Phase H)
- 執行 CMake 設定確認無語法錯誤
- 編譯 libslic3r 確認所有新成員和函式正確

### 後續 Step (Phase 1 其他任務)
| Step | 任務 | 前置依賴 |
|------|------|----------|
| 1.2 | ZCorrection 模組移植 | Step 1.1 `zcorrection_layers` ✅ |
| 1.3 | Archive 格式擴展 | Step 1.1 `sla_archive_format` ✅ |
| 1.4 | Rasterization 更新 | 無直接依賴 |
