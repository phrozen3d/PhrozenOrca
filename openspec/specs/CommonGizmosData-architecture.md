# CommonGizmosData 架構說明

## 概覽

`CommonGizmosData` 是一套「共用資源池」系統，讓多個 SLA Gizmo 可以共享同一份資料（mesh、raycaster、clipping plane 等），避免重複計算，同時統一管理資源的生命週期。

---

## 類別關係圖

```
GLGizmosManager
  └── CommonGizmosDataPool         ← 唯一一個 pool 實例，由 manager 持有
        └── m_data: map<CommonGizmosDataID, unique_ptr<CommonGizmosDataBase>>
              ├── SelectionInfo      (ID bitmask: 0x01)
              ├── InstancesHider     (ID bitmask: 0x02)
              ├── HollowedMesh       (ID bitmask: 0x20)
              ├── Raycaster          (ID bitmask: 0x08)
              ├── ObjectClipper      (ID bitmask: 0x10)
              └── SupportsClipper    (ID bitmask: 0x40)

各 SLA Gizmo (GLGizmoSlaBase 子類)
  └── m_c: CommonGizmosDataPool*   ← 持有 pool 的原始指標，不擁有
```

---

## CommonGizmosDataBase — 所有 resource 的基類

**檔案**: `src/slic3r/GUI/Gizmos/GLGizmosCommon.hpp:120`

```
CommonGizmosDataBase
  ├── m_is_valid      bool，目前是否被維護（初始為 false）
  ├── m_common        back-pointer 回 pool（用來存取其他 resource）
  │
  ├── update()        → on_update()  + 設 m_is_valid = true
  ├── release()       → on_release() + 設 m_is_valid = false
  └── is_valid()      → 回傳 m_is_valid
```

`on_update()` 和 `on_release()` 為純虛函數，各子類自行實作。

---

## CommonGizmosDataPool — 生命週期控制器

**檔案**: `src/slic3r/GUI/Gizmos/GLGizmosCommon.hpp:86`

### 每 frame 觸發點

```
GLGizmosManager::update_data()       [每 frame 呼叫]
  └── m_common_gizmos_data->update(
          get_current()
              ? get_current()->get_requirements()   // active gizmo 宣告需要哪些 resource
              : CommonGizmosDataID(0)               // 無 active gizmo → 全部 release
      )
```

### update() 邏輯

```
for each resource in m_data:
    if (required bitmask 包含此 resource)
        data->update()          → on_update() + is_valid = true
    else if (data->is_valid())
        data->release()         → on_release() + is_valid = false
```

### Getter 行為

所有 getter（`object_clipper()`、`raycaster()` 等）在 `is_valid == false` 時回傳 `nullptr`：

```cpp
ObjectClipper* CommonGizmosDataPool::object_clipper() const {
    ObjectClipper* oc = dynamic_cast<ObjectClipper*>(...);
    return (oc && oc->is_valid()) ? oc : nullptr;
}
```

---

## 各子類職責

### SelectionInfo (0x01)
**依賴**: 無

提供所有其他 resource 都需要的基礎資訊：
- `model_object()` — 目前選取的 `ModelObject*`
- `print_object()` — 對應的 `SLAPrintObject*`
- `get_active_instance()` — active instance index
- `get_sla_shift()` — SLA Z 方向位移量

`on_update()`: 從 canvas selection 讀取並快取以上資訊。  
`on_release()`: 清空所有指標。

---

### InstancesHider (0x02)
**依賴**: SelectionInfo

管理非選取 instance 的可見性，以及截面輪廓的渲染：
- `set_hide_full_scene(bool)` — 完全隱藏所有 model（gizmo 自行渲染時使用）
- `render_cut()` — 渲染截面填色輪廓

`on_update()`: 隱藏非選取 instance。  
`on_release()`: 恢復所有 instance 可見性。

---

### Raycaster (0x08)
**依賴**: SelectionInfo

