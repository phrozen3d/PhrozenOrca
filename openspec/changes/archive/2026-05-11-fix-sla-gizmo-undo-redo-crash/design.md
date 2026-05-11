## Context

PhrozenOrca 的 undo/redo 系統以 `UndoRedo::Stack` 為核心，透過 cereal binary archive 序列化 Model、Selection、GLGizmosManager、PartPlateList。完整的執行順序如下：

```
【UndoRedo::Stack::load_snapshot()】
  1. Model restored（old ModelObjects deleted, new ones created）
  2. m_selection (stack copy) restored（僅 raw data，canvas Selection 尚未更新）
  3. GLGizmosManager::load(archive)            ← ⚠️ m_c 尚未更新
     ├─ m_serializing = true
     ├─ activate_gizmo(new_current)
     │     old_gizmo.set_state(Off)
     │     new_gizmo.set_state(On)
     │     new_gizmo.register_raycasters_for_picking()  ← crash site 1
     └─ m_gizmos[new_current]->load(ar)
  4. PartPlateList restored

【Plater::priv::update_after_undo_redo()】
  5. selection.clear()
  6. this->update(FORCE_BACKGROUND_PROCESSING_UPDATE)  ← 重建 GLVolumes，可能觸發 repaint
  7. selection.set_deserialized(...)                   ← canvas Selection 在此才真正還原
  8. gizmos_manager.update_after_undo_redo()
     └─ update_data()
          m_c->update(requirements)   ← SelectionInfo 在此才刷新
          data_changed(is_serializing=true)  ← crash site 2
```

**Crash 1：GLGizmoDrill::register_hole_raycasters_for_picking()（步驟 3）**

此函式透過 `m_c->selection_info()->model_object()` 取 ModelObject。`m_c->selection_info()` 的有效性取決於前一個 gizmo 的 requirements：

| 前一個 gizmo | SelectionInfo 狀態 | model_object() |
|---|---|---|
| Move / FDM gizmo（requirements = 0） | `release()` 被呼叫 → `m_is_valid = false` → `selection_info()` 回傳 **nullptr** | 不適用（info 為 null） |
| Hollow / Drill / Support（require SelectionInfo） | `update()` 被呼叫 → `m_is_valid = true`，但 model_object 指標是 undo **前**的舊物件 | **dangling pointer**（舊 ModelObject 在步驟 1 被刪） |

這意味著：
- SLA gizmo → non-SLA gizmo（Move）→ undo：現有 `if (info != nullptr)` 已足夠
- SLA gizmo A → SLA gizmo B → undo：`info` non-null，`model_object()` 是 dangling pointer → **null check 不夠**

因此修法不能只加 `model_object() != nullptr`，必須繞過 `m_c->selection_info()` 整個機制。

**Crash 2：GLGizmoSlaSupports::update_point_raycasters_for_picking_transform()（步驟 8）**

步驟 6 的 `this->update()` 重建 GLVolumes 過程中可能觸發 repaint（步驟 7 的 `set_deserialized()` 尚未執行）。此時 `data_changed()` 被呼叫，`selection.get_first_volume()` 因 canvas Selection 尚未還原而回傳 **null**。GLGizmoDrill 的同名函式在先前的 change 中已加 null guard，SlaSupports 漏掉了。

**Crash 3：GLGizmoHollow render path（on_render / on_render_input_window / get_config_options）**

在 smoke test（多次 gizmo 切換交叉 undo）中，前次 gizmo 關閉所 post 的 EVT_GLCANVAS_FORCE_UPDATE 事件積累在 wx 事件佇列中，在後續 undo 的 `this->update()` 過程中被觸發。此時 Hollow 已是 current gizmo（activate_gizmo 已完成），但 `m_c->update()` 尚未執行，SelectionInfo 持有前一個 SLA gizmo session 的 stale 狀態（`model_object()` 指向已刪除物件的 dangling pointer）。

觸發的 render 函式：
- `on_render()` line 98：`sel_info = m_c->selection_info()` → line 100：`sel_info->model_object()` — 若 sel_info 為 null 則 crash
- `on_render_input_window()` line 213：`m_c->selection_info()->model_object()` — 無 null guard，直接 crash
- `get_config_options()` line 184（由 on_render_input_window 呼叫）：同上，且 `mo->config.get()` 在 mo 為 dangling pointer（非 null，但已釋放）時 crash

