# Step 2.3: SLAPrint 初始化 — 結果報告

**執行日期**: 2026-02-17
**前置條件**: Step 2.2 A~D 通過，E4 crash 確認根因

---

## 問題描述

`BackgroundSlicingProcess::m_sla_print` 永遠是 nullptr，導致：
- Fix 5 (GLGizmosCommon.cpp) — 所有 Gizmo 啟動時 crash
- E4 (GLGizmoSlaSupports.cpp:1088) — SLA 支撐自動生成 crash
- SLA 切片完全無法執行

---

## 根因分析

### 呼叫鏈

```
任何 Gizmo / SLA 功能
  → GLCanvas3D::sla_print()
    → BackgroundSlicingProcess::sla_print()
      → m_sla_print  ← 永遠 nullptr
```

### 為什麼 m_sla_print 是 nullptr

PhrozenOrca 用 PartPlate 系統取代 PrusaSlicer 的直接 print 設定：

```cpp
// Plater.cpp:3054 — 現在使用 PartPlate
partplate_list.update_slice_context_to_current_plate(background_process);

// Plater.cpp:3055-3071 — 原始 PrusaSlicer 程式碼被整段註解
/*
background_process.set_fff_print(&fff_print);
background_process.set_sla_print(&sla_print);    ← 不再執行
...
sla_print.set_status_callback(statuscb);          ← 不再執行
*/
```

PartPlate 系統**完全沒有 SLA 概念**：

| PartPlate 元件 | SLA 支援 | 說明 |
|----------------|----------|------|
| `PartPlate::m_print` | ❌ `Print*` only | 沒有 `m_sla_print` 成員 |
| `PartPlate::set_print()` | ❌ `ptFFF` only | 有 `//todo, for other printers` 註解 |
| `PartPlate::update_slice_context()` | ❌ | 只呼叫 `set_fff_print()`，不呼叫 `set_sla_print()` |
| `PartPlateList::init()` | ❌ | 只建立 `Print*`，不建立 `SLAPrint*` |
| `PartPlateList::create_plate()` | ❌ | 同上 |

### PrusaSlicer 的做法（對照）

PrusaSlicer 沒有 PartPlate，直接在 `Plater::priv` 建構時連接：

```cpp
background_process.set_fff_print(fff_prints.front().get());
background_process.set_sla_print(sla_prints.front().get());
background_process.select_technology(this->printer_technology);
```

兩種 print 都預先建立並連接，`select_technology()` 只是切換 `m_print` 指標。

---

## 實作方案選擇

### 方案 A：繞過 PartPlate，直接連接 SLAPrint ✅ 採用

| 優點 | 說明 |
|------|------|
| 不修改 PartPlate | 符合 CLAUDE.md 規則：不改 PhrozenOrca 客製化 |
| SLA 不需多板 | SLA 印表機使用單一樹脂槽，不需要 PartPlate 多板管理 |
| 最小修改 | 只需在 Plater.cpp 加 2~3 行 |
| 物件已存在 | `Plater::priv::sla_print` 成員已宣告（line 2460），只需連接 |

### 方案 B：擴展 PartPlate 支援 SLA ❌ 不採用

需修改 PhrozenOrca 特有的 PartPlate 系統，違反專案規則。

---

## 實作內容

### 修改 1：Plater.cpp — 連接 SLAPrint（line 3055~3059）

**位置**: `Plater.cpp` line 3054 之後

在 `partplate_list.update_slice_context_to_current_plate(background_process)` 之後加入：

```cpp
// Step 2.3: Connect SLAPrint to BackgroundSlicingProcess.
// PartPlate system only manages FFF prints (no SLA concept).
// SLA printers don't use multi-plate, so we directly connect
// the single sla_print instance from Plater::priv.
background_process.set_sla_print(&sla_print);
```

