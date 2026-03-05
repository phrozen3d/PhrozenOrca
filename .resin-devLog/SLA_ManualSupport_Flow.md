# SLA 手動長出支撐 — 完整流程文件

> 分析日期：2026-03-05
> 分析範圍：PhrozenOrca GLGizmoSlaSupports + GLGizmoSlaBase + GLGizmosManager

---

## 一、Gizmo 系統架構與註冊

### 類別繼承鏈

```
GLGizmoBase
  └── GLGizmoSlaBase          (Gizmos/GLGizmoSlaBase.hpp)
        └── GLGizmoSlaSupports  (Gizmos/GLGizmoSlaSupports.hpp)
```

### 靜態註冊位置

`GLGizmosManager.cpp:227` — `init_gizmos()` 在程式啟動時呼叫一次：

```cpp
m_gizmos.emplace_back(new GLGizmoSlaSupports(
    m_parent, "sla_supports.svg", EType::SlaSupports));
```

所有 gizmo 存放在 `m_gizmos[]` 陣列，以 `EType` enum 為索引。
`EType::SlaSupports` 的索引固定，不與其他 gizmo 衝突。

### 建構子設計（no-step 特殊處理）

`GLGizmoSlaSupports.cpp:110`

```cpp
// 使用 no-step constructor (m_min_sla_print_object_step = -1)
// 代表不要求任何 SLA 步驟先完成 → 進入 gizmo 立刻啟用滑鼠輸入
GLGizmoSlaSupports::GLGizmoSlaSupports(...)
    : GLGizmoSlaBase(parent, icon_filename, sprite_id)
```

對比 `GLGizmoHollow`：使用 `slaposSliceSupports` 作為最低步驟，代表要等切片完才能互動。
`GLGizmoSlaSupports` 沒有此限制，讓使用者隨時可以手動放置支撐點。

---

## 二、進入支撐編輯模式的完整流程

### 步驟順序

```
使用者選取模型 (單一 instance)
  │
  ▼
點擊工具列 sla_supports.svg 圖示，或按快捷鍵 Ctrl+L
  │
  ▼
GLGizmosManager::open_gizmo(EType::SlaSupports)   [GLGizmosManager.cpp:346]
  │  ├── on_is_activable() 檢查（見下方條件表）
  │  ├── activate_gizmo(SlaSupports) → m_current = SlaSupports
  │  └── update_data() → 觸發 data_changed()
  │
  ▼
GLGizmoSlaSupports::on_set_state()  [GLGizmoSlaSupports.cpp:1016]
  │  (On 方向)
  └── 從 sla_prints preset config 讀取 support_head_front_diameter
      → 設定 m_new_point_head_diameter 預設值
  │
  ▼
GLGizmoSlaSupports::data_changed()  [GLGizmoSlaSupports.cpp:141]
  ├── instances_hider()->set_hide_full_scene(true)  ← 隱藏其他物件
  ├── update_volumes()                              ← 載入 SLA mesh 到 m_volumes
  ├── 若 sla_points_status == Generating
  │     → get_data_from_backend()                  ← 撈取 auto-generate 結果
  └── register_point_raycasters_for_picking()       ← 建立支撐點的點選 raycaster
```

### on_is_activable() 條件

`GLGizmoSlaSupports.cpp:967`

| 條件 | 未滿足時 |
|------|----------|
| 目前 printer preset 為 ptSLA | 不顯示工具列圖示 |
| 選取為單一 instance | gizmo 無法開啟 |
| 選取的 volume 沒有超出列印範圍 | gizmo 無法開啟 |

### on_is_selectable() 條件

`GLGizmoSlaSupports.cpp:984`

```cpp
return (printer_technology() == ptSLA);
// SLA 機台才在工具列顯示此圖示
```

---

## 三、GLGizmoSlaBase 提供的共用能力

