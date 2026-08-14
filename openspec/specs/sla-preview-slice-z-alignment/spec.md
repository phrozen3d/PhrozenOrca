## ADDED Requirements

### Requirement: SLA Preview Z 座標三路徑一致性

`m_sla_layers_z`、clipping plane 以及 `_render_sla_slices()` 的 print_level 反算 SHALL 全部基於統一的座標換算：

```
world_Z  =  print_level × SCALING_FACTOR + elevation
print_level  =  (world_Z − elevation) / SCALING_FACTOR
```

其中 `elevation = obj->get_current_elevation()`。不得以 `material_config.initial_layer_height` 作為 layer 0 的 Z 偏移。

#### Scenario: m_sla_layers_z 直接對應切片格柵

- **WHEN** `on_sla_preview()` 建立 `m_sla_layers_z` 時
- **THEN** 第 N 筆值 SHALL 等於 `obj->get_slice_index()[N].print_level() × SCALING_FACTOR + obj->get_current_elevation()`
- **AND** 不依賴 `material_config.initial_layer_height` 的數值

#### Scenario: material initial_layer_height 與 object layer_height 不同時仍對齊

- **WHEN** `material_config.initial_layer_height`（如 0.1 mm）與 `object_config.layer_height`（如 0.05 mm）不相等
- **THEN** 粉紅色截面的渲染 Z 位置 SHALL 與灰色 3D 模型對應 layer 的裁切 Z 位置一致，視覺上無可見偏移

#### Scenario: key_high 反算正確找到 print_level[N]

- **WHEN** clipping plane 設為 `m_sla_layers_z[N]`（slider 在第 N 層）
- **THEN** `_render_sla_slices()` 計算的 `key_high` SHALL 等於 `obj->get_slice_index()[N].print_level()`
- **AND** `closest_slice_to_print_level(key_high)` SHALL 返回第 N 層的 `SliceRecord`

#### Scenario: key_low 反算正確找到相鄰的下一層

- **WHEN** clipping plane 下限設為 `m_sla_layers_z[M]`（slider 下限在第 M 層）
- **THEN** `_render_sla_slices()` 計算的 `key_low` SHALL 等於 `obj->get_slice_index()[M+1].print_level()`（M+1 層之底面即 M 層之頂面，用於渲染底部截面帽）

#### Scenario: 支撐頂端截面形狀與 3D 模型一致

- **WHEN** layer slider 停在支撐結構頂端（最後一個純支撐層）
- **THEN** 粉紅色截面所顯示的 ExPolygons 形狀 SHALL 與灰色 3D 模型在同一 world Z 的截面形狀吻合（支撐尖端對支撐尖端，不顯示其他高度的截面）
