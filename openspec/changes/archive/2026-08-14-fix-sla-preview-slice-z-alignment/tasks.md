## 1. 修改 m_sla_layers_z 建立邏輯

- [x] 1.1 `src/slic3r/GUI/GUI_Preview.cpp`：移除 `initial_layer_height` 變數（`material_config.initial_layer_height`）與 `low_coord` 錨點
- [x] 1.2 改為 `rec.print_level() * SCALING_FACTOR + obj->get_current_elevation()`，對每個 object 分別取 `elevation`

## 2. 修改 _render_sla_slices() key 反算公式

- [x] 2.1 `src/slic3r/GUI/GLCanvas3D.cpp`：移除 `initial_layer_height` 變數與 `key_zero` 錨點
- [x] 2.2 `key_high` 改為 `coord_t((clip_max_z - elevation) / SCALING_FACTOR)`
- [x] 2.3 `key_low` 改為 `coord_t((clip_min_z - elevation + layer_height) / SCALING_FACTOR)`
- [x] 2.4 `elevation` 取自 `obj->get_current_elevation()`（與 `m_sla_layers_z` 路徑一致）

## 3. 驗收

- [x] 3.1 使用 Phrozen Speed Plus - Black 或 Tough ABS-like+（`initial_layer_height=0.1`, `layer_height=0.05`）切片後，粉紅截面與灰色模型在支撐頂端視覺對齊
- [x] 3.2 使用 `initial_layer_height=layer_height`（如 `sla_material_common.json`）切片，行為與修改前一致（無回歸）
- [x] 3.3 沿 slider 全程拖動，粉紅截面不出現跳躍或位置錯誤
- [x] 3.4 底部截面帽（clip_min_z 端）顯示正常
