# SLA 第二段抬升/下降參數新增記錄

**建立日期**: 2026-03-06
**分支**: phrozen-resin-dev
**涉及檔案**:
- [PrintConfig.hpp](../src/libslic3r/PrintConfig.hpp)
- [PrintConfig.cpp](../src/libslic3r/PrintConfig.cpp)
- [Tab.cpp](../src/slic3r/GUI/Tab.cpp)

---

## 一、修改原因

光固化（MSLA/DLP）印表機在執行抬升（Lift）與下降（Retract）動作時，常見的進階控制方式是將一次完整的抬升/下降動作分為**兩段**，各段可設定不同距離與速度，以達到：

1. **防止分層失敗（Delamination）**：第一段以低速抬升離開成型面，第二段以高速快速完成剩餘行程，縮短列印時間。
2. **減少支撐斷裂**：緩起動避免模型與 FEP/nFEP 薄膜之間因真空吸力導致的瞬間拉扯力。
3. **對齊主流切片器格式**：Chitubox、Lychee Slicer 等均支援雙段 Lift/Retract 參數（`LiftingSpeed` + `LiftingSpeed2`），新增這些參數可確保未來匯出格式的完整性。

---

## 二、新增參數總覽

共新增 **8 個參數**（含上次的 `lift_second_distance`，本次補齊完整對稱組）：

### 距離類（單位：mm）

| 參數名稱 | 中文說明 | 預設值 | 對應第一段參數 |
|---------|---------|--------|--------------|
| `lift_second_distance` | 一般層第二段抬升距離 | 7.0 mm | `lifting_distance` (7.0) |
| `retract_second_distance` | 一般層第二段下降距離 | 7.0 mm | `retract_distance` (7.0) |
| `bottom_lift_second_distance` | 底層第二段抬升距離 | 8.0 mm | `bottom_lift_distance` (8.0) |
| `bottom_retract_second_distance` | 底層第二段下降距離 | 8.0 mm | `bottom_retract_distance` (8.0) |

### 速度類（單位：mm/min）

| 參數名稱 | 中文說明 | 預設值 | 對應第一段參數 |
|---------|---------|--------|--------------|
| `lift_second_speed` | 一般層第二段抬升速度 | 45.0 mm/min | `lifting_speed` (45.0) |
| `retract_second_speed` | 一般層第二段下降速度 | 150.0 mm/min | `retract_speed` (150.0) |
| `bottom_lift_second_speed` | 底層第二段抬升速度 | 45.0 mm/min | `bottom_lift_speed` (45.0) |
| `bottom_retract_second_speed` | 底層第二段下降速度 | 150.0 mm/min | `bottom_retract_speed` (150.0) |

---

## 三、如何修改（步驟說明）

依照 [SLA_UI_Parameter_Architecture.md](SLA_UI_Parameter_Architecture.md) 第八章「新增 SLA 參數的完整步驟」操作，本次修改涉及步驟 1、2、3。

### Step 1：PrintConfig.hpp — 新增 Config Class Member

**檔案**：`src/libslic3r/PrintConfig.hpp`
**位置**：`PRINT_CONFIG_CLASS_DERIVED_DEFINE` macro 中的 `SLAMaterialConfig` 區塊

在每個第一段參數之後，緊接加入對應的第二段參數：

```cpp
// 距離群組（插入方式）
((ConfigOptionFloats, bottom_lift_distance))
((ConfigOptionFloats, bottom_lift_second_distance))   // ← 插入於此
((ConfigOptionFloats, lifting_distance))
((ConfigOptionFloats, lift_second_distance))           // ← 插入於此
((ConfigOptionFloats, bottom_retract_distance))
((ConfigOptionFloats, bottom_retract_second_distance)) // ← 插入於此
((ConfigOptionFloats, retract_distance))
((ConfigOptionFloats, retract_second_distance))        // ← 插入於此

// 速度群組（插入方式）
((ConfigOptionFloat, bottom_lift_speed))
((ConfigOptionFloat, bottom_lift_second_speed))       // ← 插入於此
((ConfigOptionFloat, lifting_speed))
((ConfigOptionFloat, lift_second_speed))              // ← 插入於此
((ConfigOptionFloat, bottom_retract_speed))
((ConfigOptionFloat, bottom_retract_second_speed))    // ← 插入於此
((ConfigOptionFloat, retract_speed))
((ConfigOptionFloat, retract_second_speed))           // ← 插入於此
```

