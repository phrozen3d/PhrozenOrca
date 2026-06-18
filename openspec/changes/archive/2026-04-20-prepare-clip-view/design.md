# Design: Prepare View Global Z-Clip Slider（修訂版 v2）

## 架構現狀

### 現有 Clipping 管線總覽

#### Gizmo 路徑（ObjectClipper）

```
Gizmo ImGui "View clipping" slider
  → m_c->object_clipper()->set_position_by_ratio(ratio, true)
  → ObjectClipper 更新 m_clp (ClippingPlane at -UnitZ)
  → GLGizmosManager::get_clipping_plane()
      if position == 0 → ClipsNothing()
      else → ClippingPlane(-normal, offset)
  → GLCanvas3D::_render_objects():
      m_camera_clipping_plane = m_gizmos.get_clipping_plane()
  → m_volumes.set_clipping_plane(m_camera_clipping_plane.get_data())
  → GLSL shader: clipping_planes_dots → fragment discard
```

#### Preview 路徑（m_clipping_planes）

```
IMSlider（Preview 右側雙 bar）
  → on_sla_layer_slider_changed()
  → canvas->set_clipping_plane(0, ClippingPlane(+UnitZ, -z_low))   // 下截面
  → canvas->set_clipping_plane(1, ClippingPlane(-UnitZ,  z_high))  // 上截面
  → GLCanvas3D::m_clipping_planes[2] 更新
  → m_use_clipping_planes = true
  → _render_objects() 中 m_volumes.set_z_range(-z_low, z_high)
  → _render_sla_slices() 渲染 cap mesh（有切片結果時）
```

#### 關鍵差異

| 面向 | Gizmo / ObjectClipper 路徑 | Preview / m_clipping_planes 路徑 |
|------|--------------------------|----------------------------------|
| 截面來源 | `object_clipper()->get_clipping_plane()` | `m_clipping_planes[0/1]` |
| Shader uniform | `clipping_plane`（任意平面）| `z_range`（Z 軸上下限）|
| Raycasting clip | ✅ `is_point_clipped()` 過濾 | ❌ 不暴露給 raycaster |
| Cap mesh | `ObjectClipper::render_cut()` | `_render_sla_slices()`（需切片）|
| 實例範圍 | 各 Gizmo 的 CommonGizmosDataPool | 各 canvas 獨立的 m_clipping_planes |

---

## 設計決策

### D1：Slider 永遠顯示（SLA 模式）

**選擇：只檢查 `current_printer_technology() == ptSLA && canvas_type == CanvasView3D`，不檢查物件選取狀態**

理由：
- 使用者需要在任何狀態下（含無物件選取、gizmo 激活中）都能操作截面
- 若場景無物件，range 使用預設值（scene_max_z = 50.0），slider 存在但無效果

邊界：
- 切換到 FDM 印表機 → slider 不顯示
- 切換到 Preview tab → 另一個 canvas 實例，不受影響

### D2：Range 計算（全場景 max Z）

```cpp
// 取場景所有物件（含 instance transform）的世界座標 max Z
double scene_max_z = 0.0;
for (const ModelObject* obj : m_model->objects)
    for (const ModelInstance* inst : obj->instances)
        scene_max_z = std::max(scene_max_z, 
            inst->get_matrix() * obj->bounding_box().max.z());
if (scene_max_z <= 0.0)
    scene_max_z = 50.0;  // 預設值，無物件時

// slider range:
//   ratio=0.0 → z = 0.0   （下限固定 Z=0）
//   ratio=1.0 → z = scene_max_z
```

**注意：下限固定為 Z=0，不跟隨物件 bbox min Z。**

### D3：Slider UI 實作（複用 IMSlider）

**選擇：在 view3D canvas 上加入一個 IMSlider 實例（垂直雙 bar）**

位置：Prepare canvas 右側浮動，與 Preview slider 相同位置。

實作方式：
- 在 `GLCanvas3D` 新增私有 `IMSlider* m_prepare_clip_slider = nullptr`
- SLA 模式時初始化（或延遲初始化）
- 在 `_render_imgui_overlay()` 或同等函式中呼叫 `m_prepare_clip_slider->render()`
- slider 以 **絕對 Z 值（mm）** 為單位（不用 ratio），配合 scene_max_z

