## Why

Island Detection gizmo 的 overlay 目前是貼在模型表面的單層半透明 flat polygon。細小的 island（面積僅幾 mm²）在 3D 視角下幾乎不可見——它們融入背景，尤其在斜視角時近乎消失。使用者無法快速識別需要支撐的危險區域。此外，原本繼承自 painter base 的球形游標和繪製上色功能對 island detection 模式無意義，並且干擾 3D view 的旋轉和平移操作。

## What Changes

**Island 視覺化：**
- 新增 `m_island_original_model`：所有 island 的原始（未縮放）flat 輪廓，橘色，永遠可見
- `rebuild_island_overlay_mesh()`：改為向下延伸的 extrude solid（P3N3 + gouraud_light），XY 縮放，排除已選 island
- `rebuild_island_highlight_mesh()`：改為灰色 extrude solid（與 overlay 同規格），取代舊的橘色 flat polygon
- 新增 `rebuild_island_original_mesh()`：建立 flat 橘色輪廓 mesh
- 新增 `build_island_extrude_p3n3()` static helper：帶法向量的擠出幾何
- 顏色管理改為具名 static member function

**操作行為改寫：**
- 移除球形游標（`render_cursor()` 不再呼叫）
- 移除繪製上色功能（`on_mouse()` 完整覆寫，不呼叫 `gizmo_event()`）
- 滑鼠左鍵拖曳 → 以模型表面接觸點為旋轉中心的 orbit（滑鼠不在模型上則使用 selection bbox center）
- 滑鼠右鍵拖曳 → 交由 canvas 執行標準 pan（gizmo 回傳 false）

## Capabilities

### New Capabilities

- `sla-island-display-thickness`: Island overlay 以有厚度的 extrude solid 呈現（P3N3 + gouraud_light，有光照），XY 放大並向下延伸，側牆斜視角清晰可見
- 三層視覺顯示：橘色原始輪廓（永遠可見）+ 黃色擠出 solid（未選）+ 灰色擠出 solid（選中）
- Island Detection 模式的 3D view 操作：左鍵以表面接觸點為旋轉中心，右鍵 pan

### Modified Capabilities

- `sla-island-contour-overlay`: Island overlay 的幾何結構從 flat polygon 改為 extrude solid；highlight 改為灰色擠出 solid；移除球形游標和繪製功能

## Impact

| 檔案 | 修改內容 |
|------|---------|
| `GLGizmoPainterBase.hpp` | `RaycastResult` struct、`m_rr`、`update_raycast_cache()` 從 `private` 移至 `protected` |
| `GLGizmoLcdOverhangDetection.hpp` | 新增 `m_island_original_model`、三個顏色 static function、導覽狀態成員、`on_mouse()` override 宣告 |
| `GLGizmoLcdOverhangDetection.cpp` | 移除 `render_cursor()` 呼叫，改為 raycast cache 更新 |
| `GLGizmoLcdOverhangDetection.cpp` | 新增 `on_mouse()` override（orbit / pan 導覽，無繪製）|
| `GLGizmoLcdOverhangDetection.cpp` | 新增 `compute_orbit_center()`（world-space 表面接觸點）|
| `GLGizmoLcdOverhangDetection.cpp` | 新增 `build_island_extrude_p3n3()` static helper（P3N3 法向量）|
| `GLGizmoLcdOverhangDetection.cpp` | 改寫 `rebuild_island_overlay_mesh()`（P3N3、gouraud_light、跳過選中 island）|
| `GLGizmoLcdOverhangDetection.cpp` | 新增 `rebuild_island_original_mesh()`（flat 橘色輪廓）|
| `GLGizmoLcdOverhangDetection.cpp` | 改寫 `rebuild_island_highlight_mesh()`（P3N3 灰色擠出 solid）|
| `GLGizmoLcdOverhangDetection.cpp` | 改寫 `render_island_contours()`（兩個 shader pass，depth test 分開控制）|
