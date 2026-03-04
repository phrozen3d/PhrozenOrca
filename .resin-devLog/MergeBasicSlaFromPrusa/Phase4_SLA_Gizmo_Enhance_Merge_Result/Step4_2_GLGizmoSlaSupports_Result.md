# Step 4.2: GLGizmoSlaSupports 重構（繼承 GLGizmoSlaBase）— 完成報告

**完成日期**: 2026-02-28
**狀態**: ✅ 完成（編譯 + 執行驗證通過）

---

## 任務目標

將 PhrozenOrca 的 `GLGizmoSlaSupports`（SLA 支撐點 Gizmo）從繼承 `GLGizmoBase` 改為繼承
`GLGizmoSlaBase`（Step 4.1 新增的中間基礎類別），並同步 PrusaSlicer 的實作。

主要變更：
- 統一 SLA Gizmo 的渲染管道（`render_volumes()` / `update_volumes()`）
- 使用 `data_changed()` 取代 `set_sla_support_data()`
- 整合 `set_hide_full_scene(true)`（Step 4.4 依賴）
- 整合 Volume Raycasters（`register_volume_raycasters_for_picking()`）
- 移除 `m_cylinder` 裸 GLModel，改由 GLGizmoSlaBase 統一管理

---

## 修改檔案

| 檔案 | 修改內容 |
|------|---------|
| `PhrozenOrca/src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.hpp` | 完整重寫 |
| `PhrozenOrca/src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp` | 大量修改（重構關鍵函數） |
| `PhrozenOrca/src/slic3r/GUI/Gizmos/GLGizmosManager.cpp` | 修復外部呼叫（1 行） |

---

## .hpp 主要結構變更

```cpp
// Before:
#include "GLGizmoBase.hpp"
class GLGizmoSlaSupports : public GLGizmoBase

// After (Step 4.2):
#include "GLGizmoSlaBase.hpp"
class GLGizmoSlaSupports : public GLGizmoSlaBase
```

| 變更項目 | 舊版 | 新版 |
|---------|------|------|
| 繼承 | `GLGizmoBase` | `GLGizmoSlaBase` |
| 建構函式 4th param | （無） | `slaposDrillHoles` |
| data API | `set_sla_support_data()` | `data_changed(bool)` override |
| `RenderPointScale` | `const float`（成員變數） | `static constexpr float`（類別常數） |
| `GLModel m_cylinder` | 有 | 移除（SlaBase 統一管理） |
| `m_show_support_structure` | 無 | 新增 `bool m_show_support_structure = false` |
| `is_in_editing_mode()` | 無 | `override`（SlaBase 純虛函數實作） |
| `is_selection_rectangle_dragging()` | 無 | `override`（SlaBase 純虛函數實作） |
| `render_points()` | `void render_points(Selection&, bool picking)` | `void render_points(Selection&)`（移除 picking 參數） |
| `unproject_on_mesh()` | 自行實作 | 移除（繼承自 GLGizmoSlaBase） |
| `is_point_in_hole()` | 自行實作 | 移除 |
| `m_desc` 型別 | `map<string, wxString>` | 保留（PhrozenOrca ImGui 相容） |
| `on_get_requirements()` | override | 移除（繼承自 GLGizmoSlaBase） |

---

## .cpp 主要函數變更

### 建構函式
```cpp
// 新版：傳入 slaposDrillHoles 作為最低 SLA 步驟
// 並初始化 show_sla_supports(false)
GLGizmoSlaSupports::GLGizmoSlaSupports(...) : GLGizmoSlaBase(parent, icon, sprite_id, slaposDrillHoles)
{
    show_sla_supports(false);
}
```

### data_changed()（取代 set_sla_support_data）
```cpp
void GLGizmoSlaSupports::data_changed(bool is_serializing)
{
    // reload_cache() if model object changed
    m_c->instances_hider()->set_hide_full_scene(true);   // Step 4.4 依賴
    // reslice_until_step() if required step not done
    update_volumes();                                     // from GLGizmoSlaBase
    // get_data_from_backend() if Generating
    // register/update point raycasters
    m_c->instances_hider()->set_hide_full_scene(true);   // 同 PrusaSlicer 呼叫兩次
}
```

### on_mouse()
```cpp
void GLGizmoSlaSupports::on_mouse(...)
{
    if (!is_input_enabled()) return false;  // Step 4.2 新增：SlaBase 輸入鎖定
    // ...existing logic...
}
```

### on_render()
```cpp
void GLGizmoSlaSupports::on_render()
{
    if (!selected_print_object_exists(...)) { /* close gizmo */ }
    render_volumes();                          // SlaBase — 渲染半透明模型
    render_points(selection);                  // 移除 false 參數
    m_selection_rectangle.render(m_parent);
    m_c->object_clipper()->render_cut();
    if (are_sla_supports_shown())              // 條件式：編輯模式中隱藏支撐結構
        m_c->supports_clipper()->render_cut();
}
```