**效果**：
- `BackgroundSlicingProcess::m_sla_print` 有值
- `select_technology(ptSLA)` 時 `m_print = m_sla_print` 正確指向 `SLAPrint`
- Fix 5 的 nullptr 守衛不再觸發（但保留作為防禦性程式）
- E4 的 `sla_print()->objects()` 不再 crash

---

### 修改 2：Plater.cpp — update_background_process SLA 守衛（line 5531~5543）

**問題發現過程**：

修改 1 生效後，E4（按 A 自動生成支撐）不再 crash，但出現 FFF 的錯誤訊息：
> "one object has empty initial layer and can't be printed. Please cut the bottom or enable supports"

這是 FFF 切片的錯誤（GCode.cpp:1213），不應出現在 SLA 模式下。

**根因追蹤**：

```
auto_generate()
  → reslice_SLA_supports()
    → wxGetApp().plater()->reslice_SLA_supports()
      → reslice_SLA_until_step()
        → update_background_process(true, postpone_error_messages)
          → switch_print = true (預設值)
```

`update_background_process()` 的宣告（line 2760）：
```cpp
unsigned int update_background_process(
    bool force_validation = false,
    bool postpone_error_messages = false,
    bool switch_print = true   // ← 預設為 true
);
```

當 `switch_print = true` 時，執行：
```cpp
this->partplate_list.update_slice_context_to_current_plate(background_process);
```

而 `PartPlate::update_slice_context()` 會呼叫：
```cpp
process.select_technology(this->printer_technology);
// PartPlate 的 printer_technology 永遠是 ptFFF！
```

**結果**：即使修改 1 正確連接了 `m_sla_print`，每次呼叫 `update_background_process()` 都會把 `m_print` 重新指回 `m_fff_print`，導致 FFF 切片路徑執行。

**修改內容**（line 5531~5543）：

```cpp
if (switch_print)
{
    //BBS: update the current print to the current plate
    // Step 2.3: PartPlate only manages FFF prints. In SLA mode, skip
    // update_slice_context which would override m_print back to FFF.
    // Instead, re-apply select_technology to ensure m_print points to SLAPrint.
    if (this->printer_technology == ptSLA) {
        background_process.select_technology(ptSLA);
    } else {
        this->partplate_list.update_slice_context_to_current_plate(background_process);
        this->preview->update_gcode_result(partplate_list.get_current_slice_result());
    }
}
```

**效果**：
- SLA 模式下不再觸發 PartPlate 的 `update_slice_context`（避免被重設回 FFF）
- 改為直接呼叫 `select_technology(ptSLA)` 確保 `m_print` 指向 `SLAPrint`
- FDM 模式行為完全不變

---

### 修改 3（Fix 6）：bbs_3mf.cpp — timelapse_type nullptr 守衛（line 7818~7828）

**問題發現過程**：

修改 2 生效後，E4 第一次自動生成支撐成功。但**第二次重複執行**時 crash：
- 位置：`bbs_3mf.cpp:7818`
- 原始碼：`int timelapse_type = int(config.opt_enum<TimelapseType>("timelapse_type"));`

**根因**：

E4 第一次成功後，`plate_data->is_sliced_valid` 變為 true。之後 3MF 自動備份觸發 `_add_slice_info_config_file_to_archive()`，嘗試寫入 `timelapse_type` — 這是 FDM 專屬參數，SLA config 中不存在。

這與 Fix 2（`filament_colour`）、Fix 4（`seam_slope_type`）是同類問題：FDM 專屬參數在 SLA config 中不存在，直接存取會 crash。

**修改內容**（line 7818~7828）：

```cpp
// Guard: timelapse_type is FDM-only, default to -1 when not available (e.g. SLA mode)
int timelapse_type = -1;
if (config.option("timelapse_type")) {
    timelapse_type = int(config.opt_enum<TimelapseType>("timelapse_type"));
    for (auto it = plate_data->warnings.begin(); it != plate_data->warnings.end(); it++) {
        if (it->msg == NOT_GENERATE_TIMELAPSE) {
            timelapse_type = -1;
            break;
        }
    }
}
```

