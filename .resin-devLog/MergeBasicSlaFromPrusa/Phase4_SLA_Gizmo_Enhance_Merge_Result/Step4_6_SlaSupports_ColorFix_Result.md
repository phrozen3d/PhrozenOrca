# Step 4.6: SLA Support Gizmo — 模型顏色修正與 UI 完善

## 概述

修正 `GLGizmoSlaBase::update_volumes()` 中模型顏色邏輯，使進入 SLA Support Points Gizmo
後模型維持與 Viewport 一致的原始顏色（而非白色或灰色）。同時補充 no-step constructor、
icon 支援及支撐點統計顯示。

---

## 問題根因分析

### PhrozenOrca `set_render_color()` 的 `selected` 分支被 BBS 注解掉

**位置**：`src/slic3r/GUI/3DScene.cpp:246`

```cpp
// PrusaSlicer 原始行為：selected = true → 渲染為藍色（SELECTED_COLOR）
/* BBS
if (hover == HS_Select)
    set_render_color(HOVER_SELECT_COLOR);
else if (hover == HS_Deselect)
    set_render_color(HOVER_DESELECT_COLOR);
else if (selected)
    set_render_color(outside ? SELECTED_OUTSIDE_COLOR : SELECTED_COLOR);
else if (disabled)
*/
// PhrozenOrca 實際執行路徑：
if (disabled)
    set_render_color(DISABLED_COLOR);    // → 深灰色
else
    adjust_color_for_rendering(color);   // → 直接使用 volume->color
```

**結論**：
- `volume->selected = true` 在 PhrozenOrca 中 **完全沒有視覺效果**
- `volume->color` 才是實際驅動渲染顏色的欄位
- `GLVolume` 預設 `color = {1,1,1,1}`（白色）→ Gizmo 進入後模型顯示為白色

---

## 修改內容

### 1. `GLGizmoSlaBase.cpp` — `update_volumes()` 顏色邏輯

#### Backend path（SLAPrintObject 可用時）

新增從 Selection 取得原始模型顏色的邏輯：

```cpp
// 在 add_volume lambda 之前
ColorRGBA original_model_color = ColorRGBA::WHITE();
{
    const Selection& sel = m_parent.get_selection();
    for (unsigned int idx : sel.get_volume_idxs()) {
        const GLVolume* sv = sel.get_volume(idx);
        if (!sv->is_modifier && sv->composite_id.volume_id >= 0) {
            original_model_color = sv->color;
            break;
        }
    }
}
```

更新 `add_volume` lambda：

```cpp
if (m_input_enabled) {
    volume->selected = true;              // kept for API compatibility
    volume->color = original_model_color; // actual color driver
} else {
    volume->set_color(DISABLED_COLOR);
}
```

#### Fallback path（po == nullptr，使用 Selection 直接渲染）

```cpp
new_volume->selected = true;  // kept for API compatibility
new_volume->color = v->color; // PhrozenOrca: copy original color (selected flag has no visual effect)
```

### 2. `GLGizmoSlaBase.hpp / .cpp` — No-step Constructor

新增不帶 `SLAPrintObjectStep` 參數的建構式（sentinel `m_min_sla_print_object_step = -1`）：

```cpp
// .hpp
GLGizmoSlaBase(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

// .cpp
GLGizmoSlaBase::GLGizmoSlaBase(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
: GLGizmoBase(parent, icon_filename, sprite_id)
// m_min_sla_print_object_step stays at default -1
{}
```

**用途**：`GLGizmoSlaSupports` 使用此建構式，因為 PhrozenOrca 沒有 PrusaSlicer 的
`slaposBase`/`slaposAssembly`（永遠完成的初始化步驟），故不需要最小步驟檢查。

`update_volumes()` 對應邏輯：
```cpp
if (m_min_sla_print_object_step < 0) {
    m_input_enabled = true; // always enable input when mesh is available
} else {
    m_input_enabled = po->is_step_done((SLAPrintObjectStep)m_min_sla_print_object_step)
        || po->model_object()->sla_points_status == sla::PointsStatus::UserModified;
}
```

### 3. `GLGizmoSlaSupports` — Icon 支援與 UI 完善

#### Icon 初始化（Step 4.5+）

```cpp
namespace {
enum class IconType : unsigned {
    show_support_points_selected,
    show_support_points_unselected,
    show_support_points_hovered,
    show_support_structure_selected,
    show_support_structure_unselected,
    show_support_structure_hovered,
    _count
};

IconManager::Icons init_support_icons(IconManager &mng, ImVec2 size = ImVec2{50, 50});
bool draw_support_view_mode(bool &show_support_structure, const IconManager::Icons &icons);
} // anonymous namespace
```

Icons 使用 PhrozenOrca 路徑 `/images/`（非 PrusaSlicer 的 `/icons/`）。

#### 支撐點統計顯示

```cpp
int count_island = 0;
for (const sla::SupportPoint &sp : m_normal_cache)
    if (sp.is_new_island) ++count_island;
// 使用 is_new_island 而非 SupportPointType（PhrozenOrca 的 SupportPoint 無此欄位）
```

#### `data_changed()` 修正

只在有 minimum step 需求（`required_step >= 0`）且步驟未完成時才觸發 reslice：

```cpp
const int required_step = get_min_sla_print_object_step();
const SLAPrintObject* po = m_c->selection_info()->print_object();
if (required_step >= 0 && po != nullptr && !po->is_step_done((SLAPrintObjectStep)required_step))
    reslice_until_step((SLAPrintObjectStep)required_step, false);
```

---

## 修改的檔案

| 檔案 | 修改內容 |
|------|---------|
| `src/slic3r/GUI/Gizmos/GLGizmoSlaBase.hpp` | 新增 no-step constructor 宣告 |
| `src/slic3r/GUI/Gizmos/GLGizmoSlaBase.cpp` | No-step constructor 實作 + update_volumes() 顏色修正 |
| `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.hpp` | 新增 m_icon_manager、m_icons、m_show_support_structure 成員 |
| `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp` | Icon 支援、支撐點統計、data_changed 修正 |
| `resources/images/support_structure.svg` | 新增 icon 資源 |
| `resources/images/support_structure_invisible.svg` | 新增 icon 資源 |

---

## 驗證結果

| 項目 | 結果 |
|------|:----:|
| 編譯成功 | ✅ |
| 進入 Support Points Gizmo — 模型顏色與 Viewport 一致 | ✅ |
| UI 流程（auto-generate / manual edit / apply / discard）正常 | ✅ |
| 支撐點 icon 切換（show points / show structure）正常 | ✅ |
| 停用狀態（input disabled）顯示深灰色 | ✅ |
| FDM 路徑不受影響 | ✅ |

---

## 技術備注

### 為何 `selected = true` 無效？

BambuStudio fork 將 `set_render_color()` 中的 `selected` 分支整個注解掉（標記 `/* BBS */`），
僅保留 `disabled` 分支與 `adjust_color_for_rendering(color)` 作為 fallback。
因此所有顏色控制必須直接設定 `volume->color`，`selected` 欄位僅作 API 相容性保留。

### Fallback path 的 `m_input_enabled = true`

當 `po == nullptr`（SLAPrint 未初始化）時，`update_volumes()` 使用 Selection 複製 GLVolume。
此時設 `m_input_enabled = true` 並複製 `v->color`，確保模型以正常顏色顯示，
且支撐點手動編輯仍可使用。

---

## 後續步驟

- Step 5.x：GLGizmoHollow 相關功能驗證（如有需要）
- Step 2.5：SLA Layer Slider 實作（計畫中）