`GLGizmoSlaBase.cpp:88` — `on_get_requirements()` 回傳的資源集合：

| 資源 (CommonGizmosDataID) | 說明 |
|--------------------------|------|
| `SelectionInfo` | 取得 ModelObject、SLAPrintObject、active instance index |
| `InstancesHider` | `set_hide_full_scene()` — 進入 gizmo 時隱藏場景其他物件 |
| `Raycaster` | 滑鼠射線打到 mesh 表面，用於新增支撐點位置判斷 |
| `ObjectClipper` | 剖面顯示（拖動 Clipping of view 滑桿） |
| `SupportsClipper` | 剖面顯示支撐結構（與 model clipper 聯動） |

### update_volumes() 行為

`GLGizmoSlaBase.cpp:98`

當 SLAPrintObject 存在且 mesh 非空時：
- 主模型 mesh (volume_id = 0)
- 支撐 mesh (volume_id = -slaposSupportTree)，若 `po->support_mesh()` 非空
- Pad mesh (volume_id = -slaposPad)，若 `po->pad_mesh()` 非空

當 SLAPrintObject 不存在（PartPlate 尚未連接）：
- Fallback：直接從 Selection 複製 GLVolume，保留原始顏色

---

## 四、UI 面板結構與操作

### 4.1 顯示模式切換（頂部圖示按鈕）

`GLGizmoSlaSupports.cpp:829`

兩個 icon button，icon 來自 `/resources/images/support_structure*.svg`：

```
[ support_structure.svg (灰色) ]  [ support_structure_invisible.svg (彩色) ]
```

| 圖示狀態 | m_show_support_structure | 顯示內容 |
|---------|--------------------------|----------|
| 支撐點模式（預設） | `false` | 只顯示彩色球形支撐點（sphere + cone）|
| 支撐結構模式 | `true` | 顯示完整 3D 支撐柱+底座 mesh（via SupportsClipper）|

點擊後觸發：
```cpp
show_sla_supports(m_show_support_structure); // 控制 SupportsClipper 是否渲染
if (m_show_support_structure) {
    if (m_normal_cache.empty())
        auto_generate();              // 無點 → 先自動生成再顯示
    else
        reslice_until_step(slaposPad); // 有點 → 計算到 Pad 步驟
}
```

### 4.2 非編輯模式（預設狀態）

| 按鈕 | 對應函式 | Pipeline 影響 |
|------|---------|---------------|
| Auto-generate points | `auto_generate()` | reslice 到 slaposSupportPoints（或 slaposPad） |
| Manual editing | `switch_to_editing_mode()` | 無 pipeline 影響 |
| Remove all | 刪除全部點 → apply | reslice 到 slaposSupportPoints |
| Minimal distance slider | 設定 `m_minimal_point_distance` | 僅更新參數 |
| Points density slider | 設定 `m_density` | 僅更新參數 |
| Clipping of view slider | `object_clipper()->set_position_by_ratio()` | 即時剖面更新 |

### 4.3 編輯模式

| 按鈕 | 對應函式 |
|------|---------|
| Apply changes | `editing_mode_apply_changes()` → 寫回 ModelObject → reslice |
| Discard changes | `editing_mode_discard_changes()` → 還原 m_normal_cache |
| Remove selected | `delete_selected_points()` |

### 4.4 鍵盤快捷鍵（gizmo 開啟中）

`GLGizmosManager.cpp:776-916`

| 快捷鍵 | 行為 |
|--------|------|
| `A` | AutomaticGeneration → `auto_generate()` |
| `M` | ManualEditing → `switch_to_editing_mode()` |
| `Enter` | ApplyChanges → `editing_mode_apply_changes()` |
| `ESC` | DiscardChanges → 詢問對話框 |
| `Delete` / `Backspace` | 刪除選取的支撐點 |
| `Ctrl+A` | 全選支撐點 |
| `R` | ResetClippingPlane |
| 滾輪 | 調整支撐點頭部大小（Ctrl + 滾輪） |