提供 `MeshRaycaster`，用於 gizmo 的滑鼠 hit-test（點擊模型表面偵測）：
- `raycaster()` — 取得 `MeshRaycaster*`（單一 volume）
- `raycasters()` — 取得所有 volume 的 raycaster 列表

`on_update()`: 建構 `MeshRaycaster`（mesh 變更時重建）。  
`on_release()`: 清空 raycaster。

---

### ObjectClipper (0x10)
**依賴**: SelectionInfo

管理 **model mesh** 的截面平面，最關鍵的 resource：
- `m_clp` — `unique_ptr<ClippingPlane>`，截面平面定義
- `m_clp_ratio` — 目前 slider 位置（0.0 ~ 1.0）
- `m_active_inst_bb_radius` — instance bounding box radius（`set_position_by_ratio` 用）

**主要 API**:

| 方法 | 說明 |
|------|------|
| `get_position()` | 回傳 `m_clp_ratio` |
| `set_position_by_ratio(pos, keep_normal, vertical_normal=false)` | 依 bounding box 座標系計算 plane，normal 預設跟 camera forward |
| `set_range_and_pos(normal, offset, pos)` | **直接設定** ClippingPlane（絕對 mm 座標），同時更新 m_clp_ratio |
| `get_clipping_plane()` | 回傳 `m_clp.get()`（用於渲染時傳給 OpenGL） |
| `render_cut()` | 渲染截面填色 |

`on_update()`: 重建 MeshClipper mesh（SLA 優先使用 hollowed mesh）。**不重設** `m_clp_ratio`。  
`on_release()`: **清空 `m_clp` + 重設 `m_clp_ratio = 0`**。⚠️ 這是生命週期的關鍵行為。

---

### HollowedMesh (0x20)
**依賴**: SelectionInfo

從 `SLAPrintObject` 取得已完成空心化（+ drill holes）的 mesh：
- `get_hollowed_mesh()` — 回傳 `TriangleMesh*`（若有完成 `slaposDrillHoles` step）
- `get_drainholes()` — 回傳排水孔列表

`on_update()`: 從 print object 快取 hollowed mesh。  
`on_release()`: 清空快取。

---

### SupportsClipper (0x40)
**依賴**: SelectionInfo + ObjectClipper

管理 **support mesh** 的截面，與 ObjectClipper 使用同一個截面平面：
- `render_cut()` — 渲染 support 截面（使用 ObjectClipper 的平面方向）

`on_update()`: 從 print object 取得 support/pad mesh 並建構 MeshClipper。  
`on_release()`: 清空 support clipper。

---

## 依賴關係圖

```
SelectionInfo (0x01)
    ↑
    ├── InstancesHider (0x02)
    ├── Raycaster      (0x08)
    ├── HollowedMesh   (0x20)
    └── ObjectClipper  (0x10)
              ↑
              └── SupportsClipper (0x40)
```

Debug build 會 assert 依賴順序（依賴的 resource ID 必須小於自身 ID）。

---

## SLA Gizmo 的 requirements 宣告

所有繼承自 `GLGizmoSlaBase` 的 gizmo（Hollow、Drill、SlaSupports）共用同一份 requirements：

```cpp
// GLGizmoSlaBase.cpp:88
CommonGizmosDataID GLGizmoSlaBase::on_get_requirements() const {
    return CommonGizmosDataID(
          int(CommonGizmosDataID::SelectionInfo)
        | int(CommonGizmosDataID::InstancesHider)
        | int(CommonGizmosDataID::Raycaster)
        | int(CommonGizmosDataID::ObjectClipper)
        | int(CommonGizmosDataID::SupportsClipper)
    );
    // 注意：不包含 HollowedMesh（由 ObjectClipper::on_update 內部自行處理 SLA branch）
}
```

---

## 進入 Hollow Gizmo 的完整 frame 序列

