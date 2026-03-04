# Step 4.3: GLGizmoHollow 重構（繼承 GLGizmoSlaBase）— 完成報告

**完成日期**: 2026-02-28
**狀態**: ✅ 完成（編譯 + 執行驗證通過）

---

## 任務目標

將 PhrozenOrca 的 `GLGizmoHollow`（排水孔 Gizmo）從繼承 `GLGizmoBase` 改為繼承
`GLGizmoSlaBase`（Step 4.1 新增的中間基礎類別），並同步 PrusaSlicer 的實作。

主要變更：
- 統一 SLA Gizmo 的渲染管道（`render_volumes()` / `update_volumes()`）
- 使用 `data_changed()` 取代 `set_sla_support_data()`
- 升級 `GLModel` → `PickingModel`（加入 raycaster 支援）
- 整合 `set_hide_full_scene(true)`（Step 4.4 依賴）

---

## 修改檔案

| 檔案 | 修改內容 |
|------|---------|
| `PhrozenOrca/src/slic3r/GUI/Gizmos/GLGizmoHollow.hpp` | 完整重寫 |
| `PhrozenOrca/src/slic3r/GUI/Gizmos/GLGizmoHollow.cpp` | 完整重寫 |

---

## .hpp 主要結構變更

```cpp
// Before:
#include "GLGizmoBase.hpp"
class GLGizmoHollow : public GLGizmoBase

// After (Step 4.3):
#include "GLGizmoSlaBase.hpp"
class GLGizmoHollow : public GLGizmoSlaBase
```

| 變更項目 | 舊版 | 新版 |
|---------|------|------|
| 繼承 | `GLGizmoBase` | `GLGizmoSlaBase` |
| 建構函式 4th param | （無） | `slaposSliceSupports` |
| data API | `set_sla_support_data()` | `data_changed(bool)` override |
| 渲染 pick | `GLModel m_cylinder` | `PickingModel m_cylinder` |
| Raycasters | 無 | `m_hole_raycasters` vector |
| `m_desc` 型別 | `map<string, string>` | `map<string, wxString>`（PhrozenOrca ImGui 相容） |
| `unproject_on_mesh()` | 自行實作 | 繼承自 GLGizmoSlaBase |
| `on_get_requirements()` | override（含 HollowedMesh） | 不 override（繼承 SlaBase） |
| `hollow_mesh()` | 有 | 移除 |

---

## .cpp 主要函數變更

### 建構函式
```cpp
// 新版：傳入 slaposSliceSupports 作為最低 SLA 步驟
// （PrusaSlicer 使用 slaposAssembly，PhrozenOrca 無此 enum 值）
GLGizmoHollow::GLGizmoHollow(...) : GLGizmoSlaBase(parent, icon, sprite_id, slaposSliceSupports)
```

### data_changed()（取代 set_sla_support_data）
```cpp
void GLGizmoHollow::data_changed(bool is_serializing)
{
    // reload_cache() if model object changed
    // reslice_until_step(slaposSliceSupports) if mesh empty
    update_volumes();                             // from GLGizmoSlaBase
    register/update hole raycasters
    m_c->instances_hider()->set_hide_full_scene(true); // Step 4.4 dependency
}
```

### on_render()
```cpp
void GLGizmoHollow::on_render()
{
    // selected_print_object_exists check
    render_volumes();          // SlaBase — renders drilled mesh
    render_points(selection);  // PickingModel cylinder raycasters
    m_selection_rectangle.render(m_parent);
    m_c->object_clipper()->render_cut();
    if (are_sla_supports_shown())
        m_c->supports_clipper()->render_cut();
}
```

### on_set_state()
```cpp
if (m_state == Off && m_old_state != Off) {
    m_c->instances_hider()->set_hide_full_scene(false); // restore scene visibility on close
}
```

### init_cylinder_model()（lazy init）
```cpp
void GLGizmoHollow::init_cylinder_model()
{
    if (!m_cylinder.model.is_initialized()) {
        indexed_triangle_set its = its_make_cylinder(1.0, 1.0);
        m_cylinder.model.init_from(its);
        m_cylinder.mesh_raycaster = std::make_unique<MeshRaycaster>(
            std::make_shared<const TriangleMesh>(std::move(its)));
    }
}
```

---

## PhrozenOrca 相容性修正（編譯期錯誤修正）

| 問題 | 原因 | 修正 |
|------|------|------|
| `slaposAssembly` 未宣告（lines 40, 82） | PhrozenOrca 無此 enum 值 | → `slaposSliceSupports`（最後步驟） |
| `Vec2i` 未宣告（line 417） | Point.hpp 中已注解掉 | → `Vec2i32` |
| `TakeSnapshot(_L("..."))` 型別不符（5處） | PhrozenOrca TakeSnapshot 只接受 `std::string` | 移除 `_L()` 包裝 |
| `DynamicPrintConfig::option_def()` 不存在（3處） | PhrozenOrca 無此方法 | → `def()->get(key)` |

---

## 移除項目

| 移除項目 | 原因 |
|---------|------|
| `hollow_mesh()` | PrusaSlicer 無此方法；改用 `reslice_until_step()` |
| `unproject_on_mesh()` | 移至 `GLGizmoSlaBase` 基礎類別 |
| `on_get_requirements()` | 繼承自 `GLGizmoSlaBase`（含 HollowedMesh 已移除） |
| `set_sla_support_data()` | 由 `data_changed()` 取代 |
| `picking_color_component()` | PickingModel 機制取代 |

---

## FDM 安全確認

| 修改項目 | FDM 影響 |
|---------|:-------:|
| `GLGizmoHollow` 整個重構 | ✅ Hollow Gizmo 僅在 SLA 模式啟用，FDM 不受影響 |
| `set_hide_full_scene()` 呼叫 | ✅ 僅在 SLA Gizmo On 時呼叫，Off 時立即還原 |
| `render_volumes()` 新增 | ✅ SLA-only 渲染路徑 |

---

## 驗證結果

- ✅ 編譯通過（無 error）
- ✅ 可正常執行
- ✅ FDM 功能未受影響

---

## 後續步驟

| 步驟 | 內容 | 狀態 |
|------|------|:----:|
| Step 4.2 | GLGizmoSlaSupports 完整同步 | ⏳ 待執行 |
| Step 4.5 | AnycubicSLA 格式（獨立） | ⏳ 待執行 |
