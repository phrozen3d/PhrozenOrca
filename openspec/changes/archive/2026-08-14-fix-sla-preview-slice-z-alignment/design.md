## Context

SLA Preview 的 Z 座標流程涉及三個獨立子系統，各自對「第 N 層的世界座標 Z」有不同的計算起點：

### 切片格柵（SLAPrintSteps.cpp）

```
ilhd = object_config.layer_height          // ← 切片的初始層高
minZ = bb3d.min(Z) - get_elevation()       // bb3d 為 SLA TRAFO 座標系中的 bounding box
print_level[N] = scaled(minZ + ilhd + N × lhd)
```

layer 0 在世界座標中的實際 Z（instance_z = 0, corr_z = 1）：
```
world_Z[0] = print_level[0] × SCALING_FACTOR + elevation
           = (minZ + ilhd) + elevation
           = (-elevation + ilhd) + elevation
           = ilhd                          // = object_config.layer_height
```

### 舊的 m_sla_layers_z（GUI_Preview.cpp）

```cpp
double initial_layer_height = print->material_config().initial_layer_height.value;
m_sla_layers_z[N] = initial_layer_height + (print_level[N] - print_level[0]) * SCALING_FACTOR
                  = ILH_mat + N × LH_obj
```

layer 0 的 Z = `ILH_mat`（material 的 `initial_layer_height`）。

### 不一致點

`ILH_mat` 與 `ILH_obj`（object_config.layer_height）是**不同的 config 欄位**，不保證相等。當它們不同時：

```
clip plane at layer N  = ILH_mat + N × LH_obj
actual world_Z[N]      = ILH_obj + N × LH_obj
固定偏移 δ             = ILH_mat − ILH_obj
```

偏移 δ 為常數，與 N 無關，使得所有層都有相同的視覺位置誤差。

### 舊 key_high 公式的巧合正確性

```cpp
coord_t key_zero = obj->get_slice_index().front().print_level();
coord_t key_high = coord_t((clip_max_z - ILH_mat) / SCALING_FACTOR) + key_zero;
         = coord_t((ILH_mat + N*LH - ILH_mat) / SCALING_FACTOR) + print_level[0]
         = print_level[N]   ← 正確！
```

key_high 的 slice 選取本身是正確的，偏移在「clip plane Z」與「灰色 mesh 的實際裁切位置」之間——粉紅截面的 ExPolygons 來自正確的 layer，但粉紅截面渲染的 Z 位置（= clip_max_z）與灰色模型的 layer N 邊界（= world_Z[N]）相差 δ。

## Goals / Non-Goals

**Goals:**

- 讓 `m_sla_layers_z[N]` 直接對應切片格柵的 `print_level[N]`（透過 `+ elevation` 換算）
- 讓 `_render_sla_slices()` 的 key_high / key_low 使用與 `m_sla_layers_z` 相同的座標基準
- 消除 `material_config.initial_layer_height` 在 Preview Z 計算中的影響

**Non-Goals:**

- 不改動切片生成邏輯
- 不修改 SLA 2D canvas 的渲染路徑
- 不改變 `initial_layer_height` 在曝光時間計算中的功能（那是 `SLARasterizer` 的責任）

## Decisions

### D1：以 `print_level × SCALING_FACTOR + elevation` 作為唯一座標換算

**採用**。這是從切片格柵的定義直接推導出的換算式，不依賴任何外部 config 值，對所有 `initial_layer_height` / `layer_height` 的組合都正確。

```
world_Z = print_level × SCALING_FACTOR + elevation
print_level = (world_Z − elevation) / SCALING_FACTOR
```

**考慮過的替代方案**：改為讓 `SLAPrintSteps` 以 `material_config.initial_layer_height` 建立切片格柵的第一層（使 `ilhd = ILH_mat`）。已拒絕——切片格柵對所有層使用相同高度，`initial_layer_height` 在 SLA 中主要是曝光時間參數，強制讓它等於切片高度會破壞更大範圍的邏輯。

### D2：移除 key_high 中的 `key_zero` 錨點

原公式使用 `key_zero = print_level[0]` 作為相對偏移的錨點。新公式的 inverse 計算直接產生絕對 print_level 座標，不需錨點。兩者數學等價（在 `ILH_mat = ILH_obj` 時），但新公式在 `ILH_mat ≠ ILH_obj` 時仍正確。

## Risks / Tradeoffs

- **`get_current_elevation()` vs `get_elevation()`**：`m_sla_layers_z` 使用 `get_current_elevation()`，在全部步驟完成（包含 pad）時與 `get_elevation()` 相同；`_render_sla_slices()` 也使用 `get_current_elevation()`，保持兩者一致。切片前（尚未完成 `slapsRasterize`）不進入此流程，不造成影響。
- **多物件場景**：`m_sla_layers_z` 以迴圈收集所有物件的 slice index 後排序去重，此行為不變；`_render_sla_slices()` 對每個 `obj` 分別取 `get_current_elevation()`，行為不變。
- **行為回歸風險**：對 `ILH_mat == ILH_obj` 的材料（如 `sla_material_common.json`：`initial_layer_height = 0.05`，`layer_height = 0.05`），修改前後數值完全相同，無回歸風險。