**型別說明**：使用 `ConfigOptionFloats`（複數），配合 `dual_float` GUI type，UI 顯示為「主值 + 次值」兩欄位。

---

### Step 2：PrintConfig.cpp — 新增 Option 定義

**檔案**：`src/libslic3r/PrintConfig.cpp`
**函式**：`PrintConfigDef::init_sla_params()`

每個新參數緊接在其對應的第一段參數定義之後加入，格式與第一段完全一致，僅 key、label、tooltip 有別：

```cpp
// 範例：lift_second_distance 定義
def                          = this->add("lift_second_distance", coFloats);
def->label                   = L("Lift Second Distance");
def->category                = L("Distance");
def->tooltip                 = L("Second stage lifting distance.");
def->sidetext                = "mm";
def->min                     = 0;
def->gui_type                = ConfigOptionDef::GUIType::dual_float;
def->dual_float_width        = 5;
def->dual_float_width_second = 8;
def->set_default_value(new ConfigOptionFloats({7.0, 0.0}));
```

**所有距離參數共用屬性**：
- `coFloats` / `ConfigOptionDef::GUIType::dual_float`
- `dual_float_width = 5`、`dual_float_width_second = 8`
- 單位 `mm`、`min = 0`

**所有速度參數共用屬性**：
- `coFloat`（純量，**非** `coFloats`）/ 不使用 `dual_float` GUI type
- 單位 `mm/min`、`min = 0`

> **注意**：速度參數型別為 `coFloat`，與距離參數的 `coFloats` 不同。在 `PhrozenPRZ.cpp` 中必須用 `cfg_f()` 存取，不能用 `cfg_floats0()`（後者只能讀 `coFloats`，讀速度時 cast 失敗會靜默回傳 0）。

---

### Step 3：Tab.cpp — 在 build() 中加入 UI 行

**檔案**：`src/slic3r/GUI/Tab.cpp`
**函式**：`TabSLAPrint::build()`

在對應的頁面（Distance / Speed）的同一個 optgroup 中，各第一段參數之後插入第二段：

**Distance 頁面**（`Lift & Retract Distance` 群組）：
```cpp
optgroup->append_single_option_line("bottom_lift_distance", "123");
optgroup->append_single_option_line("bottom_lift_second_distance", "123");  // ← 新增
optgroup->append_single_option_line("lifting_distance", "123");
optgroup->append_single_option_line("lift_second_distance", "123");          // ← 新增
optgroup->append_single_option_line("bottom_retract_distance", "123");
optgroup->append_single_option_line("bottom_retract_second_distance", "123"); // ← 新增
optgroup->append_single_option_line("retract_distance", "123");
optgroup->append_single_option_line("retract_second_distance", "123");        // ← 新增
```

**Speed 頁面**（`Lift & Retract Speed` 群組）：
```cpp
optgroup->append_single_option_line("bottom_lift_speed", "123");
optgroup->append_single_option_line("bottom_lift_second_speed", "123");       // ← 新增
optgroup->append_single_option_line("lifting_speed", "123");
optgroup->append_single_option_line("lift_second_speed", "123");              // ← 新增
optgroup->append_single_option_line("bottom_retract_speed", "123");
optgroup->append_single_option_line("bottom_retract_second_speed", "123");    // ← 新增
optgroup->append_single_option_line("retract_speed", "123");
optgroup->append_single_option_line("retract_second_speed", "123");           // ← 新增
```

