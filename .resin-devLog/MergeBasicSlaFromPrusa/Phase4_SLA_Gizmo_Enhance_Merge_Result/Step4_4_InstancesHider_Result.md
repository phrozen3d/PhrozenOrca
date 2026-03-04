# Step 4.4: InstancesHider::set_hide_full_scene() — 完成報告

**完成日期**: 2026-02-27
**狀態**: ✅ 完成（編譯 + 執行驗證通過）

---

## 任務目標

在 PhrozenOrca 的 `InstancesHider` 中新增 `set_hide_full_scene()` 方法，
讓 SLA Gizmo 的 `data_changed()` 能隱藏全部模型物件，改由 Gizmo 自行
透過 `render_volumes()` 渲染（與 PrusaSlicer 行為一致）。

**此步驟為 Steps 4.2 / 4.3 的硬依賴前置條件。**

---

## 修改檔案

| 檔案 | 修改內容 |
|------|---------|
| `PhrozenOrca/src/slic3r/GUI/Gizmos/GLGizmosCommon.hpp` | 新增 public method + private member |
| `PhrozenOrca/src/slic3r/GUI/Gizmos/GLGizmosCommon.cpp` | `on_update()` 加 guard + 新增函數實作 |

---

## 新增 API

### GLGizmosCommon.hpp — `InstancesHider` 類別

```cpp
// public 區塊
void set_hide_full_scene(bool hide); // Step 4.4: hides all model objects (gizmo renders its own volumes)

// private 區塊
bool m_hide_full_scene{ false }; // Step 4.4: when true, on_update() skips restoring instance visibility
```

### GLGizmosCommon.cpp — 新函數

```cpp
void InstancesHider::set_hide_full_scene(bool hide)
{
    // Step 4.4: Called from SLA gizmo data_changed() to hide all model objects.
    // on_update() will skip toggle_model_objects_visibility(true, ...) when this is set.
    if (m_hide_full_scene != hide) {
        m_hide_full_scene = hide;
        on_update();
    }
}
```

### GLGizmosCommon.cpp — `on_update()` 修改

```cpp
// 修改前（PrusaSlicer 原始）：
canvas->toggle_model_objects_visibility(false);
canvas->toggle_model_objects_visibility(true, mo, active_inst);
canvas->toggle_sla_auxiliaries_visibility(false, mo, active_inst);

// 修改後（PhrozenOrca）：
canvas->toggle_model_objects_visibility(false);
// Step 4.4: when hide_full_scene is set, keep all objects hidden so the
// SLA gizmo can render its own volumes via render_volumes() instead.
if (!m_hide_full_scene) {
    canvas->toggle_model_objects_visibility(true, mo, active_inst);
    canvas->toggle_sla_auxiliaries_visibility(false, mo, active_inst);
}
```

---

## PhrozenOrca 特殊處理

PhrozenOrca 的 `on_update()` 含有 **AssembleView** 分支（PrusaSlicer 無此邏輯）：

```cpp
// PhrozenOrca-only: AssembleView canvas type check
auto canvas_type = m_parent.get_canvas_type();
if (canvas_type == GLCanvas3D::EType::Assemble) {
    // ... z_min logic for assemble view ...
    return;
}
```

此分支在 `toggle_model_objects_visibility(false)` 之前執行（early return），
故 `m_hide_full_scene` guard 置於其後不影響 AssembleView 邏輯，完整保留。

---

## FDM 安全確認

| 修改項目 | FDM 影響 |
|---------|:-------:|
| `m_hide_full_scene` 預設 `false` | ✅ FDM gizmo 不呼叫 `set_hide_full_scene()`，行為不變 |
| `if (!m_hide_full_scene)` guard | ✅ `false` 時直接跳過 guard，邏輯等同原始 |
| AssembleView 分支保留 | ✅ 完整保留 PhrozenOrca 特有邏輯 |

---

## 驗證結果

- ✅ 編譯通過（無 error）
- ✅ 可正常執行
- ✅ FDM 功能未受影響

---

## 後續步驟

| 步驟 | 內容 | 狀態 |
|------|------|:----:|
| Step 4.3 | GLGizmoHollow 重構（繼承 GLGizmoSlaBase，呼叫 set_hide_full_scene） | ⏳ 待執行 |
| Step 4.2 | GLGizmoSlaSupports 完整同步 | ⏳ 待執行 |
| Step 4.5 | AnycubicSLA 格式（獨立） | ⏳ 待執行 |