**效果**：
- SLA 模式下 `timelapse_type` 預設為 -1（無效值），不會 crash
- FDM 模式行為完全不變（`timelapse_type` 存在時正常讀取）

### 修改 4：GLGizmoSlaSupports — data_changed() 覆寫

**問題發現過程**：

修改 1~3 完成後，E4 自動生成支撐不再 crash，但**支撐點沒有顯示在模型上**（PrusaSlicer 會在模型表面顯示藍色示意點）。

**根因追蹤**：

```
GLGizmosManager::update_data()
  → m_gizmos[m_current]->data_changed()     // 每次渲染週期呼叫
    → GLGizmoBase::data_changed() {}         // ← 空實作！
```

PrusaSlicer 的 `GLGizmoSlaSupports` 覆寫了 `data_changed()`，在其中：
1. 檢查 `sla_points_status == Generating`
2. 呼叫 `get_data_from_backend()` 從 `SLAPrintObject` 取得生成的支撐點
3. 複製到 `m_normal_cache` 供渲染使用

PhrozenOrca 有對應的 `set_sla_support_data()` 方法（邏輯相同），但**從未被任何程式碼呼叫**。`data_changed()` 走的是 `GLGizmoBase` 的空實作。

**修改內容**：

`GLGizmoSlaSupports.hpp` (line 61)：
```cpp
void data_changed(bool is_serializing) override;
```

`GLGizmoSlaSupports.cpp` (line 86~90)：
```cpp
// Step 2.3: Override data_changed() so GLGizmosManager::update_data() can
// trigger backend polling. PhrozenOrca's set_sla_support_data() has the
// correct logic but was never called — PrusaSlicer uses data_changed() instead.
void GLGizmoSlaSupports::data_changed(bool is_serializing)
{
    set_sla_support_data(nullptr, m_parent.get_selection());
}
```

**效果**：
- `GLGizmosManager::update_data()` 現在正確觸發 `get_data_from_backend()`
- 背景程序生成支撐點後，Gizmo 能取回結果並填入 `m_normal_cache`
- 支撐點正確顯示在模型表面

### 修改 5：GLGizmoSlaSupports — on_mouse() 覆寫

**問題發現過程**：

修改 1~4 完成後，SLA 支撐 Gizmo 可以進入編輯模式（Edit mode），但**左鍵點擊模型表面無法新增支撐點**。

**根因追蹤**：

```
滑鼠點擊
  → GLCanvas3D::on_mouse()
    → m_gizmos.on_mouse()
      → GLGizmosManager::on_mouse()     // line 712-731
        → current_gizmo->on_mouse(evt)  // 呼叫 GLGizmoBase::on_mouse()
```

PhrozenOrca 的 `GLGizmosManager::on_mouse()` 會先檢查 gizmo 的 `on_mouse()` 覆寫。如果 gizmo 的 `on_mouse()` 回傳 `true`，事件被消費；否則交給 canvas 處理（旋轉/平移視角）。

`GLGizmoBase::on_mouse()` 預設只處理 grabber（位移/旋轉/縮放用），不知道 SLA 支撐點的存在。SLA 支撐的事件處理在 `gizmo_event()` 中。

PrusaSlicer 的 `GLGizmoSlaSupports` 覆寫了 `on_mouse()`，將滑鼠事件轉換為 `gizmo_event()` 呼叫。PhrozenOrca 沒有這個覆寫 — `gizmo_event()` 從未被呼叫。

**修改內容**：

`GLGizmoSlaSupports.hpp` (line 64)：
```cpp
bool on_mouse(const wxMouseEvent &mouse_event) override;
```

`GLGizmoSlaSupports.cpp` — 新增 `on_mouse()` 實作：