---

## 四、修改內容（Diff 摘要）

### PrintConfig.hpp — 新增 8 個 member

```diff
  ((ConfigOptionFloats, bottom_lift_distance))
+ ((ConfigOptionFloats, bottom_lift_second_distance))
  ((ConfigOptionFloats, lifting_distance))
+ ((ConfigOptionFloats, lift_second_distance))
  ((ConfigOptionFloats, bottom_retract_distance))
+ ((ConfigOptionFloats, bottom_retract_second_distance))
  ((ConfigOptionFloats, retract_distance))
+ ((ConfigOptionFloats, retract_second_distance))
  ((ConfigOptionFloat, bottom_lift_speed))
+ ((ConfigOptionFloat, bottom_lift_second_speed))
  ((ConfigOptionFloat, lifting_speed))
+ ((ConfigOptionFloat, lift_second_speed))
  ((ConfigOptionFloat, bottom_retract_speed))
+ ((ConfigOptionFloat, bottom_retract_second_speed))
  ((ConfigOptionFloat, retract_speed))
+ ((ConfigOptionFloat, retract_second_speed))
```

### PrintConfig.cpp — 新增 8 個 option 定義

| 參數 key | label | category | default |
|---------|-------|----------|---------|
| `bottom_lift_second_distance` | Bottom Lift Second Distance | Distance | {8.0, 0.0} |
| `lift_second_distance` | Lift Second Distance | Distance | {7.0, 0.0} |
| `bottom_retract_second_distance` | Bottom Retract Second Distance | Distance | {8.0, 0.0} |
| `retract_second_distance` | Retract Second Distance | Distance | {7.0, 0.0} |
| `bottom_lift_second_speed` | Bottom Lift Second Speed | Speed | 45.0 |
| `lift_second_speed` | Lift Second Speed | Speed | 45.0 |
| `bottom_retract_second_speed` | Bottom Retract Second Speed | Speed | 150.0 |
| `retract_second_speed` | Retract Second Speed | Speed | 150.0 |

### Tab.cpp — Distance 頁面 +4 行、Speed 頁面 +4 行

```diff
  // Distance page - Lift & Retract Distance group
  optgroup->append_single_option_line("bottom_lift_distance", "123");
+ optgroup->append_single_option_line("bottom_lift_second_distance", "123");
  optgroup->append_single_option_line("lifting_distance", "123");
+ optgroup->append_single_option_line("lift_second_distance", "123");
  optgroup->append_single_option_line("bottom_retract_distance", "123");
+ optgroup->append_single_option_line("bottom_retract_second_distance", "123");
  optgroup->append_single_option_line("retract_distance", "123");
+ optgroup->append_single_option_line("retract_second_distance", "123");

  // Speed page - Lift & Retract Speed group
  optgroup->append_single_option_line("bottom_lift_speed", "123");
+ optgroup->append_single_option_line("bottom_lift_second_speed", "123");
  optgroup->append_single_option_line("lifting_speed", "123");
+ optgroup->append_single_option_line("lift_second_speed", "123");
  optgroup->append_single_option_line("bottom_retract_speed", "123");
+ optgroup->append_single_option_line("bottom_retract_second_speed", "123");
  optgroup->append_single_option_line("retract_speed", "123");
+ optgroup->append_single_option_line("retract_second_speed", "123");
```

---

## 五、後續待辦

- [ ] 在 slicing pipeline（`SLAPrint.cpp`）中讀取第二段參數，實際影響輸出的 `.ctb`/`.phz` 等格式的抬升控制指令
- [ ] Profile .ini 文件同步（若有針對特定機台的 override 需要設定）
- [ ] 視需要在 `ConfigManipulation.cpp` 的 `toggle_print_sla_options()` 加入顯示/隱藏邏輯（例如第二段距離 = 0 時隱藏速度欄位）
