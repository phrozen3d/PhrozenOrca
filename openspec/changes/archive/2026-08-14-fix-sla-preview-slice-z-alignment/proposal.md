## Why

SLA 切片完成後進入 Preview，粉紅色截面（當前 layer 的實際切面）形狀與灰色 3D 模型在同一 Z 高度應有的截面明顯不符，尤其支撐頂端可看出截面來自另一高度。

### 根因

`GUI_Preview.cpp` 建立 `m_sla_layers_z` 時以 `material_config.initial_layer_height` 作為 layer 0 的 Z 偏移，但 `SLAPrintSteps::Steps()` 建立切片格柵時 `ilhd` 取自 `object_config.layer_height`——兩者是不同的 config 欄位，數值可能不同。

三個需要對齊的路徑：

| 路徑 | 舊公式 | 問題 |
|------|--------|------|
| `m_sla_layers_z[N]`（GUI_Preview.cpp） | `ILH_mat + N × LH_obj` | Z₀ 用 material 值 |
| clipping plane（Preview slider callback） | 直接使用 `m_sla_layers_z[N]` | 繼承 Z₀ 偏移 |
| `key_high` 反算（GLCanvas3D.cpp） | `(clip_z − ILH_mat) / SCALING_FACTOR + key_zero` | 反算抵消，slice 選取正確，但 render 位置隨 clip plane 偏移 |

Phrozen 兩支材料 profile 的實測偏移量：

| 材料 | `initial_layer_height` | `layer_height` | 固定偏移 |
|------|----------------------|----------------|---------|
| Phrozen Speed Plus - Black | 0.1 mm | 0.05 mm | **+1 層（0.05 mm）** |
| Phrozen Tough ABS-like+    | 0.1 mm | 0.05 mm | **+1 層（0.05 mm）** |

偏移在支撐頂端與模型底部交界處最為明顯：slider 停在最後一個支撐層時，粉紅截面顯示支撐尖端輪廓，但灰色模型的裁切位置已高出一層（對應第一個模型本體截面），兩者形狀完全不符。

## What Changes

以統一的座標公式取代原本依賴 `material_config.initial_layer_height` 的分散邏輯：

```
world_Z = print_level × SCALING_FACTOR + elevation
```

- **`GUI_Preview.cpp`**：`m_sla_layers_z` 改為 `rec.print_level() * SCALING_FACTOR + obj->get_current_elevation()`，移除對 `material_config.initial_layer_height` 的依賴
- **`GLCanvas3D.cpp`**：`_render_sla_slices()` 的 `key_high` / `key_low` 改為直接以 `(clip_z ∓ elevation) / SCALING_FACTOR` 反算，移除 `initial_layer_height` 與 `key_zero`

## Capabilities

### New Capabilities

- `sla-preview-slice-z-alignment`：定義 SLA Preview 中 layer slider Z 值、clipping plane、粉紅截面 print_level 反算三者的座標基準一致性不變式

### Modified Capabilities

（無）

## Impact

**修改的檔案：**
- `src/slic3r/GUI/GUI_Preview.cpp`：`on_sla_preview()` 中 `m_sla_layers_z` 建立邏輯（約 line 907–914）
- `src/slic3r/GUI/GLCanvas3D.cpp`：`_render_sla_slices()` 中 key_high / key_low 公式（約 line 8905–8913）

**不需修改：**
- 切片生成邏輯（`SLAPrintSteps.cpp`）
- 任何 API、資料結構、檔案格式
- FDM 程式碼
- SLA 2D canvas（`SLASlice2DCanvas`）
- 支撐生成演算法