---

## 五、自動生成支撐流程

`GLGizmoSlaSupports.cpp:1235`

```
auto_generate()
  │
  ├── 若 sla_points_status == UserModified && cache 非空
  │     → 彈出「Autogeneration will erase all manually edited points.」確認
  │
  ├── mo->sla_points_status = PointsStatus::Generating
  │
  └── reslice_until_step(
          m_show_support_structure ? slaposPad : slaposSupportPoints)
```

### Pipeline 呼叫鏈

```
reslice_until_step(step)                          [GLGizmoSlaBase.cpp:74]
  → wxGetApp().CallAfter(...)
  → plater()->reslice_SLA_until_step(step, *model_object)  [Plater.cpp:13089]
  → background_process.set_task({to_object_step = step})
  → SLAPrint::process() 只執行到 step 為止（partial pipeline）
  → on_process_completed() 觸發
  → data_changed() 被呼叫
  → get_data_from_backend() 撈取計算結果到 m_normal_cache
```

### get_data_from_backend()

`GLGizmoSlaSupports.cpp:1210`

```cpp
// 從 SLAPrintObject 讀取 auto-generate 後的支撐點
for (const SLAPrintObject* po : sla_print()->objects()) {
    if (po->model_object()->id() == mo->id()) {
        m_normal_cache = po->get_support_points();
        // 注意：不寫回 ModelObject，避免觸發 backend invalidation
    }
}
```

---

## 六、手動新增支撐點流程

進入編輯模式後（`switch_to_editing_mode()` `GLGizmoSlaSupports.cpp:1256`）：

### switch_to_editing_mode()

```cpp
wxGetApp().plater()->enter_gizmos_stack(); // 進入 undo/redo 子 stack
m_editing_mode = true;
m_editing_cache = m_normal_cache;          // 複製到可編輯的暫存
select_point(NoPoints);                    // 清除選取
```

### 滑鼠操作（on_mouse + gizmo_event）

`GLGizmoSlaSupports.cpp:181`

| 操作 | 行為 |
|------|------|
| 左鍵點模型表面 | `unproject_on_mesh()` 射線打面 → `m_editing_cache` 加入新 SupportPoint |
| Shift + 左鍵點支撐點 | 選取（多選） |
| Alt + 左鍵 | 刪除被 hover 的支撐點 |
| 拖曳支撐點 | `on_start_dragging()` 記錄起始狀態 / `on_stop_dragging()` 建立 undo snapshot |
| 右鍵點支撐點 | 選取單點 |
| 矩形框選 | `m_selection_rectangle` 框選多點 |

### editing_mode_apply_changes()

`GLGizmoSlaSupports.cpp:1156`

```cpp
disable_editing_mode();   // 先離開 editing undo stack，再取 snapshot
if (unsaved_changes()) {
    TakeSnapshot("Support points edit");
    mo->sla_support_points = (從 m_editing_cache 取出 SupportPoint 列表);
    mo->sla_points_status = PointsStatus::UserModified;
    reslice_until_step(slaposSupportPoints);
}
```

---

## 七、渲染架構

`GLGizmoSlaSupports.cpp:235` — `on_render()`

```
on_render()
  ├── selected_print_object_exists() 檢查 — 物件不在 SLAPrint 中時關閉 gizmo
  ├── render_volumes()          ← 來自 GLGizmoSlaBase，渲染 m_volumes（model + support + pad）
  ├── render_points()           ← 渲染所有支撐點（sphere head + cone tail）
  ├── m_selection_rectangle.render()  ← 框選矩形
  ├── object_clipper()->render_cut()  ← model 剖面線
  └── (若 m_show_support_structure)
        supports_clipper()->render_cut()  ← support 剖面線
```

支撐點顏色邏輯（`render_points()`）：

