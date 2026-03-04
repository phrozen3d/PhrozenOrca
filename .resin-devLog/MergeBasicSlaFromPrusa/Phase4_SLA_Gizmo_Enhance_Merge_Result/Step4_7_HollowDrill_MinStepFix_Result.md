# Step 4.7: Hollow and Drill Gizmo — Minimum Step 修正

## 概述

修正 `GLGizmoHollow` constructor 使用錯誤最小步驟的問題，使進入 Hollow and Drill Gizmo
後模型立即以正常顏色顯示、UI 立即可互動，行為與 PrusaSlicer 一致。

---

## 問題根因分析

### 錯誤的最小步驟 (`slaposSliceSupports`)

**位置**：`GLGizmoHollow.cpp`（Step 4.3 設定）

```cpp
// 舊版（錯誤）
GLGizmoHollow::GLGizmoHollow(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoSlaBase(parent, icon_filename, sprite_id, slaposSliceSupports)
{}
```

**問題**：`slaposSliceSupports` 是 PhrozenOrca SLA Pipeline 的**最後一個步驟**：

```
slaposHollowing  (step 0) — 掏空模型
slaposDrillHoles (step 1) — 鑽孔
...
slaposSliceSupports (last) — 切片支撐 ← 錯誤地被用作最小步驟
```

**影響**：`GLGizmoSlaBase::update_volumes()` 中：
```cpp
m_input_enabled = po->is_step_done(slaposSliceSupports) || ...;
```
→ 使用者進入 Gizmo 時 `m_input_enabled = false`
→ 模型顯示為深灰色（`DISABLED_COLOR`），所有 UI 按鈕被 disabled
→ 必須等到完整 SLA Pipeline 執行完才能互動 — 行為完全錯誤

### PrusaSlicer 的正確做法

PrusaSlicer 使用 `slaposAssembly` 作為 GLGizmoHollow 的最小步驟：
- `slaposAssembly` 是 PrusaSlicer Pipeline 的**早期初始化步驟**
- 物件一載入就完成，`m_input_enabled` 幾乎永遠為 `true`
- 使用者進入 Gizmo 立即看到正常顏色的模型、立即可互動

PhrozenOrca 沒有等效的「永遠完成的早期步驟」，需要使用 no-step sentinel (-1)。

---

## 修改內容

### `GLGizmoHollow.cpp` — Constructor 改為 no-step

```cpp
// Step 4.7: no-step constructor (m_min_sla_print_object_step = -1)
// 與 GLGizmoSlaSupports 相同的修正方式
GLGizmoHollow::GLGizmoHollow(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoSlaBase(parent, icon_filename, sprite_id)  // no minimum step
{}
```

**效果**：`update_volumes()` 中 `m_min_sla_print_object_step < 0` 分支：
```cpp
if (m_min_sla_print_object_step < 0) {
    m_input_enabled = true; // always enable input when mesh is available
}
```
→ 進入 Gizmo 立即啟用，模型顯示正常顏色

### `GLGizmoHollow.cpp` — `data_changed()` 改為 guarded reslice

```cpp
// Step 4.7: 使用 required_step >= 0 guard
// required_step = -1 (no-step) 時，不觸發自動 reslice
// 使用者透過 "Preview hollowed model" 按鈕明確觸發掏空預覽
const int required_step = get_min_sla_print_object_step();
const SLAPrintObject* po = m_c->selection_info()->print_object();
if (required_step >= 0 && po != nullptr && po->get_mesh_to_print().empty())
    reslice_until_step((SLAPrintObjectStep)required_step);
```

**對比 Step 4.3 舊版**（錯誤）：
```cpp
// 舊版：po 存在但 mesh 為空時，強制觸發 slaposSliceSupports（完整 pipeline！）
if (po->get_mesh_to_print().empty())
    reslice_until_step(slaposSliceSupports);
```

---

## 修改的檔案

| 檔案 | 修改內容 |
|------|---------|
| `src/slic3r/GUI/Gizmos/GLGizmoHollow.cpp` | Constructor 改為 no-step + data_changed() guard |

---

## 與 GLGizmoSlaSupports 的一致性

| 項目 | GLGizmoSlaSupports (Step 4.6) | GLGizmoHollow (Step 4.7) |
|------|-------------------------------|-----------------------------|
| Constructor | no-step (-1) | no-step (-1) ✅ |
| PrusaSlicer 最小步驟 | slaposBase / slaposAssembly | slaposAssembly |
| m_input_enabled 預設 | true（mesh 存在時）| true（mesh 存在時）✅ |
| data_changed() reslice | required_step >= 0 guard | required_step >= 0 guard ✅ |
| 模型顏色 | 原始顏色（從 Selection 複製）| 原始顏色（同一 update_volumes 邏輯）✅ |

---

## UI 流程確認（與 PrusaSlicer 一致）

PhrozenOrca 的 `on_render_input_window()` 已在 Step 4.3 同步，結構完全一致：

1. **Preview hollowed and drilled model** → `reslice_until_step(slaposDrillHoles)` ✅
2. **Hollow this object** checkbox → 更新 `hollowing_enable` config ✅
3. Offset / Quality / Closing distance 滑桿（依 mode 顯示）✅
4. Hole diameter / Hole depth 滑桿 ✅
5. Remove selected / Remove all 按鈕 ✅
6. Clipping of view 滑桿 ✅
7. **Show supports** checkbox ✅

---

## 驗證項目

| 項目 | 預期結果 |
|------|----------|
| 進入 Hollow and Drill Gizmo | 模型立即顯示正常顏色（非深灰色）|
| Gizmo 開啟時所有 UI | 全部可互動（非 greyed out）|
| 左鍵點擊模型表面 | 放置 drain hole（cylinder marker 顯示）|
| 右鍵點擊 hole | 刪除該 hole |
| 拖曳 hole | 移動至新位置 |
| "Preview hollowed model" 按鈕 | 觸發 slaposDrillHoles reslice |
| "Hollow this object" checkbox | 開關 hollowing，更新 config |
| Offset/Quality/Closing distance 滑桿 | 更新 config，正確 undo/redo snapshot |
| "Show supports" checkbox | 顯示/隱藏支撐結構 |
| FDM 路徑 | 不受影響 |

---

## 技術備注

### 為何不觸發自動 reslice？

PrusaSlicer 在 `data_changed()` 觸發 `slaposAssembly`（trivial 步驟）是為了確保基礎 mesh 就緒。
在 PhrozenOrca 中：
- `slaposHollowing` 是第一個步驟，但涉及實際計算（非 trivial）
- 使用者可能只是想配置參數，不需要立即計算
- `update_volumes()` 的 fallback path（Selection 複製）已能顯示原始 mesh
- 使用者透過 "Preview" 按鈕明確觸發計算

### 修正的連鎖效果

- `m_input_enabled = true` → `volume->color = original_model_color`（Step 4.6 的顏色邏輯）
- 模型自動使用與 Viewport 一致的原始顏色
- 不需要額外的顏色處理

---

## 後續步驟

- Step 2.5：SLA Layer Slider 實作（計畫中）
