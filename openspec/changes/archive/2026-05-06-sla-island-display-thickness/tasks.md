## 1. HPP：新增顯示參數成員與 GLModel

- [x] 1.1 在 `GLGizmoLcdOverhangDetection.hpp` 的 protected 區段新增三個 float 成員：
  ```cpp
  float m_island_overlay_thickness = 0.5f;   // downward extrusion depth (mm)
  float m_island_overlay_scale     = 1.5f;   // XY scale factor for overlay (around centroid)
  float m_island_highlight_scale   = 2.0f;   // XY scale factor for highlight (around centroid)
  ```
- [x] 1.2 新增 `GLModel m_island_original_model` 到 protected 區段（flat 橘色輪廓）
- [x] 1.3 在 private 區段新增三個顏色 static member function：
  ```cpp
  static ColorRGBA island_contour_color();   // orange flat original contour
  static ColorRGBA island_overlay_color();   // yellow extruded solid (unselected)
  static ColorRGBA island_selected_color();  // gray extruded solid (selected)
  ```
- [x] 1.4 在 private 區段新增 `void rebuild_island_original_mesh()` 宣告

**驗收**：編譯通過；四個參數及新 model 可在 cpp 中存取。

---

## 2. CPP：新增 `build_island_extrude_p3n3()` static helper

- [x] 2.1 在 rebuild 函式前新增 static free function，接受 `geo / center_top / center_bot / top_verts / bot_verts`
- [x] 2.2 建立頂面 fan（normal = `(0,0,1)`，所有頂點共享）
- [x] 2.3 建立底面 fan（reversed winding，normal = `(0,0,-1)`）
- [x] 2.4 建立側牆（unshared vertices per edge，outward normal = `normalize(dy,-dx,0)`，winding `(TL,BR,TR)` + `(TL,BL,BR)`）

**驗收**：編譯通過；helper 可被 overlay 和 highlight rebuild 函式呼叫。

---

## 3. CPP：改寫 `rebuild_island_overlay_mesh()` — P3N3 + 跳過選中

- [x] 3.1 格式改為 `EVertexLayout::P3N3`
- [x] 3.2 加入 `flat_idx` 計數，跳過 `m_current_overhang_area_index` 對應的 island
- [x] 3.3 使用 `build_island_extrude_p3n3()` 建立幾何
- [x] 3.4 在函式結尾呼叫 `rebuild_island_original_mesh()`（兩個 mesh 永遠同步）
- [x] 3.5 顏色改用 `island_overlay_color()`

**驗收**：編譯通過；未選中的 island 顯示黃色擠出 solid，從斜角可見側牆光影。

---

## 4. CPP：新增 `rebuild_island_original_mesh()`

- [x] 4.1 建立 `P3` flat fan polygon（原始未縮放輪廓，所有 island）
- [x] 4.2 Z 位置：`print_z + m_island_overlay_z_offset`
- [x] 4.3 顏色使用 `island_contour_color()`

**驗收**：編譯通過；所有 island 均顯示橘色原始輪廓。

---

## 5. CPP：改寫 `rebuild_island_highlight_mesh()` — P3N3 灰色擠出 solid

- [x] 5.1 格式改為 `EVertexLayout::P3N3`
- [x] 5.2 使用 `m_island_overlay_scale`（與 overlay 同尺寸）和相同 `z_top / z_bot`
- [x] 5.3 使用 `build_island_extrude_p3n3()` 建立幾何
- [x] 5.4 顏色改用 `island_selected_color()`

**驗收**：編譯通過；選中的 island 顯示灰色擠出 solid（取代舊的橘色 flat polygon）。

---

## 6. CPP：改寫 `render_island_contours()` — 兩個 shader pass

- [x] 6.1 移除舊的 `flat` shader 單一 pass
- [x] 6.2 **Pass 1 (gouraud_light)**：渲染 `m_island_overlay_model` + `m_island_highlight_model`
  - 設定 `view_model_matrix / projection_matrix / view_normal_matrix / emission_factor`
  - `view_normal_matrix = view_matrix.matrix().block(0,0,3,3)`（world space，model_matrix = Identity）
  - Depth test ON，CULL_FACE ON（預設），GL_BLEND ON