**特別說明：原 Non-Goal「不修改 GLGizmoHollow」已被實測推翻。** 原本的假設只涵蓋 data path（update_volumes unregister/register cycle），未涵蓋 render path 中 m_c->selection_info() 的 null/dangling 存取。Drill 和 SlaSupports 的 on_render_input_window() 首行也有完全相同的問題，需一併修正。

## Goals / Non-Goals

**Goals:**
- `GLGizmoDrill::register_hole_raycasters_for_picking()` 安全處理 null 與 dangling 兩種情況，不使用可能 stale 的 `m_c->selection_info()`
- `GLGizmoSlaSupports::update_point_raycasters_for_picking_transform()` 在 `get_first_volume()` 回傳 null 時安全退出
- `GLGizmoHollow::on_render()` / `on_render_input_window()` / `get_config_options()` 在 m_c->selection_info() 為 null 或 stale 時安全退出
- `GLGizmoDrill::on_render_input_window()` 與 `GLGizmoSlaSupports::on_render_input_window()` 加相同的 null guard（首行未 null-check selection_info()）
- 修正後跨 gizmo undo/redo 不 crash，包含 smoke test（連續多次切換）

**Non-Goals:**
- 不修改 `GLGizmosManager::load()` 的執行順序
- 不修改 `update_after_undo_redo()` 的時序
- 不修改 `SelectionInfo` 的生命週期或更新時機
- ~~不修改 GLGizmoHollow~~（原 Non-Goal 已由實測推翻，Hollow 需修正 render path）
- 不修改 FDM gizmo 任何邏輯
- 不改動 UndoRedo::Stack 核心

## Decisions

### 決策 1：register_hole_raycasters_for_picking() 改用 Selection 直接取 ModelObject

**選擇**：完全停止使用 `m_c->selection_info()` 於此函式，改用 `m_parent.get_selection()` 直接取 ModelObject：

```cpp
// 不再使用：
// const CommonGizmosDataObjects::SelectionInfo* info = m_c->selection_info();
// if (info != nullptr && !info->model_object()->sla_drain_holes.empty()) {
//     const sla::DrainHoles& drain_holes = info->model_object()->sla_drain_holes;

// 改為：
const Selection& sel = m_parent.get_selection();
const int obj_idx = sel.get_object_idx();
if (obj_idx < 0 || !sel.get_model() || obj_idx >= (int)sel.get_model()->objects.size())
    return;
const ModelObject* mo = sel.get_model()->objects[obj_idx];
if (mo == nullptr || mo->sla_drain_holes.empty())
    return;
const sla::DrainHoles& drain_holes = mo->sla_drain_holes;
```

**理由**：
- 在 `load_snapshot()` 執行順序中，**Model（步驟 1）比 GLGizmosManager::load()（步驟 3）先還原**。canvas Selection 的 `get_object_idx()` 仍是 undo 前的舊 index，但 `get_model()->objects[idx]` 已指向新還原的 ModelObject（因 Model container 本身未改，只是 objects 被替換）。
- 這與 `SelectionInfo::on_update()` 使用完全相同的取值邏輯：`m_model_object = selection.get_model()->objects[selection.get_object_idx()]`。
- 同時正確處理 null（`mo == nullptr`）和 dangling（整條路徑改用 Selection，不使用舊指標）。
- 若 registration 略過（Selection 無效），`data_changed()` 在 SelectionInfo 刷新後透過 `if (m_hole_raycasters.empty()) register_hole_raycasters_for_picking()` 補回。

**替代方案 A**：加 `info->model_object() != nullptr` null check。
**排除理由**：只能防 null，不能防 dangling pointer（從 SLA gizmo 切換到另一個 SLA gizmo 時的情況）。

**替代方案 B**：比較 `info->model_object()` 與 `selection.get_model()->objects[idx]`，不同則視為 stale。
**排除理由**：增加邏輯複雜度；若兩者剛好相同地址（不同物件但記憶體被重用）仍有風險；直接用 Selection 更乾淨。

---

### 決策 2：在 update_point_raycasters_for_picking_transform() 加 get_first_volume() null guard

**選擇**：在 `GLGizmoSlaSupports::update_point_raycasters_for_picking_transform()` 開頭（現有 early-return 之後）加 null guard，完全對齊 GLGizmoDrill 的既有模式：

```cpp
const GLVolume* vol = selection.get_first_volume();
if (!vol)
    return;  // Selection not ready (e.g. during Undo/Redo); transforms update next frame.
```