| 狀態 | 顏色 |
|------|------|
| 一般點（non-selected） | 黃色 |
| 選取點 | 藍色 |
| Hover 點 | 綠色 |
| clipped（被剖面切掉） | 不渲染，raycaster 設為 inactive |

---

## 八、Gizmo 關閉流程

`GLGizmoSlaSupports.cpp:1026` — `on_set_state()` (Off 方向)

```
on_set_state(Off)
  │
  ├── 若 m_editing_mode == true && unsaved_changes() && is_activable()
  │     → 彈出「Save support points?」(Yes / No / Cancel)
  │         Yes  → editing_mode_apply_changes()
  │         No   → editing_mode_discard_changes()
  │         Cancel → m_state = On (阻止關閉)
  │
  └── 若確認關閉：
        ├── disable_editing_mode()          ← 確保非 editing 狀態
        ├── m_old_mo_id = -1                ← 清除快取的 object ID
        ├── post_event(EVT_GLCANVAS_FORCE_UPDATE)
        └── instances_hider()->set_hide_full_scene(false)  ← 還原場景完整顯示
```

---

## 九、關鍵資料流總覽

```
3MF 檔案 / Undo-Redo snapshot
  │
  ▼
ModelObject::sla_support_points        ← 永久儲存（寫入 3MF）
ModelObject::sla_points_status         ← AutoGenerated / UserModified / Generating / None
  │
  ▼ editing_mode_apply_changes() 寫入 / reload_cache() 讀取
  │
m_normal_cache                         ← 目前生效的支撐點（顯示用 / 編輯起點）
  │
  ▼ switch_to_editing_mode() 複製
  │
m_editing_cache                        ← 編輯中的暫存（Apply 前不影響 backend）
  │
  ▼ 自動生成後 get_data_from_backend() 填入
  │
SLAPrintObject 內部快取                 ← backend 計算結果（pipeline 跑完後）
```

---

## 十、相關原始碼索引

| 功能 | 檔案 | 行號 |
|------|------|------|
| Gizmo 靜態註冊 | `Gizmos/GLGizmosManager.cpp` | 227 |
| open_gizmo() | `Gizmos/GLGizmosManager.cpp` | 346 |
| activate_gizmo() | `Gizmos/GLGizmosManager.cpp` | 1319 |
| 建構子（no-step） | `Gizmos/GLGizmoSlaSupports.cpp` | 110 |
| on_is_activable() | `Gizmos/GLGizmoSlaSupports.cpp` | 967 |
| on_is_selectable() | `Gizmos/GLGizmoSlaSupports.cpp` | 984 |
| on_set_state() | `Gizmos/GLGizmoSlaSupports.cpp` | 1016 |
| data_changed() | `Gizmos/GLGizmoSlaSupports.cpp` | 141 |
| on_render() | `Gizmos/GLGizmoSlaSupports.cpp` | 235 |
| on_render_input_window() | `Gizmos/GLGizmoSlaSupports.cpp` | ~700 |
| 顯示模式切換（icon） | `Gizmos/GLGizmoSlaSupports.cpp` | 829 |
| auto_generate() | `Gizmos/GLGizmoSlaSupports.cpp` | 1235 |
| switch_to_editing_mode() | `Gizmos/GLGizmoSlaSupports.cpp` | 1256 |
| editing_mode_apply_changes() | `Gizmos/GLGizmoSlaSupports.cpp` | 1156 |
| get_data_from_backend() | `Gizmos/GLGizmoSlaSupports.cpp` | 1210 |
| on_get_requirements() | `Gizmos/GLGizmoSlaBase.cpp` | 88 |
| update_volumes() | `Gizmos/GLGizmoSlaBase.cpp` | 98 |
| reslice_until_step() | `Gizmos/GLGizmoSlaBase.cpp` | 74 |
| unproject_on_mesh() | `Gizmos/GLGizmoSlaBase.cpp` | 271 |