從 PrusaSlicer 移植 `on_mouse()` 方法，包含：
1. **LeftDown**: 呼叫 `gizmo_event(SLAGizmoEventType::LeftDown, ...)`
2. **LeftUp**: 呼叫 `gizmo_event(SLAGizmoEventType::LeftUp, ...)`
3. **RightDown**: 呼叫 `gizmo_event(SLAGizmoEventType::RightDown, ...)` — 刪除支撐點
4. **Dragging**: 呼叫 `gizmo_event(SLAGizmoEventType::Dragging, ...)`
5. 修飾鍵傳遞（Shift, Alt, Control）

**效果**：
- 左鍵點擊可在模型表面新增支撐點
- 拖拽和右鍵刪除需要 hover 偵測（見修改 6）

---

### 修改 6：GLGizmoSlaSupports — PickingModel + SceneRaycaster 註冊

**問題發現過程**：

修改 5 完成後，左鍵**新增**支撐點功能正常（`gizmo_event(LeftDown)` 使用 `unproject_on_mesh()` 取得模型表面位置，不依賴 hover）。但以下功能仍然失敗：

| 問題 | 症狀 | 原因 |
|------|------|------|
| 拖拽支撐點 | 點擊現有支撐點時新增新點而非拖拽 | `m_hover_id == -1`，走 add 邏輯而非 drag |
| Hover 變色 | 滑鼠移到支撐點上不會高亮 | `on_set_hover_id()` 檢查 `m_hover_id` 但永遠是 -1 |
| 右鍵刪除 | 右鍵變成視角位移 | `gizmo_event(RightDown)` 的 `m_hover_id == -1`，return false，事件交給 canvas |

**根因分析**：

三個症狀的共同根因：`m_hover_id` 永遠是 -1。

```
GLCanvas3D::_picking_pass()
  → SceneRaycaster::hit()          // 射線與場景物件求交
    → 對每個已註冊的 raycaster 做 ray-mesh intersection
    → 返回最近交點的 raycaster_id
  → if (hit.type == EType::Gizmo)
      m_gizmos.set_hover_id(hit.raycaster_id)   // line 6884
```

PhrozenOrca 使用 `SceneRaycaster` 系統做 hover 偵測。Gizmo 必須透過 `on_register_raycasters_for_picking()` 註冊射線碰撞器。

`GLGizmoSlaSupports` 的問題：

1. **無 `on_register_raycasters_for_picking()` 覆寫** — 使用 `GLGizmoBase` 的空實作
2. **`m_sphere`、`m_cone` 是 `GLModel`** — 純渲染用，沒有 `MeshRaycaster`（碰撞偵測）
3. **沒有 `m_point_raycasters` 管理** — 支撐點增刪時不會更新射線碰撞器

**PrusaSlicer 的做法（對照）**：

PrusaSlicer 使用 `PickingModel` (GLModel + MeshRaycaster)，並在 `on_register_raycasters_for_picking()` 中為每個支撐點註冊 sphere 和 cone 的碰撞器。BrimEars gizmo 在 PhrozenOrca 中已有相同模式。

**修改內容**：

#### HPP 變更 (`GLGizmoSlaSupports.hpp`)：

1. 新增 `#include "slic3r/GUI/MeshUtils.hpp"` 和 `#include "slic3r/GUI/SceneRaycaster.hpp"`
2. 將 `PickingModel m_cone` 和 `PickingModel m_sphere` 取代原本的 `GLModel`（`m_cylinder` 保留為 `GLModel`，cylinder 只用於渲染不用於 picking）
3. 新增成員：
   ```cpp
   std::vector<std::pair<std::shared_ptr<SceneRaycasterItem>,
                         std::shared_ptr<SceneRaycasterItem>>> m_point_raycasters;
   ```
4. 新增私有方法宣告：
   ```cpp
   void register_point_raycasters_for_picking();
   void unregister_point_raycasters_for_picking();
   void update_point_raycasters_for_picking_transform();
   ```
5. 新增保護覆寫宣告：
   ```cpp
   void on_register_raycasters_for_picking() override;
   void on_unregister_raycasters_for_picking() override;
   ```