- [x] 6.3 **Pass 2 (flat)**：渲染 `m_island_original_model`
  - `glDisable(GL_DEPTH_TEST)` 包圍此 pass
  - 確保橘色輪廓永遠可見
- [x] 6.4 移除多餘的 `GL_CULL_FACE` disable/enable 操作

**驗收**：編譯通過；三層顯示效果正確；extruded solid 有光影；橘色輪廓永遠可見。

---

## 7. CPP：選取切換同步重建

- [x] 7.1 左箭頭按鈕：在 `rebuild_island_highlight_mesh()` 前加入 `rebuild_island_overlay_mesh()`
- [x] 7.2 右箭頭按鈕：同上
- [x] 7.3 Detect Selected 完成路徑（lines 257, 509）：同上

**驗收**：切換選取時，黃色 overlay 正確排除新選中的 island；選中的 island 立即轉為灰色。

---

## 8. 驗收測試（視覺化）

- [x] 8.1 斜視角下 side walls 清晰可見，有光照明暗變化（非 flat 均一顏色）
- [x] 8.2 Overlay XY 範圍為原始輪廓 1.5 倍
- [x] 8.3 橘色原始輪廓從任何角度均可見（不被模型遮擋）
- [x] 8.4 選中 island 顯示灰色，未選中顯示黃色
- [x] 8.5 切換選取時黃/灰正確切換
- [x] 8.6 關閉 Gizmo 後三個 model 均 reset，無 GL 資源殘留

---

## 9. 移除球形游標 + 覆寫滑鼠導覽行為

- [x] 9.1 `GLGizmoPainterBase.hpp`：將 `RaycastResult` struct、`m_rr`、`update_raycast_cache()` 從 `private` 移至 `protected`
  - 確認其他 painter gizmo（FdmSupports, Seam, MmuSegmentation, FuzzySkin）不受影響
- [x] 9.2 `render_painter_gizmo()`：移除 `render_cursor()` 呼叫，改為直接呼叫 `update_raycast_cache()` 維持 `m_rr` 即時性
- [x] 9.3 HPP 新增 `on_mouse()` override 宣告和三個 navigation 私有成員：
  ```cpp
  bool  m_nav_dragging     = false;
  Vec2d m_nav_drag_start   = Vec2d::Zero();
  Vec3d m_nav_orbit_center = Vec3d::Zero();
  ```
- [x] 9.4 HPP 新增 `Vec3d compute_orbit_center() const` 宣告
- [x] 9.5 CPP 實作 `compute_orbit_center()`：`m_rr.mesh_id >= 0` 時轉換 mesh local → world space；否則 fallback 至 `selection.get_bounding_box().center()`
- [x] 9.6 CPP 實作 `on_mouse()` override：
  - LeftDown → 記錄 orbit center + drag start，return true
  - LeftDrag → `rot = Vec3d(Δx, Δy, 0) * (π × 0.8 / 180°) * orbit_mult`，呼叫 `rotate_on_sphere_with_target`，return true
  - LeftUp → 清除 drag 狀態，return true
  - Right / Middle / Moving → return false（canvas 處理）
- [x] 9.7 **修正軸對調 bug**：旋轉公式初始錯誤為 `Vec3d(Δy, Δx, 0)`，修正為 `Vec3d(Δx, Δy, 0)` 與 canvas 一致

**驗收**：
- [x] 9.8 模型表面上方無球形游標顯示
- [x] 9.9 左鍵拖曳在模型上方時，以接觸點為旋轉中心執行 orbit（左右旋轉 / 上下翻轉方向正確）
- [x] 9.10 左鍵拖曳在模型外時，以 selection bbox center 為旋轉中心
- [x] 9.11 右鍵拖曳執行 pan，與一般模式行為一致
