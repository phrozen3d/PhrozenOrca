# Step 2.1: 啟用 SlaSupports / Hollow Gizmo — 結果報告

**執行日期**: 2026-02-14
**狀態**: 程式碼修改完成，待編譯驗證

---

## 修改總覽

共修改 **4 個檔案**，新增 **2 個資源檔案**。

---

## 修改檔案清單

### 1. GLGizmosManager.hpp
**路徑**: `src/slic3r/GUI/Gizmos/GLGizmosManager.hpp`

| 修改位置 | 內容 |
|---------|------|
| line 93, 96 | 取消 `SlaSupports` 和 `Hollow` 在 EType enum 的註解 |

```cpp
// 修改前:
//SlaSupports,
//Hollow,

// 修改後:
SlaSupports,
Hollow,
```

> FaceRecognition 保持註解 (PrusaSlicer 也已移除)

---

### 2. GLGizmosManager.cpp
**路徑**: `src/slic3r/GUI/Gizmos/GLGizmosManager.cpp`

| 修改位置 | 功能 | 類型 |
|---------|------|------|
| line 16 | `#include GLGizmoSlaSupports.hpp` | 取消註解 |
| line 22 | `#include GLGizmoHollow.hpp` | 取消註解 |
| line ~178 | `switch_gizmos_icon_filename()` 新增 SlaSupports/Hollow case | 新增 |
| line ~221 | `init()` emplace_back SlaSupports + Hollow | 取消註解 + 修改 |
| line ~465 | `gizmo_event()` SlaSupports/Hollow 路由 | 取消註解 |
| line ~533 | `wants_reslice_supports_on_undo()` SlaSupports 檢查 | 取消註解 |
| line ~594 | `on_mouse_wheel()` 加入 SlaSupports/Hollow 條件 | 取消註解 |
| line ~748 | `on_char()` Ctrl+A SelectAll | 取消註解 |
| line ~765 | `on_char()` ESC DiscardChanges | 取消註解 |
| line ~773 | `on_char()` 'A' 鍵整合 SLA AutomaticGeneration | 修改 |
| line ~782 | `on_char()` RETURN ApplyChanges | 取消註解 |
| line ~790 | `on_char()` 'R' ResetClippingPlane | 取消註解 |
| line ~800 | `on_char()` DELETE 加入 SlaSupports/Hollow | 修改 |
| line ~817 | `on_char()` 'M' ManualEditing | 取消註解 |
| line ~866 | `on_key()` KEY_UP SlaSupports/Hollow 條件 | 取消註解 |
| line ~871 | `on_key()` KEY_UP SlaSupports/Hollow 動態轉型 | 取消註解 + 重構 |
| line ~916 | `on_key()` KEY_DOWN Shift/Alt for SlaSupports | 取消註解 |
| line ~1024 | `update_after_undo_redo()` reslice_SLA_supports | 取消註解 |
| line ~1386 | `is_in_editing_mode()` SlaSupports 檢查 | 取消註解 |

#### emplace_back 修改重點
```cpp
// 修改前 (使用 sprite_id++):
//m_gizmos.emplace_back(new GLGizmoSlaSupports(m_parent, "sla_supports.svg", sprite_id++));
//m_gizmos.emplace_back(new GLGizmoHollow(m_parent, "hollow.svg", sprite_id++));

// 修改後 (使用 EType，與現有 FDM gizmo 一致):
m_gizmos.emplace_back(new GLGizmoSlaSupports(m_parent, "sla_supports.svg", EType::SlaSupports));
m_gizmos.emplace_back(new GLGizmoHollow(m_parent, "hollow.svg", EType::Hollow));
```

---

### 3. GLGizmosCommon.hpp
**路徑**: `src/slic3r/GUI/Gizmos/GLGizmosCommon.hpp`

| 修改位置 | 內容 |
|---------|------|
| includes | 新增 `#include "libslic3r/SLA/Hollowing.hpp"` |
| CommonGizmosDataID | 新增 `HollowedMesh = 1 << 5` 和 `SupportsClipper = 1 << 6` |
| SelectionInfo | 新增 `print_object()` 方法和 `m_print_object` 成員 |
| CommonGizmosDataPool | 取消 `hollowed_mesh()` 和 `supports_clipper()` 註解 |
| 新增 HollowedMesh class | SLA 挖空網格資料管理 |
| 新增 SupportsClipper class | SLA 支撐截面渲染 |