**理由**：
- 與 `GLGizmoDrill::update_hole_raycasters_for_picking_transform()` 的 null guard 完全相同，保持兩個 gizmo 間的一致性。
- `get_first_volume()` 在 undo/redo 中 selection 尚未完成還原時（步驟 6-7 之間可能觸發的 repaint）可能回傳 null。
- raycaster transform 略過一幀不影響功能，下一幀 `data_changed()` 會再次呼叫更新。

**替代方案**：在 `data_changed()` 中加 guard，避免進入 `update_point_raycasters_for_picking_transform()`。
**排除理由**：`update_point_raycasters_for_picking_transform()` 有多個呼叫點（register 與 data_changed），在函式內部防護更集中且不遺漏。

---

### 決策 3：GLGizmoHollow / GLGizmoDrill / GLGizmoSlaSupports render path 加 null guard

**選擇**：對三個 gizmo 的 render 函式加統一的 null guard：

**Hollow on_render()（line 97）**：
```cpp
const CommonGizmosDataObjects::SelectionInfo* sel_info = m_c->selection_info();
if (!sel_info)
    return;  // m_c not yet refreshed (e.g. intermediate render during undo)
```

**Hollow get_config_options()（line 183 前）** 與 **Hollow on_render_input_window()（line 212 前）**：
```cpp
if (!m_c->selection_info())
    return (empty or void);
ModelObject* mo = m_c->selection_info()->model_object();
if (!mo)
    return;
// Stale detection: compare against current selection to catch dangling non-null pointer
const Selection& sel = m_parent.get_selection();
const int obj_idx = sel.get_object_idx();
if (obj_idx >= 0 && sel.get_model() && obj_idx < (int)sel.get_model()->objects.size()
    && mo != sel.get_model()->objects[obj_idx])
    return;  // stale SelectionInfo, skip this frame
```

**Drill on_render_input_window()（line 776）** 與 **SlaSupports on_render_input_window()（line 694）**：加相同的 null guard on selection_info()。

**理由**：
- `on_render_input_window()` 是單獨呼叫的 render 函式（`render_input_window()` 在 GLGizmosManager 中獨立呼叫，line 1290/1299），`on_render()` 的 early return 不會阻止它執行。
- 在 smoke test（多次 gizmo 切換）中，積累的 wx 事件可能使 render 在 m_c->update() 之前被觸發。
- 對 null 和 dangling 兩種狀態提供防護。Stale detection（和 on_render() 現有的 stale check 相同模式）確保 dangling non-null pointer 也被捕捉。

**替代方案**：在 GLGizmosManager 的 render 路徑中加 m_c 有效性檢查。
**排除理由**：每個 gizmo 的 m_c 有效性定義不同（依 requirements 而定），統一在 Manager 層做反而更複雜。各 gizmo 自己防護更清晰。

## Risks / Trade-offs

**[Risk 1] register_hole_raycasters_for_picking() 改用 Selection 後行為差異**
→ 正常情況下，`selection.get_object_idx()` 與 SelectionInfo 的 `model_object()` 指向相同物件（SelectionInfo::on_update() 用相同邏輯取得）。改動後行為等價。
→ Mitigation：程式碼中加注解說明為何跳過 m_c，方便未來維護者理解。

**[Risk 2] registration 略過後 data_changed() 是否一定補回**
→ `data_changed()` 中的 `if (m_hole_raycasters.empty()) register_hole_raycasters_for_picking()` 確保略過後仍會補 registration。
→ Mitigation：data_changed() 在 update_after_undo_redo() → update_data() 中被呼叫，時序保證在 SelectionInfo 刷新後（步驟 8），此時 `register_hole_raycasters_for_picking()` 再次呼叫時 m_c 已是新資料，走正常路徑。

**[Risk 3] SlaSupports raycaster 可能有一幀 transform 未更新**
→ null guard 觸發時，raycaster transform 保持上一幀值，這幀的 picking 可能略有不準確。
→ Mitigation：下一幀 data_changed() 會重新呼叫更新；undo/redo 後通常不會在同一幀立即 picking。

**[Risk 4] SlaSupports m_editing_cache / m_point_raycasters size 不同步（已知次要問題）**
→ undo 後 on_load() 還原 m_editing_cache，可能與現有 m_point_raycasters 數量不符。現有 loop bound 防止越界。
→ 不在本次範圍，null guard 修正後不會因此 crash，僅 picking 精度可能略有異常。

## Open Questions

（無待解決問題，修正方案已確定）