```
frame N（無 active gizmo）:
  update(0)
    → ObjectClipper.release()
        m_clp = null
        m_clp_ratio = 0       ← ⚠️ 歸零
        is_valid = false

frame N+1（切換到 Hollow Gizmo）:
  update(SelectionInfo | InstancesHider | Raycaster | ObjectClipper | SupportsClipper)
    → SelectionInfo.update()    → 讀取 ModelObject、SLAPrintObject
    → InstancesHider.update()   → 隱藏非選取 instance
    → Raycaster.update()        → 建構 raycaster
    → ObjectClipper.update()    → 重建 MeshClipper（SLA: 使用 hollowed mesh）
                                   ⚠️ m_clp 仍為 null（沒有 set_position → 沒有平面）
                                   ⚠️ m_clp_ratio 仍為 0（on_update 不恢復）
    → SupportsClipper.update()  → 重建 support mesh clipper

  → GLGizmosManager::update_data() 的 just_entered 偵測：
      oc_valid = true, m_oc_was_valid_last_frame = false → just_entered = true

  → 我們的 sync（Stage 2）:
      z_high = get_use_clipping_planes()
                   ? get_prepare_clip_z_high()     // prepare slider 有設值
                   : get_prepare_scene_max_z()     // 無裁切 → 平面放最頂端

      rough_ratio = clamp(1.0 - z_high / scene_max, 0.0, 1.0)

      oc->set_range_and_pos(Vec3d(0, 0, -1), z_high, rough_ratio)
          → m_clp = ClippingPlane({0,0,-1}, z_high)  ✓ 方向與 prepare slider 一致
          → m_clp_ratio = rough_ratio                ✓ Hollow 的 slider 顯示位置同步
```

---

## Hollow Gizmo 的 view clip 操作路徑

| 操作 | 呼叫路徑 |
|------|---------|
| 拖動 slider | `GLGizmoHollow.cpp:485` → `object_clipper()->set_position_by_ratio(clp_dist, true)` |
| 輸入數值 | `GLGizmoHollow.cpp:492` → `object_clipper()->set_position_by_ratio(val, false)` |
| Ctrl+滾輪 | `GLGizmoHollow.cpp:147-156` → `set_position_by_ratio(pos ± 0.01, true)` |
| Reset direction | `GLGizmoHollow.cpp:523` → `set_position_by_ratio(-1., false)` （-1 = 保留目前 ratio） |

**注意**: Hollow gizmo 自己的操作全部使用 `set_position_by_ratio`，它會用 bounding box 座標系重新計算 normal（跟 camera forward 方向），會覆蓋我們 sync 設定的 `{0,0,-1}` normal。這是目前設計上的分岐點。

---

## ClippingPlane 數學慣例

```
ClippingPlane(n, d)  clips where  n · p + d < 0

prepare slider 使用的平面：
  ClippingPlane({0, 0, -1}, z_high)
  → clips where  -z + z_high < 0
  → clips where  z > z_high          ← 隱藏 z_high 以上的部分（從上往下裁切）✓
```

---

## 相關檔案

| 檔案 | 說明 |
|------|------|
| `src/slic3r/GUI/Gizmos/GLGizmosCommon.hpp` | 所有 class 宣告、CommonGizmosDataID enum |
| `src/slic3r/GUI/Gizmos/GLGizmosCommon.cpp` | Pool/Base/各子類實作 |
| `src/slic3r/GUI/Gizmos/GLGizmosManager.cpp` | `update_data()` 驅動、just_entered sync（Stage 2） |
| `src/slic3r/GUI/Gizmos/GLGizmoSlaBase.cpp` | `on_get_requirements()` 宣告 |
| `src/slic3r/GUI/Gizmos/GLGizmoHollow.cpp` | Hollow gizmo 的 view clip UI 操作 |
| `src/slic3r/GUI/GLCanvas3D.cpp` | `_on_prepare_clip_changed()` → prepare slider 同步觸發 |
