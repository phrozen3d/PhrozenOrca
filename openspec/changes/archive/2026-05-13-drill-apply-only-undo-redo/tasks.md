## 1. Header — 成員變數調整（GLGizmoDrill.hpp）

- [x] 1.1 新增 `sla::DrainHoles m_working_holes;` 成員（pending session 工作集）
- [x] 1.2 確認 `m_radius_before_change`、`m_height_before_change`、`m_holes_before_change` 是否只服務於舊 TakeSnapshot/BrimEars snapshot pattern（grep 呼叫點）；若是，移除這三個成員；若仍用於暫存/dirty 狀態/互動同步，保留但在 task 8 中重構為只操作 `m_working_holes`
- [x] 1.3 確認 `begin_size_change` 與 `apply_size_change` 是否只服務於舊 TakeSnapshot pattern（grep 呼叫點）；若是，移除宣告；若仍有其他用途，保留宣告並在 task 8 中重構
- [x] 1.4 移除 `sla::DrainHoles m_holes_stash;` 成員
- [x] 1.5 移除 `bool m_stash_initialized = false;` 成員

## 2. data_changed() 改寫（GLGizmoDrill.cpp:50）

- [x] 2.1 `m_old_mo_id != mo->id()` 分支：移除舊 object 的 `old_obj->sla_drain_holes = m_holes_stash` restore 邏輯；改為 `m_working_holes = mo->sla_drain_holes;`；保留 `reload_cache(); m_old_mo_id = mo->id();`
- [x] 2.2 `is_serializing` 分支：移除 `m_stash_initialized` guard；直接執行 `m_working_holes = mo->sla_drain_holes; reload_cache(); unregister_hole_raycasters_for_picking();`

## 3. on_set_state() 改寫（GLGizmoDrill.cpp:851）

- [x] 3.1 `On` 分支：移除 `m_stash_initialized = false;`（成員已刪除）
- [x] 3.2 `Off` 分支：移除整個 exit-restore block（`mo_restore->sla_drain_holes = m_holes_stash; m_stash_initialized = false;`）；改為 `m_working_holes.clear();`

## 4. Add hole 路徑（GLGizmoDrill.cpp:250 gizmo_event LeftDown）

- [x] 4.1 移除 `Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Add drainage hole");`
- [x] 4.2 將 `mo->sla_drain_holes.emplace_back(...)` 改為 `m_working_holes.emplace_back(...)`
- [x] 4.3 修正 assert：`m_selected.size() == m_working_holes.size()`

## 5. Delete hole 路徑（GLGizmoDrill.cpp:358 delete_selected_points()）

- [x] 5.1 移除 `Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Delete drainage hole");`
- [x] 5.2 將 `sla::DrainHoles& drain_holes = m_c->selection_info()->model_object()->sla_drain_holes;` 改為 `sla::DrainHoles& drain_holes = m_working_holes;`

## 6. Drag 路徑（on_start_dragging / on_stop_dragging / on_dragging）

- [x] 6.1 `on_start_dragging`（cpp:896）：`m_hole_before_drag` 與 `m_hole_normal_before_drag` 改從 `m_working_holes[m_hover_id]` 讀取
- [x] 6.2 `on_stop_dragging`（cpp:908）：`sla::DrainHoles& drain_holes` 改為 `m_working_holes`；移除 `Plater::TakeSnapshot snapshot(..., "Move drainage hole")`
- [x] 6.3 `on_dragging`（cpp:934）：`sla::DrainHoles &drain_holes` 改為 `m_working_holes`

## 7. Render / Raycaster 路徑

- [x] 7.1 `render_points`（cpp:166）：`const sla::DrainHoles& drain_holes` 改讀 `m_working_holes`（移除 `m_c->selection_info()->model_object()->sla_drain_holes` 引用）
- [x] 7.2 `register_hole_raycasters_for_picking`（cpp:442）：`mo->sla_drain_holes.empty()` 改為 `m_working_holes.empty()`；`const sla::DrainHoles& drain_holes = mo->sla_drain_holes` 改為 `const sla::DrainHoles& drain_holes = m_working_holes`（保留 Selection-based `mo` 指標用於 raycaster 數量 guard，但洞資料改讀 `m_working_holes`）
- [x] 7.3 `update_hole_raycasters_for_picking_transform`（cpp:463）：`info->model_object()->sla_drain_holes` 改為 `m_working_holes`
- [x] 7.4 gizmo_event selection rectangle（cpp:276）：`mo->sla_drain_holes.size()` 與 `mo->sla_drain_holes[i].pos` 改為 `m_working_holes`

## 8. Size change 路徑（render_new_drill_panel diameter/depth slider + InputFloat）