或使用兩個 `ImGui::VSliderFloat` 手動實作（若 IMSlider 耦合度過高）。

觸發條件（AND）：
1. `m_canvas_type == CanvasView3D`
2. `current_printer_technology() == ptSLA`
3. （不需要物件選取條件）

### D4：雙系統同步策略

Prepare Slider 移動時**同時**更新兩個系統：

```cpp
void GLCanvas3D::_on_prepare_clip_changed(double z_low, double z_high) {
    // 1. 視覺 Z-range（shader）
    if (z_low == 0.0 && z_high >= scene_max_z) {
        m_use_clipping_planes = false;
    } else {
        set_clipping_plane(0, ClippingPlane(Vec3d::UnitZ(), -z_low));
        set_clipping_plane(1, ClippingPlane(-Vec3d::UnitZ(), z_high));
        m_use_clipping_planes = true;
    }
    
    // 2. ObjectClipper 同步（raycasting + cap mesh）
    if (!m_syncing_clipper) {
        m_syncing_clipper = true;
        double ratio = (scene_max_z > 0) ? z_high / scene_max_z : 1.0;
        ratio = std::clamp(ratio, 0.0, 1.0);
        m_gizmos.common_gizmos_data()->object_clipper()
            ->set_position_by_ratio(ratio < 1.0 ? ratio : -1.0, false);
        m_syncing_clipper = false;
    }
    
    set_as_dirty();
}
```

**注意**：ObjectClipper 只同步 **top（z_high）**，底部（z_low）僅由 shader Z-range 控制。
ObjectClipper 內部只有單截面概念，bottom clip 不影響 raycasting。

### D5：Gizmo 進場同步（on_set_state: On）

在 `GLGizmoSlaBase::on_set_state(On)` 加入：

```cpp
// 從全域 prepare slider 反推 ObjectClipper 位置
if (m_parent.get_use_clipping_planes()) {
    double z_high = m_parent.get_clipping_planes()[1].get_data()[3];
    double scene_max_z = /* 同上計算 */;
    double ratio = (scene_max_z > 0) ? z_high / scene_max_z : 1.0;
    ratio = std::clamp(ratio, 0.0, 1.0);
    if (ratio < 1.0)
        m_c->object_clipper()->set_position_by_ratio(ratio, false);
    // else: 不設定（維持 position=0 = ClipsNothing）
}
```

### D6：Gizmo 離場同步（on_set_state: Off）

在 `GLGizmoSlaBase::on_set_state(Off)` 加入：

```cpp
// 從 ObjectClipper 讀取當前位置，回寫至 m_clipping_planes
double pos = m_c->object_clipper()->get_position();
if (pos > 0.0 && pos <= 1.0) {
    double scene_max_z = /* 同上計算 */;
    double z_high = pos * scene_max_z;
    m_parent.set_clipping_plane(1, ClippingPlane(-Vec3d::UnitZ(), z_high));
    m_parent.set_use_clipping_planes(true);
}
```

### D7：移除 Gizmo 內部 View Clipping Slider

從以下檔案移除 "View clipping" UI 渲染程式碼（保留 ObjectClipper 基礎設施）：
- `GLGizmoHollow.cpp` → `on_render_input_window()` 中的 view_clipping 區塊
- `GLGizmoDrill.cpp` → 同上
- `GLGizmoSlaSupports.cpp` → 同上

**保留**：
- `set_position_by_ratio()` 在滾輪事件中的呼叫（若有）
- ObjectClipper 本身的初始化和 raycasting 邏輯

### D8：参數共用分析

**結論：不與 Preview 共用，機制相同但實例獨立。**

| 比較項目 | 說明 |
|---------|------|
| `m_clipping_planes[2]` | view3D 和 preview 各自獨立的成員 |
| `m_use_clipping_planes` | 同上，獨立 |
| IMSlider | view3D 新增自己的實例，與 preview 的 m_layers_slider 無關 |
| `on_sla_layer_slider_changed()` | Preview 獨有，不呼叫 |

