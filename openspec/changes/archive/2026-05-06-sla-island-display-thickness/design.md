## Context

`GLGizmoLcdOverhangDetection` 繼承自 `GLGizmoPainterBase`。原本 island overlay 使用 `flat` shader + `P3` 格式（無法線）+ `GL_DEPTH_TEST` disabled + `GL_CULL_FACE` disabled，並繼承了 painter base 的球形游標和繪製上色功能。

實作後，架構改為三個 GLModel + 兩個 shader pass，並完整覆寫滑鼠操作行為：

| Model | Shader | Depth Test | Cull Face | 用途 |
|-------|--------|-----------|-----------|------|
| `m_island_original_model` | `flat` | OFF | 預設 | 所有 island 的原始 flat 輪廓（橘色，永遠可見） |
| `m_island_overlay_model`  | `gouraud_light` | ON | ON | 未選中 island 的擠出 solid（黃色） |
| `m_island_highlight_model`| `gouraud_light` | ON | ON | 選中 island 的擠出 solid（灰色） |

## Goals / Non-Goals

**Goals:**
- Overlay mesh 向下延伸，從各視角清晰可見
- 三層視覺層次：橘色輪廓 + 黃色 solid（未選）+ 灰色 solid（選中）
- Extruded solid 有正確法向量，gouraud_light 呈現真實光影
- 橘色輪廓永遠可見（depth test OFF），不被模型遮擋
- 移除無意義的球形游標和繪製功能
- 滑鼠左鍵以模型表面接觸點為旋轉中心，右鍵 pan

**Non-Goals:**
- 不影響 island detection 邏輯、座標系統
- 不影響任何 libslic3r 檔案
- 不修改其他繼承 GLGizmoPainterBase 的 gizmo 行為

## Decisions

### D1：三個 GLModel 分工

選中與未選中的 island 使用不同 model，避免同一 Z 的 z-fighting 並允許不同顏色。

### D2：P3N3 + gouraud_light（取代 P3 + flat）

Extruded solid 需要法向量才能呈現光影，讓 3D 形狀可辨。`gouraud_light` 是專案中標準的 lighting shader。

### D3：Normals 計算

**Top / Bottom face cap**：法向量固定為 `(0,0,±1)`，cap 內所有頂點共享相同法向量。

**Side walls**：每條邊用 **unshared vertices**（flat shading）。對 CCW polygon（libslic3r 標準），邊 j→j+1 的 outward normal：
```
dx = top[j+1].x - top[j].x
dy = top[j+1].y - top[j].y
normal = normalize(dy, -dx, 0)   // 右手法則，CCW 向外
```

Side wall winding（CCW from outside）：
```
triangle 1: (TL, BR, TR)   // sw+0, sw+2, sw+1
triangle 2: (TL, BL, BR)   // sw+0, sw+3, sw+2
```

### D4：view_normal_matrix

Overlay 幾何已在 world space，model_matrix = Identity。因此：
```cpp
view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3)
// = (view * Identity).block(0,0,3,3).inverse().transpose()
// = view_matrix.block(0,0,3,3)  （純旋轉，inverse = transpose 相消）
```

### D5：Depth test 分兩個 pass

- **Pass 1 (extruded solids)**：depth test ON → side walls 正確被模型遮擋，呈現自然 3D 效果
- **Pass 2 (original flat)**：depth test OFF → 橘色輪廓永遠顯示，不受模型幾何遮擋

Island 是 overhang 底面，`print_z` 在模型幾何體「下方」；若 depth test ON，橘色輪廓會被模型遮擋而不可見。

### D6：XY 縮放圍繞重心

```cpp
double sx = cx + (vertex_x - cx) * scale;
double sy = cy + (vertex_y - cy) * scale;
```

選擇 scale factor 而非 `offset_ex`（Clipper offset）的理由：
- `offset_ex` 需要引入 Clipper 依賴（目前 rebuild 函式不用）
- 對細小 island（半徑 < 1mm）`offset_ex` 可能因幾何退化失敗，scale factor 不會

### D7：顏色具名化

顏色改為 private static member function，方便日後調整：

```cpp
static ColorRGBA island_contour_color();  // 橘色 flat 輪廓
static ColorRGBA island_overlay_color();  // 黃色 extruded（未選）
static ColorRGBA island_selected_color(); // 灰色 extruded（選中）
```

### D8：選取切換時同步重建 overlay

因 `m_island_overlay_model` 排除選中的 island，切換選取時需同時呼叫 `rebuild_island_overlay_mesh()` 更新黃色 mesh，再呼叫 `rebuild_island_highlight_mesh()` 更新灰色 mesh。

### D9：移除球形游標與繪製功能

Island Detection 模式的目的是觀察和選取 island，不需要繪製上色。球形游標由 `render_cursor()` 渲染，完整覆寫 `on_mouse()` 後繪製邏輯（`gizmo_event()`）不再被觸發。

`render_painter_gizmo()` 移除 `render_cursor()` 呼叫，改為直接呼叫 `update_raycast_cache()` 維持 `m_rr` 的即時性（供 `on_mouse()` 的 orbit center 計算使用）。