> **前置判斷**（承接 task 1.2/1.3 結論）：
> - 若 `begin_size_change` / `apply_size_change` 確認只服務於 TakeSnapshot pattern → 各步驟移除這兩個呼叫。
> - 若它們仍具暫存/dirty/rollback 用途 → 各步驟保留呼叫但重構實作：只操作 `m_working_holes`，不呼叫 `TakeSnapshot`，不寫 `mo->sla_drain_holes`。
> 以下各步驟以「移除呼叫」為預設描述，若前置判斷結論為「保留重構」請改為重構。

- [x] 8.1 Diameter slider（cpp:637–642）：`mo->sla_drain_holes[idx].radius` 改為 `m_working_holes[idx].radius`；依前置判斷移除或重構 `begin_size_change(...)` 呼叫
- [x] 8.2 Diameter slider `IsItemDeactivated`（cpp:644）：依前置判斷移除或重構 `apply_size_change("Change hole radius")` 呼叫
- [x] 8.3 Diameter InputFloat `IsItemActivated`（cpp:649）：依前置判斷移除或重構 `begin_size_change(...)` 呼叫
- [x] 8.4 Diameter InputFloat `IsItemDeactivatedAfterEdit`（cpp:653–659）：`mo->sla_drain_holes[idx].radius` 改為 `m_working_holes[idx].radius`；依前置判斷移除或重構 `apply_size_change(...)` 呼叫
- [x] 8.5 Depth slider（cpp:675–680）：`mo->sla_drain_holes[idx].height` 改為 `m_working_holes[idx].height`；依前置判斷移除或重構 `begin_size_change(...)` 呼叫
- [x] 8.6 Depth slider `IsItemDeactivated`（cpp:683）：依前置判斷移除或重構 `apply_size_change("Change hole depth")` 呼叫
- [x] 8.7 Depth InputFloat `IsItemActivated`（cpp:688）：依前置判斷移除或重構 `begin_size_change(...)` 呼叫
- [x] 8.8 Depth InputFloat `IsItemDeactivatedAfterEdit`（cpp:692–697）：`mo->sla_drain_holes[idx].height` 改為 `m_working_holes[idx].height`；依前置判斷移除或重構 `apply_size_change(...)` 呼叫

## 9. Apply button（render_new_drill_panel cpp:759–762）

- [x] 9.1 替換 Apply handler：移除 `m_holes_stash = mo->sla_drain_holes;`；改為 `Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Apply drain holes"); mo->sla_drain_holes = m_working_holes;`；保留 `reslice_until_step(slaposDrillHoles);`
- [x] 9.2 修正 "remove all" guard（cpp:753）：`!mo->sla_drain_holes.empty()` 改為 `!m_working_holes.empty()`

## 10. 清理廢棄程式碼

- [x] 10.1 依 task 1.3 結論：若確認 `begin_size_change` 只服務於 TakeSnapshot pattern，移除實作（cpp:960–967）；否則重構實作為只操作 `m_working_holes`，移除 `TakeSnapshot` 與 `mo->sla_drain_holes` 寫入
- [x] 10.2 依 task 1.3 結論：若確認 `apply_size_change` 只服務於 TakeSnapshot pattern，移除實作（cpp:969–998）；否則重構實作為只操作 `m_working_holes`，移除 `TakeSnapshot` 與 `mo->sla_drain_holes` 寫入
- [x] 10.3 `select_point()`（cpp:1003）：`const sla::DrainHoles& drain_holes = m_c->selection_info()->model_object()->sla_drain_holes;` 改為 `const sla::DrainHoles& drain_holes = m_working_holes;`
- [x] 10.4 `reload_cache()`（cpp:1040）：`m_c->selection_info()->model_object()->sla_drain_holes.size()` 改為 `m_working_holes.size()`
- [x] 10.5 `on_set_hover_id()`（cpp:1049）：`m_c->selection_info()->model_object()->sla_drain_holes.size()` 改為 `m_working_holes.size()`（此函式也可考慮移除 `model_object() == nullptr` guard，因 m_working_holes 不依賴 m_c）

## 11. Scope 確認（實作前檢查）

- [x] 11.0 grep `m_last_resliced_holes` 於整個 repo；若發現此名稱的殘留，先回報，不自行沿用或重構。本 change 不引入此成員。

## 12. 手動驗證

- [x] 12.1 新增孔 → 未 Apply → 切換工具 → 確認孔未出現在模型
- [x] 12.2 新增孔 → Apply → 確認 undo stack 有 "Apply drain holes" → Ctrl+Z 還原 → 孔消失
- [x] 12.3 Apply 兩次 → Ctrl+Z 兩次 → 兩次都正確還原
- [x] 12.4 拖曳孔、改 size → Apply → Ctrl+Z → 確認回到 Apply 前狀態（非逐步 undo）
- [x] 12.5 Undo 後 m_working_holes 已同步（render 顯示正確孔數）
- [x] 12.6 切換物件後 m_working_holes 以新物件 sla_drain_holes 初始化
- [x] 12.7 連續多次 Ctrl+Z 在 Drill gizmo 內不 crash