共用的**只有程式碼模式**（呼叫同樣的 set_clipping_plane API），
實際執行時兩個 canvas 完全隔離。

---

## 元件修改清單

```
GLCanvas3D.hpp / .cpp
├── [新增] bool m_syncing_clipper = false
├── [新增] IMSlider* m_prepare_clip_slider = nullptr（或手動 ImGui VSlider）
├── [新增] double m_prepare_scene_max_z = 50.0  // 快取，每幀更新或物件變更時更新
├── [新增] void _render_prepare_clip_slider()    // 渲染 slider UI
├── [新增] void _on_prepare_clip_changed(double z_low, double z_high)  // 同步兩個系統
└── [修改] _render_imgui_overlay() → 加入 _render_prepare_clip_slider() 呼叫

GLGizmoSlaBase.cpp
├── [修改] on_set_state(On) → 從 m_clipping_planes[1] 初始化 ObjectClipper
└── [修改] on_set_state(Off) → ObjectClipper position 回寫 m_clipping_planes

GLGizmoHollow.cpp
└── [修改] on_render_input_window() → 移除 "View clipping" slider 區塊

GLGizmoDrill.cpp
└── [修改] on_render_input_window() → 移除 "View clipping" slider 區塊

GLGizmoSlaSupports.cpp
└── [修改] on_render_input_window() → 移除 "View clipping" slider 區塊（若有）
```

---

## 資料流（最終設計）

### Prepare Slider 移動時

```
IMSlider / VSlider 值變化（z_low, z_high）
  → _on_prepare_clip_changed(z_low, z_high)
      → set_clipping_plane(0, bottom_plane)   // shader Z-range
      → set_clipping_plane(1, top_plane)
      → m_use_clipping_planes = true
      → object_clipper()->set_position_by_ratio(ratio_high)  // raycasting + cap
      → set_as_dirty()
```

### 進入 Gizmo 時（on_set_state: On）

```
  → 讀取 m_clipping_planes[1] 的 z_high
  → 計算 ratio = z_high / scene_max_z
  → object_clipper()->set_position_by_ratio(ratio)
  // Gizmo 的 UI slider 已移除，ObjectClipper 由全域 slider 驅動
```

### 離開 Gizmo 時（on_set_state: Off）

```
  → 讀取 object_clipper()->get_position() = ratio
  → z_high = ratio * scene_max_z
  → set_clipping_plane(1, ClippingPlane(-UnitZ, z_high))
  // 確保全域 slider 和 ObjectClipper 一致
```

### 下一幀渲染

```
_render_objects():
  if m_use_clipping_planes:
    m_volumes.set_z_range(-z_low, z_high)          // shader Z-range clip
  m_camera_clipping_plane = m_gizmos.get_clipping_plane()  // ObjectClipper（gizmo 激活時）
  m_volumes.set_clipping_plane(camera_clip)
```

---

## 風險與對策

| 風險 | 說明 | 對策 |
|------|------|------|
| scene_max_z 計算效能 | 每幀遍歷所有物件 | 快取 m_prepare_scene_max_z，僅在物件變更時更新 |
| ObjectClipper 未初始化 | 無物件選取時 object_clipper 可能無效 | 進入 _on_prepare_clip_changed 前先檢查 m_c 是否有效 |
| 同步迴圈 | on_set_state 觸發 object_clipper 再觸發 m_clipping_planes | m_syncing_clipper flag 解決 |
| Gizmo 離場後 clip 位置跳動 | ObjectClipper 離場後 position 可能被重置 | on_set_state(Off) 中明確回寫 m_clipping_planes |
| FDM 無影響 | 所有新邏輯均有 ptSLA + CanvasView3D guard | 逐一驗證 |
| Preview 無影響 | 獨立 canvas 實例 | 不共用任何狀態，自然隔離 |

---

## Post-archive bugfix notes（2026-06-18）