#### 新增類別定義

**HollowedMesh** (SLA gizmo 需要的挖空網格快取):
- `get_hollowed_mesh()` → 回傳挖空後的 TriangleMesh* (或 nullptr)
- `get_drainholes()` → 回傳排水孔集合 (含 `failed` 狀態)

**SupportsClipper** (SLA 支撐截面渲染器):
- `render_cut()` → 渲染支撐和底座的截面

---

### 4. GLGizmosCommon.cpp
**路徑**: `src/slic3r/GUI/Gizmos/GLGizmosCommon.cpp`

| 修改位置 | 內容 |
|---------|------|
| 建構子 | 取消 HollowedMesh 和 SupportsClipper 的註解 |
| SelectionInfo::on_update() | 新增 m_print_object 查詢和 SLA z_shift 計算 |
| SelectionInfo::on_release() | 新增 m_print_object = nullptr |
| 新增 hollowed_mesh() getter | CommonGizmosDataPool 存取器 |
| 新增 supports_clipper() getter | CommonGizmosDataPool 存取器 |
| 新增 HollowedMesh 實作 | on_update/on_release/get_hollowed_mesh/get_drainholes |
| 新增 SupportsClipper 實作 | on_update/on_release/render_cut (來自 PrusaSlicer) |

---

### 5. CMakeLists.txt
**路徑**: `src/slic3r/CMakeLists.txt`

| 修改位置 | 內容 |
|---------|------|
| line 134-135 | 取消 GLGizmoSlaSupports.cpp/hpp 的註解 |
| line 144-145 | 取消 GLGizmoHollow.cpp/hpp 的註解 |

---

### 6. 資源檔案 (複製)

| 來源 | 目標 |
|------|------|
| `PrusaSlicer/resources/icons/sla_supports.svg` | `PhrozenOrca/resources/images/sla_supports.svg` |
| `PrusaSlicer/resources/icons/hollow.svg` | `PhrozenOrca/resources/images/hollow.svg` |

> SLA gizmo icon 無 dark mode 變體 (PrusaSlicer 也沒有)，兩種模式使用相同 icon

---

## 技術決策

### 1. EType enum 順序
SlaSupports 和 Hollow 加在 BrimEars 之後、Undefined 之前。
FaceRecognition 保持註解。enum 值自動遞增，不影響現有 gizmo。

### 2. HollowedMesh 來源
PrusaSlicer 也已移除 HollowedMesh 類別定義 (只剩 forward declaration)。
由於 PhrozenOrca 的 SLA gizmo 仍然引用它，我們基於 SLAPrintObject API 重新實作了最小版本。

### 3. SelectionInfo 增強
新增 `print_object()` 方法以支持 SupportsClipper 和其他 SLA 功能。
更新 `on_update()` 以查詢 SLAPrintObject 並計算正確的 z_shift。

### 4. 保持 PhrozenOrca 繼承架構
```
PhrozenOrca: GLGizmoSlaSupports → GLGizmoBase (保持不變)
```
不移植 PrusaSlicer 的 GLGizmoSlaBase 中間類 (留給 Phase 4)。

---

## 未修改的檔案

| 檔案 | 原因 |
|------|------|
| GLGizmoSlaSupports.hpp/cpp | 原始碼已完整存在，直接啟用 |
| GLGizmoHollow.hpp/cpp | 原始碼已完整存在，直接啟用 |
| PhrozenOrca 客製化程式碼 | 未觸及任何 PhrozenOrca 特有修改 |
| FDM gizmo 程式碼 | 未觸及任何 FDM 專用程式碼 |

---

## 預期行為

| 場景 | 預期結果 |
|------|---------|
| FDM printer 模式 | SLA gizmo 不會出現在 toolbar (on_is_activable 回傳 false) |
| SLA printer 模式 | SLA gizmo 出現在 toolbar，FDM painter gizmo 消失 |
| 通用 gizmo | Move/Rotate/Scale 等兩種模式都顯示 |
| Assembly view | 不受影響 (有獨立 gizmo 過濾) |

---

## 待驗證項目

- [ ] 編譯成功
- [ ] FDM 模式下 SLA gizmo 不出現
- [ ] SLA 模式下 SLA gizmo 出現
- [ ] SlaSupports gizmo 可啟用
- [ ] Hollow gizmo 可啟用
- [ ] 現有 FDM gizmo 功能不受影響
