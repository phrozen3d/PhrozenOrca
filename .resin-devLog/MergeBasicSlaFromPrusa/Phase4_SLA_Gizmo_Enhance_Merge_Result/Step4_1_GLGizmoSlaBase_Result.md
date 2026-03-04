# Step 4.1: GLGizmoSlaBase 基類移植 — 完成報告

**完成日期**: 2026-02-27
**狀態**: ✅ 完成（編譯 + 執行驗證通過）

---

## 任務目標

在 PhrozenOrca 中新增 `GLGizmoSlaBase` 中間基類，建立與 PrusaSlicer 一致的 SLA Gizmo 繼承鏈：

```
PrusaSlicer 目標架構:
GLGizmoBase
    └── GLGizmoSlaBase  ← 本步驟新增
            ├── GLGizmoSlaSupports
            └── GLGizmoHollow
```

---

## 新增 / 修改檔案

### 新增
- `PhrozenOrca/src/slic3r/GUI/Gizmos/GLGizmoSlaBase.hpp`
- `PhrozenOrca/src/slic3r/GUI/Gizmos/GLGizmoSlaBase.cpp`

### 修改
- `PhrozenOrca/src/slic3r/CMakeLists.txt` — 加入兩個新檔案

---

## PhrozenOrca API 差異修正（4 處）

| # | PrusaSlicer API | PhrozenOrca API | 修正方式 |
|---|----------------|----------------|---------|
| 1 | `get_mesh_to_print()` → `shared_ptr<const indexed_triangle_set>` | 回傳 `const TriangleMesh&` | 直接 copy-construct |
| 2 | `po->last_completed_step()` | 方法不存在 | 改用 `po->is_step_done((SLAPrintObjectStep)m_min_sla_print_object_step)` |
| 3 | `render(type, cull, view, proj)` — 4 參數 | 需第 5 參數 `cnv_size` | 加入 `m_parent.get_canvas_size()` |
| 4 | `selection_info()->model_instance()` | 方法不存在 | 改用 `model_object()` + `get_active_instance()` |

### 語義等價性說明（修正 2）
`last_completed_step() >= min_step` ⟺ `is_step_done(min_step)`
因為 SLA 列印步驟具順序性：若步驟 N 完成，則步驟 N-1 必已完成。

---

## MultipleBeds 移除

PrusaSlicer 的 `update_volumes()` 對三個 mesh（backend/supports/pad）各呼叫一次：
```cpp
mesh.translate(s_multiple_beds.get_bed_translation(s_multiple_beds.get_active_bed()).cast<float>());
```

PhrozenOrca 使用 PartPlateList 架構，mesh 座標不需 bed offset，全部移除（3 行），改為說明性 comment。

---

## FDM 安全確認

| 修改項目 | FDM 影響 |
|---------|:-------:|
| 新增 GLGizmoSlaBase.hpp / .cpp | ✅ 無（新增 SLA-only 檔案） |
| CMakeLists.txt 加入新檔案 | ✅ 無（純新增，不影響現有編譯單元） |
| GLGizmoSlaSupports / GLGizmoHollow 繼承鏈 | ✅ 尚未修改（留待 Step 4.2 / 4.3）|

---

## 驗證結果

- ✅ 編譯通過（無 error）
- ✅ 可正常執行
- ✅ FDM 功能未受影響

---

## Phase 4 執行順序說明

### 為何不按數字順序執行

Steps 4.2 / 4.3 的 `data_changed()` 會呼叫 `m_c->instances_hider()->set_hide_full_scene()`，
此方法由 **Step 4.4** 新增。若按數字順序（4.2 → 4.3 → 4.4）執行，4.2/4.3 編譯時會找不到該方法。

### 決定執行順序

```
4.4 → 4.3 → 4.2   （硬依賴鏈）
4.5                 （獨立，可任意時間執行）
```

| Step | 內容 | 依賴 |
|------|------|------|
| **4.4** | InstancesHider::set_hide_full_scene() | 無（基礎 API） |
| **4.3** | GLGizmoHollow 重構 | 需 4.4 的 set_hide_full_scene |
| **4.2** | GLGizmoSlaSupports 完整同步 | 需 4.4 的 set_hide_full_scene |
| **4.5** | AnycubicSLA 格式 | 獨立，無依賴 |

### Result 閱讀順序

閱讀 Phase 4 result 文件時，請依此順序：
`Step4_4` → `Step4_3` → `Step4_2` → `Step4_5`