> 本 change 已封存於 2026-04-20。封存後在多物件與 Support 浮空場景發現一批回歸 / 邊界 bug，於 2026-06-18 完成修正。修正未另開 active change（沿用既有「bugfix 直接補進 main spec」慣例），詳細行為規格已落到 `openspec/specs/prepare-z-clip-slider/spec.md` 的 **v3.1 區塊**（SLA-3 與 SLA-4 註記、新增 SLA-10～SLA-12、驗收 T15–T19、Out of Scope 註記）。
>
> 本檔保留封存時的原始設計，**不大改**；以下僅補關鍵技術決策摘要，作為從 archive 反查 v3.1 行為的 bridge。

### B1：scene_max_z 永遠遍歷全場景

`_update_prepare_scene_max_z()` 移除「`if (!m_selection.is_empty()) only_iterate_selection`」分支。理由：Hollow / Drill / Support 退出後 selection 仍非空，舊分支會把全場景 max 算成「所選物件 max」，造成 100 mm 物件被卡在 30 mm 高度顯示。

### B2：Gizmo session 用 selected object bbox + z_shift

Gizmo session 期間 `InstancesHider::set_hide_full_scene(true)` 把所有 model object GLVolumes 設為 `is_active = false`，原 `scene_max_z` 必然 fallback 到 50 mm。`update_sla_prepare_layers_slider()` 在 session active 時改取 `m_gizmo_obj_z_min/max`（由 `enter_gizmo_slider_mode` cache）作 slider range。

### B3：ObjectClipper plane 與 visual world Z 對齊

Gizmo session 中 ObjectClipper 同步從 `set_position_by_ratio(ratio, true, true)` 改成 `set_range_and_pos(Vec3d(0, 0, 1), z_high_eff, ratio)`。原 `set_position_by_ratio` 內部公式假設 model centered，Support 的非零 z_shift 與 bed-aligned mesh 一起把 cap mesh 推到錯誤 world Z，產生「多出薄片」。新呼叫直接以 world Z offset 設定 `m_clp`，與 `set_clipping_plane(1, ClippingPlane(-Z, z_high_mm))` 落在同一個平面。

### B4：Full visible 退出映射到 prepare scene max

`exit_gizmo_slider_mode()` 偵測 `m_gizmo_clip_ratio <= 1e-6`，true 時 `restore_high = m_prepare_scene_max_z`；否則保留 `clamp(z_cur_abs, 0, scene_max)` 的世界 Z 保留行為。對應 main spec SLA-10。

### B5：Gizmo 互切 re-enter（ObjectClipper 不 release）

Hollow / Drill / SLA Support 都把 ObjectClipper 列在 `on_get_requirements()`，互切時 `CommonGizmosDataPool` 不 release，原本只靠 ObjectClipper valid 邊緣的 `just_entered` 偵測會漏掉同 session 內的 gizmo type 切換。`GLGizmosManager` 新增 `m_last_oc_gizmo_type` 追蹤，偵測 `oc_valid && m_oc_was_valid_last_frame && m_current != m_last_oc_gizmo_type` 時呼叫 `enter_gizmo_slider_mode()`；後者區分「第一次進入」與「已 session 內 re-enter」兩條路徑（後者只更新 bbox、不覆寫 saved）。對應 main spec SLA-11。

### B6：data_changed() 必須先於 SLA session 偵測

`GLGizmoSlaSupports::data_changed()` 內 `SelectionInfo::set_use_config_elevation(true)` 才會把 `m_z_shift` 算成含 elevation lift 的值。`update_data()` 把 `data_changed()` 移到 SLA session 偵測之前，確保 `enter_gizmo_slider_mode()` 讀 `get_sla_shift()` 時拿到 lifted 後的 z_shift。Hollow / Drill 不呼叫 `set_use_config_elevation()`，順序對它們等效。對應 main spec SLA-12。

### Out of Scope

`GLGizmoSlaSupports` 中 Points 預覽縮放錯位（物件 scale 後 Points 預覽柱狀位置 / 尺寸對應未放大前物件，Structure 與實際支撐正確）為**既有 / 另案問題**。本批 bugfix 完全未碰 `GLGizmoSlaSupports.cpp` / `GLGizmoSlaBase.cpp` / 任何 raycaster scaling 邏輯。