### D10：on_mouse() 覆寫 — 表面接觸點 orbit

完整覆寫 `on_mouse()`，不呼叫父類實作：

| 事件 | 行為 | 理由 |
|------|------|------|
| LeftDown | 記錄 orbit center（hit point 或 bbox center），return true | 防止 canvas 處理物件選取 |
| LeftDrag | 呼叫 `camera.rotate_on_sphere_with_target()` | 自行 orbit，以表面點為中心 |
| LeftUp | 清除 drag 狀態，return true | |
| RightDown / RightDrag / RightUp | return false | canvas 執行標準 pan |
| Moving | return false | canvas 更新 hover 狀態 |

**Orbit 公式**（與 GLCanvas3D 完全一致）：
```cpp
// delta = 本次與上次滑鼠位置差（pixels）
Vec3d rot = Vec3d(delta.x(), delta.y(), 0.) * (M_PI * 0.8 / 180.) * orbit_mult;
// rot.x = 水平 pixel delta → azimuth（左右旋轉）
// rot.y = 垂直 pixel delta → zenith（上下翻轉）
camera.rotate_on_sphere_with_target(rot.x(), rot.y(), false, orbit_center);
```

注意：曾錯誤寫成 `Vec3d(delta.y(), delta.x(), ...)` 導致旋轉軸對調，正確為 `Vec3d(delta.x(), delta.y(), ...)`。

### D11：compute_orbit_center() — 世界座標轉換

`m_rr.hit` 是 mesh local space 座標。轉換到 world space：
```cpp
// vol_idx 對應 m_rr.mesh_id（只計算 is_model_part() volumes）
Transform3d trafo = mi->get_transformation().get_matrix() * mv->get_matrix();
Vec3d world_hit = trafo * m_rr.hit.cast<double>();
```
若 `m_rr.mesh_id < 0`（滑鼠不在模型上），fallback 使用 `selection.get_bounding_box().center()`。

### D12：GLGizmoPainterBase.hpp 存取層級調整

`update_raycast_cache()` 和 `m_rr`（含 `RaycastResult` struct）從 `private` 移至 `protected`，允許 derived class 在不渲染球形游標的情況下維持 raycast 狀態。

這是 additive 變更（只增加可見度），不影響其他繼承 `GLGizmoPainterBase` 的 gizmo（FdmSupports, Seam, MmuSegmentation, FuzzySkin）。

### D13：Print space → World space 座標轉換（修正版）

**問題根因：** `m_mesh_to_slice` 是由 `csg::model_to_csgmesh(*po.model_object(), po.trafo(), ...)` 建立，其中 `po.trafo() = sla_trafo()` 包含：
- Instance 的 X/Y rotation（傾斜）
- Instance 的 scale
- Z elevation（支撐高度）
- Shrinkage compensation（`S_corr`）
- **但不含** XY translation（被歸零）和 Z rotation（被歸零）

因此 contour 的 XY/Z 均在 **print space**（po.trafo() 已套用）。

**初版修法（不完整）：** 只加 `instances[0]->get_transformation().get_offset().xy`。
這對「純平移」和「X/Y tilt」正確，但在下列情況失敗：
- **Z rotation**：po.trafo() 把 R_z 歸零，inst_matrix 有真實 R_z → correction 仍包含 R_z，需套用
- **Scale + Z rotation 組合**：兩者殘差疊加，導致 XY 位置錯誤
- **Shrinkage compensation (S_corr ≠ 1)**：po.trafo() 含 S_corr，inst_matrix 不含 → XY 需除以 S_corr

**正確修法：**
```cpp
const Transform3d correction = inst_matrix * po.trafo().inverse();
Vec3d world = correction * Vec3d(px, py, ic.print_z);
```

這是完整的 print space → world space 轉換，涵蓋所有 transform 組合。

**Z 座標的一致性：** `correction * (px, py, print_z).z` 依賴 XY 輸入（當 correction 含 XY→Z coupling 時）。Flat original contour 用重心 `(cx, cy)` 計算參考 Z，與 extruded solid 保持一致，避免兩層 Z 不同。

套用範圍：`rebuild_island_overlay_mesh()`, `rebuild_island_original_mesh()`, `rebuild_island_highlight_mesh()`, `focus_camera_on_island()`。

## Key Parameters (current defaults)

| 參數 | 預設值 | 說明 |
|------|--------|------|
| `m_island_overlay_z_offset` | -0.05f | Overlay top face Z offset |
| `m_island_overlay_thickness` | 0.5f | 向下擠出深度（mm） |
| `m_island_overlay_scale` | 1.5f | XY 縮放倍率（overlay solid） |
| `m_island_highlight_scale` | 2.0f | XY 縮放倍率（highlight solid，備用） |

## Risks / Trade-offs

- **頂點數增加**：側牆用 unshared vertices，每個 island 約 6n+2 個頂點（shared 方案為 2n+2）。對一般 island 數量（< 50）可接受。
- **Alpha 深度排序**：depth test ON 時，semi-transparent solid 的 back/front face 順序由 z-buffer 決定。CULL_FACE ON 確保每個面只渲染一次，避免疊加混亂。