#### CPP 變更 (`GLGizmoSlaSupports.cpp`)：

**變更 A — `on_render()` 模型初始化**：

將 `GLModel` 初始化改為 `PickingModel` 初始化，同時建立 `MeshRaycaster`：

```cpp
if (!m_cone.model.is_initialized()) {
    indexed_triangle_set cone_its = its_make_cone(1.0, 1.0, double(PI) / 12.0);
    m_cone.mesh_raycaster = std::make_unique<MeshRaycaster>(
        std::make_shared<const TriangleMesh>(cone_its));
    m_cone.model.init_from(std::move(cone_its));
}
if (!m_sphere.model.is_initialized()) {
    indexed_triangle_set sphere_its = its_make_sphere(1.0, double(PI) / 12.0);
    m_sphere.mesh_raycaster = std::make_unique<MeshRaycaster>(
        std::make_shared<const TriangleMesh>(sphere_its));
    m_sphere.model.init_from(std::move(sphere_its));
}
```

**變更 B — `render_points()` 渲染呼叫**：

所有 `m_cone.set_color()`/`m_cone.render()` 改為 `m_cone.model.set_color()`/`m_cone.model.render()`，`m_sphere` 同理。

**變更 C — `data_changed()` Raycaster 管理**：

```cpp
void GLGizmoSlaSupports::data_changed(bool is_serializing)
{
    set_sla_support_data(nullptr, m_parent.get_selection());
    // Step 2.3 Mod 6: Manage raycasters for support point picking
    if (m_point_raycasters.empty())
        register_point_raycasters_for_picking();
    else
        update_point_raycasters_for_picking_transform();
}
```

**變更 D — `gizmo_event()` LeftDown 後重新註冊**：

新增支撐點後，重新註冊所有射線碰撞器：
```cpp
unregister_point_raycasters_for_picking();
register_point_raycasters_for_picking();
```

**變更 E — `delete_selected_points()` 後重新註冊**：

刪除支撐點後，重新註冊所有射線碰撞器（同上）。

**變更 F — `switch_to_editing_mode()` 進入編輯模式**：

進入編輯模式時重新註冊射線碰撞器。

**變更 G — `disable_editing_mode()` 離開編輯模式**：

離開編輯模式時清除射線碰撞器。

**變更 H — 5 個新方法實作**：

```cpp
void GLGizmoSlaSupports::on_register_raycasters_for_picking()
{
    register_point_raycasters_for_picking();
}

void GLGizmoSlaSupports::on_unregister_raycasters_for_picking()
{
    unregister_point_raycasters_for_picking();
}

void GLGizmoSlaSupports::register_point_raycasters_for_picking()
{
    // 為每個支撐點的 sphere + cone 註冊到 SceneRaycaster
    // raycaster_id = 支撐點 index，用於設定 m_hover_id
    for (size_t i = 0; i < m_editing_cache.size(); ++i) {
        m_point_raycasters.emplace_back(
            m_parent.add_raycaster_for_picking(EType::Gizmo, i,
                *m_sphere.mesh_raycaster, Transform3d::Identity()),
            m_parent.add_raycaster_for_picking(EType::Gizmo, i,
                *m_cone.mesh_raycaster, Transform3d::Identity()));
    }
    update_point_raycasters_for_picking_transform();
}

void GLGizmoSlaSupports::unregister_point_raycasters_for_picking()
{
    for (size_t i = 0; i < m_point_raycasters.size(); ++i)
        m_parent.remove_raycasters_for_picking(EType::Gizmo, i);
    m_point_raycasters.clear();
}

void GLGizmoSlaSupports::update_point_raycasters_for_picking_transform()
{
    // 根據支撐點位置、法線、半徑，計算 sphere 和 cone 的世界空間 transform
    // 與 render_points() 的 transform 計算邏輯完全一致
}
```

**效果**：