### render_points()
```cpp
void GLGizmoSlaSupports::render_points(const Selection& selection)
// 移除：bool picking 參數
// 移除：drain hole 渲染（GLGizmoHollow 負責）
// 移除：hollowed_mesh() 使用
// 新增：raycaster active/clipped 管理
const bool clipped = is_mesh_point_clipped(support_point.pos.cast<double>());
if (i < m_point_raycasters.size()) {
    m_point_raycasters[i].first->set_active(!clipped);
    m_point_raycasters[i].second->set_active(!clipped);
}
```

### on_set_state()
```cpp
// 在 Gizmo 真正關閉時還原場景可見性
m_c->instances_hider()->set_hide_full_scene(false);
```

### on_register/unregister_raycasters_for_picking()
```cpp
void GLGizmoSlaSupports::on_register_raycasters_for_picking()
{
    register_point_raycasters_for_picking();
    register_volume_raycasters_for_picking();  // Step 4.2 新增
}
void GLGizmoSlaSupports::on_unregister_raycasters_for_picking()
{
    unregister_point_raycasters_for_picking();
    unregister_volume_raycasters_for_picking();  // Step 4.2 新增
}
```

### 編輯模式切換
```cpp
void GLGizmoSlaSupports::switch_to_editing_mode()
{
    show_sla_supports(false);  // 進入編輯模式：隱藏支撐結構
    // ...existing logic...
}
void GLGizmoSlaSupports::disable_editing_mode()
{
    show_sla_supports(m_show_support_structure);  // 退出編輯模式：依設定還原
    // ...existing logic...
}
```

### auto_generate() / editing_mode_apply_changes()
```cpp
// Before:
reslice_SLA_supports();

// After:
reslice_until_step(slaposSupportPoints);  // 使用繼承的方法
```

---

## PhrozenOrca 相容性修正（編譯期錯誤修正）

| 問題 | 原因 | 修正 |
|------|------|------|
| `reslice_SLA_supports` 不是成員（GLGizmosManager.cpp:1022） | 方法已移除，外部呼叫未更新 | → `reslice_until_step(slaposSupportPoints, true)` |
| `ColorRGBA::REDISH()` 不存在 | PhrozenOrca 只有 `ColorRGB::REDISH()` | → `ColorRGBA{1.f, 0.3f, 0.3f, 1.f}` |
| `sla::SupportPointType` 不存在 | PhrozenOrca 只有 `bool is_new_island` | 保留原有顏色邏輯 |
| `model_instance()` 不存在 | PhrozenOrca 無此 selection_info API | 保留原有 `vol->get_instance_transformation()` 路徑 |
| `last_completed_step()` 不存在 | PrusaSlicer 特有 API | 改用 `is_step_done()` 語意等價替代 |

---

## 移除項目

| 移除項目 | 原因 |
|---------|------|
| `picking_color_component()` | PrusaSlicer 無此函數，PickingModel 機制取代 |
| `set_sla_support_data()` | 由 `data_changed()` 取代 |
| `reslice_SLA_supports()` | 由繼承的 `reslice_until_step()` 取代 |
| `unproject_on_mesh()` | 移至 `GLGizmoSlaBase` 基礎類別 |
| `is_point_in_hole()` | PrusaSlicer 無此函數 |
| `on_get_requirements()` | 繼承自 `GLGizmoSlaBase` |
| `GLModel m_cylinder` | SlaBase 統一管理 volume 渲染 |
| drain hole 渲染邏輯 | 責任移至 `GLGizmoHollow`（Step 4.3） |

---

## FDM 安全確認

| 修改項目 | FDM 影響 |
|---------|:-------:|
| `GLGizmoSlaSupports` 整個重構 | ✅ SLA Support Points Gizmo 僅在 SLA 模式啟用，FDM 不受影響 |
| `set_hide_full_scene()` 呼叫 | ✅ 僅在 SLA Gizmo On 時呼叫，Off 時立即還原 |
| `GLGizmosManager.cpp` 修改 | ✅ 僅影響 `update_after_undo_redo()` 中的 SLA 分支 |

---

## 驗證結果

- ✅ 編譯通過（無 error）
- ✅ 可正常執行
- ✅ FDM 功能未受影響
- 📋 功能驗證：見 `Step4_2_GLGizmoSlaSupports_Verify.md`

---

## 後續步驟

| 步驟 | 內容 | 狀態 |
|------|------|:----:|
| Step 4.5 | AnycubicSLA 格式（獨立） | ⏳ 待執行 |