| 功能 | 修改前 | 修改後 |
|------|--------|--------|
| 支撐點 Hover 變色 | ❌ `m_hover_id` 永遠 -1 | ✅ SceneRaycaster 偵測碰撞，設定正確 hover_id |
| 拖拽支撐點 | ❌ 新增新點 | ✅ `m_hover_id >= 0` 觸發 drag 邏輯 |
| 右鍵刪除支撐點 | ❌ 視角平移 | ✅ `m_hover_id >= 0` 觸發 delete，on_mouse 消費事件 |

---

## 測試清單

| # | 測試項目 | 預期結果 | 實測結果 | 備註 |
|---|----------|----------|----------|------|
| 1 | 編譯通過 | 無錯誤 | ✅ Pass | |
| 2 | 啟動無 crash | 正常啟動 | ✅ Pass | |
| 3 | FDM → SLA 切換 | 無 crash | ✅ Pass | |
| 4 | SLA → FDM 切換 | 無 crash | ✅ Pass | |
| 5 | E4 重測：按 A 自動生成支撐 | 支撐點顯示在模型上 | ✅ Pass | 需修改 4 才能顯示 |
| 6 | E5 重測：拖拽支撐點 | 可操作 | ✅ Pass | 需修改 5+6 |
| 7 | E6 重測：Delete 支撐點 | 可刪除 | ✅ Pass | 需修改 5+6 |
| 8 | D4 重測：Flatten gizmo | 仍正常（Fix 5 守衛仍在） | ✅ Pass | |
| 9 | FDM 切片 | 正常切片，不受影響 | ✅ Pass | |
| 10 | SLA 切片（進階） | 可觸發切片流程 | 🔄 Issues Found | 按 Slice Plate 後模型消失 — 見下方分析 |

---

## 修改檔案總覽

| 檔案 | 修改類型 | 位置 |
|------|----------|------|
| `src/slic3r/GUI/Plater.cpp` | 修改 1：新增 SLAPrint 連接 | line 3055~3059 |
| `src/slic3r/GUI/Plater.cpp` | 修改 2：update_background_process SLA 守衛 | line 5531~5543 |
| `src/libslic3r/Format/bbs_3mf.cpp` | 修改 3 (Fix 6)：timelapse_type nullptr 守衛 | line 7818~7828 |
| `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.hpp` | 修改 4：data_changed() 宣告 | line 63 |
| `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp` | 修改 4：data_changed() 實作 | line 86~97 |
| `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.hpp` | 修改 5：on_mouse() 宣告 | line 64 |
| `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp` | 修改 5：on_mouse() 實作 | PrusaSlicer 移植 |
| `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.hpp` | 修改 6：PickingModel + raycaster 成員宣告 | lines 5-7, 97-102, 137-143 |
| `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp` | 修改 6：PickingModel 初始化 + raycaster 管理 | 8 處變更 + 5 個新方法 |

---

## 風險評估

| 風險 | 機率 | 影響 | 緩解 |
|------|------|------|------|
| FDM 功能受影響 | 極低 | 高 | 修改 1 只設定 `m_sla_print`，不影響 `m_fff_print`；修改 2/3 有 SLA 條件守衛 |
| sla_print 物件生命週期 | 低 | 中 | `sla_print` 是 `Plater::priv` 成員，與 Plater 同壽命 |
| PartPlate 切換板時覆蓋 | 低 | 中 | PartPlate 只設定 `m_fff_print`，不會清除 `m_sla_print` |
| update_background_process 其他呼叫路徑 | 低 | 中 | 修改 2 以 `printer_technology` 判斷，涵蓋所有呼叫路徑 |
| Raycaster 同步不一致 | 低 | 中 | 每次增/刪支撐點都完整重建 raycaster 列表；data_changed() 每幀更新 transform |
| on_mouse 事件消費衝突 | 極低 | 低 | 只在 SLA Supports Gizmo 啟用時覆寫，其他 gizmo 不受影響 |

---

## 備註

- 修改 1 解決所有 `sla_print() nullptr` 相關 crash 的根本原因
- 修改 2 解決 PartPlate 在 SLA 模式下持續覆蓋 `m_print` 的問題
- 修改 3 (Fix 6) 與 Fix 1/2/4 屬同類問題：FDM 專屬參數在 SLA config 不存在
- 修改 4 解決支撐點生成後無法從 backend 取回結果的問題
- 修改 5 解決滑鼠事件無法傳遞到 `gizmo_event()` 的問題
- 修改 6 解決支撐點無法被 SceneRaycaster 偵測的問題（hover/drag/delete 的根本原因）
- Fix 5 (GLGizmosCommon.cpp) 的 nullptr 守衛保留作為防禦性程式
- SLA 完整切片流程可能還需要額外修復（SLAPrint::apply、SLA preview 等），但核心 nullptr crash 已解決
- 測試 10（SLA 切片）已發現問題 — 見下方「Test 10 分析」章節

---

## Test 10 分析：按 Slice Plate 後模型消失

**測試日期**: 2026-02-19
**症狀**: 按下 "Slice plate" 後，3D 場景中的模型消失，畫面變為空白

---

### 根本原因

**PhrozenOrca 的 `GUI_Preview.cpp::load_print()` 缺少 `ptSLA` 分支**。

按下 Slice Plate 後的完整流程：

```
on_action_slice_plate()               // Plater.cpp:7435
  │
  ├─ get_current_fff_print()          // ⚠️ SLA 模式卻存取 FFF print
  ├─ reslice()                        // 啟動背景切片
  └─ select_view_3D("Preview")        // 強制切換到 Preview 面板
       │
       └─ set_current_panel(preview)
            │
            └─ preview->reload_print(false)
                 │
                 ├─ m_canvas->reset_volumes()    // ← 清除所有 3D 模型！
                 └─ load_print()                 // GUI_Preview.cpp:325
                      │
                      ├─ if (tech == ptFFF) load_print_as_fff()
                      └─ // ptSLA → 什麼都不做！← 💥 模型消失，畫面空白
```

---

### PhrozenOrca vs PrusaSlicer 流程比較

#### PrusaSlicer 的正確 SLA 切片流程

```
使用者點擊 "Slice now"
  │
  ├─ reslice()                         // Plater.cpp:6405
  │   ├─ 自動標記 NoPoints → Generating（觸發支撐點自動生成）
  │   ├─ update_background_process()   // 啟動背景切片
  │   ├─ preview->reload_print()       // 預先刷新 preview cache
  │   └─ 【不主動切換視圖，留在 3D Editor】
  │
  ├─ 切片過程中：on_slicing_update() 事件
  │   ├─ RELOAD_SCENE → update_sla_scene() → 3D Editor 即時顯示 support/pad
  │   └─ RELOAD_SLA_PREVIEW → preview->reload_print() → 更新 Preview 數據
  │
  ├─ 切片完成：on_slicing_completed() + on_process_completed()
  │   └─ update_sla_scene() → 3D Editor 顯示完整 support + pad 模型
  │
  └─ 使用者手動切換到 Preview tab
       └─ set_current_panel(preview)
            └─ preview->reload_print()
                 └─ load_print()                 // GUI_Preview.cpp:286
                      └─ if (tech == ptSLA) load_print_as_sla()  ← 關鍵！
                           ├─ load_sla_preview() → 載入 object + support + pad mesh
                           ├─ 設定上下 clipping planes（裁切面）
                           └─ 顯示 layer slider，隱藏 moves slider
```

#### PhrozenOrca 問題點對照

| 項目 | PrusaSlicer | PhrozenOrca | 影響 |
|------|-------------|-------------|------|
| **Preview::load_print()** | 有 `ptSLA` 分支 → `load_print_as_sla()` | **只有 `ptFFF` 分支，SLA 無處理** | **模型消失的直接原因** |
| **load_print_as_sla()** | 存在（GUI_Preview.cpp:1021）：載入 SLA shells + clipping planes + layer slider | **不存在** | Preview 無法顯示 SLA 結果 |
| **Slice 按鈕行為** | `reslice()` 不切換視圖，留在 3D Editor | `reslice()` + **強制 `select_view_3D("Preview")`** | 觸發 reset_volumes |
| **切片前 FFF 參數** | 不存取 FFF print | 無條件 `get_current_fff_print()` | SLA 模式存取錯誤物件 |
| **3D Editor 顯示支撐** | `update_sla_scene()` → `reload_scene()` 即時顯示 | 理論上相同，但被 Preview 的 reset_volumes 覆蓋 | 支撐不會顯示 |

---

### 需要修復的問題清單

| 優先級 | 位置 | 問題 | 修復方向 |
|--------|------|------|----------|
| **CRITICAL** | `GUI_Preview.cpp` `load_print()` | 缺少 `ptSLA` 分支 | 加入 `else if (tech == ptSLA) load_print_as_sla();` |
| **CRITICAL** | `GUI_Preview.cpp` | `load_print_as_sla()` 函數不存在 | 從 PrusaSlicer `GUI_Preview.cpp:1021` 移植 |
| **HIGH** | `Plater.cpp` `on_action_slice_plate()` | SLA 模式不應強制切換到 Preview | 加入 `if (printer_technology != ptSLA)` guard |
| **HIGH** | `Plater.cpp` `on_action_slice_plate()` | 無條件呼叫 `get_current_fff_print()` | 加入 SLA guard，SLA 模式跳過 |
| **MEDIUM** | `Plater.cpp` `on_process_completed()` | 切片完成後 `reload_print(false)` 再次清除 | SLA 模式跳過或用 `keep_volumes=true` |
| **MEDIUM** | `Plater.cpp` `select_view_3D("Preview")` | 進入 Preview 前無條件呼叫 FFF extruder/speed 設定 | 加入 SLA guard |

---

### PrusaSlicer 的 load_print_as_sla() 功能摘要

（來源：`PrusaSlicer/src/slic3r/GUI/GUI_Preview.cpp:1021-1057`）

```cpp
void Preview::load_print_as_sla()
{
    // 1. 從 BackgroundSlicingProcess 取得 SLAPrint
    const SLAPrint* print = m_process->sla_print();

    // 2. 從所有 SLAPrintObject 收集切層 Z 值
    std::vector<double> zs;
    for (const SLAPrintObject* obj : print->objects())
        if (obj->is_step_done(slaposSliceSupports) && !obj->get_slice_index().empty())
            // 收集每層的 Z 座標...

    // 3. 設定 clipping planes 模式（上下裁切面）
    m_canvas->set_use_clipping_planes(true);

    // 4. 載入 3D shells（object mesh + support mesh + pad mesh）
    m_canvas->load_sla_preview();

    // 5. 隱藏 moves slider，顯示 layers slider
    m_moves_slider->Hide();
    if (n_layers > 0)
        update_layers_slider(zs);  // 建立 layer slider
}
```

**Layer Slider 互動邏輯**：使用者拖動 layer slider 時，透過調整兩個 clipping planes（上/下裁切面）顯示該層截面，而非 FDM 的 GCode toolpath 過濾。

---

### 修復計畫

**下一步**：建立 `Step2_4_SLAPreview_Result.md`，規劃並執行以上 6 個修復項目。

核心工作：
1. 在 `GUI_Preview.cpp` 加入 `load_print_as_sla()`（從 PrusaSlicer 移植，可能需要 API 適配）
2. 在 `load_print()` 加入 `ptSLA` 分支
3. 在 `on_action_slice_plate()` 加入 SLA guard（避免強制切換視圖 + 避免 FFF print 存取）
4. 驗證 `load_sla_preview()` 在 PhrozenOrca 中是否存在（`GLCanvas3D.cpp`）
