## Context

`GLGizmoDrill` 目前的架構：每個 Add/Delete/Move/Size 操作都立即寫入 `mo->sla_drain_holes` 並呼叫 `TakeSnapshot`，Apply 只更新 `m_holes_stash` baseline 而不建立 snapshot，離開 gizmo 時以 `m_holes_stash` 還原 `mo->sla_drain_holes`。

問題：
1. Undo stack 被細粒度操作填滿（每個 Add、Delete、drag 都是一個節點）。
2. `mo->sla_drain_holes` 在未 Apply 期間存放著「work in progress」資料，slicing pipeline 若被觸發會讀到 pending 狀態。
3. `m_holes_stash` + exit-restore 邏輯複雜，且在 Undo/Redo 後需要額外同步（`data_changed` 的 `is_serializing` 分支）。

目標架構：session 內的全部編輯只存在 `m_working_holes`（記憶體中），`mo->sla_drain_holes` 僅在 Apply 時被寫入，Apply 是唯一的 undo 節點。

## Goals / Non-Goals

**Goals:**
- `m_working_holes` 作為 session pending 工作集，Add/Delete/Move/Size 只修改它
- Apply 建立唯一 undo snapshot：`TakeSnapshot("Apply drain holes")` → 寫 `mo->sla_drain_holes` → reslice
- Undo/Redo 後 `data_changed(is_serializing=true)` 以 cereal 還原的 `mo->sla_drain_holes` 重建 `m_working_holes`，並重新 reload cache 與 raycasters
- 離開 Drill 時直接丟棄 `m_working_holes`，不恢復 `mo->sla_drain_holes`
- render / raycaster / drag / selection 全部改讀 `m_working_holes`

**Non-Goals:**
- 不實作 session-level local undo（Ctrl+Z 在 session 內還原單步操作）
- 不實作 `has_pending_changes()` / `discard_pending_changes()`
- 不攔截 Ctrl+Z 做 pending discard
- 不修改 GLGizmosManager.cpp
- 不修改 ModelObject 欄位、cereal 格式、3MF 格式、SLA slicing pipeline
- 不引入 `m_last_resliced_holes`：此成員目前不存在於程式碼中，本 change 不新增它。Undo/Redo 後 preview refresh 精細優化不納入本 change 範圍。若日後在程式碼中發現此名稱的殘留，應先回報，不自行沿用。

## Decisions

### D1：`m_working_holes` 取代 `mo->sla_drain_holes` 作為 session 讀寫來源

**選擇**：新增 `sla::DrainHoles m_working_holes` 成員；所有 session 內的讀寫路徑改用它。

**理由**：`mo->sla_drain_holes` 是模型資料，應反映「已提交」狀態，不應持有 in-progress 資料。將 working set 分離到 gizmo 成員可消除 exit-restore 邏輯，且讓 slicing pipeline 永遠只看到 Apply 後的乾淨資料。

**替代方案考慮**：繼續用 `mo->sla_drain_holes` + 更精細的 snapshot grouping → 需修改 undo stack 語意，複雜度高且跨模組；不採用。

---

### D2：Apply 作為唯一 undo 節點

**選擇**：Apply 流程：
```
Plater::TakeSnapshot(wxGetApp().plater(), "Apply drain holes");
mo->sla_drain_holes = m_working_holes;
reslice_until_step(slaposDrillHoles);
```

**理由**：使用者的意圖是「確認這批孔」，Apply 是明確的提交動作，對應到 undo 的粒度是自然的。細粒度操作（Add/Delete/Move）是草稿行為，不應進 undo stack。

**舊行為對比**：舊架構 Apply 不建立 snapshot（只更新 baseline），undo 只能還原到個別 Add/Delete/Move 節點；新架構 undo 直接跳到上次 Apply 前的狀態。

---

### D3：`data_changed(is_serializing=true)` 重建 `m_working_holes`

**選擇**：在 `data_changed` 的 `is_serializing && same_object` 分支：
```cpp
m_working_holes = mo->sla_drain_holes;  // cereal 已還原
reload_cache();
unregister_hole_raycasters_for_picking();
```

**理由**：Undo/Redo 後，cereal 已將 `mo->sla_drain_holes` 還原至 snapshot 時的狀態；`m_working_holes` 必須重新同步才能讓 render/raycaster 反映正確狀態。

---

### D4：移除 exit-restore；`on_set_state(Off)` 只清除狀態

**選擇**：`on_set_state(Off)` 不再執行 `mo->sla_drain_holes = m_holes_stash`。只執行：
```cpp
m_working_holes.clear();
m_stash_initialized = false;   // 若保留成員
m_old_mo_id = ObjectID{};
post_event / set_hide_full_scene
```

**理由**：因 session 內從不寫入 `mo->sla_drain_holes`，離開時不需還原。`mo->sla_drain_holes` 始終反映最後一次 Apply 的結果，或進入 Drill 前的原始狀態。

---

### D5：`on_set_state(On)` 與 `data_changed`（新物件）初始化 `m_working_holes`

**選擇**：切換至新物件或 gizmo 啟動時（`m_old_mo_id != mo->id()`）：
```cpp
m_working_holes = mo->sla_drain_holes;  // 以當前已提交狀態初始化
reload_cache();
m_old_mo_id = mo->id();
```
對應地，`on_set_state(On)` 重置 `m_old_mo_id = ObjectID{}`（或直接依賴 `data_changed` 的 object-switch 分支）。

**理由**：`m_working_holes` 應以目前已提交狀態為起點，讓使用者看到和 Apply 後一致的孔列表。

## Risks / Trade-offs

- **[UX 降級]** 使用者無法 undo 單步 Add/Delete/Move/Size；只能 undo 整批 Apply。→ 此為設計意圖，屬 scope 內 trade-off，接受。

- **[Silent discard on exit]** session 內未 Apply 的孔，切換工具或物件時全部丟棄，無提示。→ 接受（明確排除 discard confirmation）；未來可補 has_pending_changes() 提示但不在本次 scope。

- **[全路徑改寫]** `render`、`raycaster`、`drag`、`on_stop_dragging`、`begin_size_change`、`apply_size_change`、Add、Delete 等約 15 處 `mo->sla_drain_holes` 引用需改讀 `m_working_holes`。→ 逐一 grep 替換；遺漏會導致視覺與資料不一致，需測試覆蓋。

- **[`begin_size_change` / `apply_size_change` 處置]** 這兩個方法及相關成員（`m_radius_before_change`、`m_height_before_change`、`m_holes_before_change`）需先確認其用途後再決定刪除或重構：
  - 若它們**只**服務於舊的 TakeSnapshot/BrimEars snapshot pattern（restore-old → TakeSnapshot → re-apply-new），則可安全移除。
  - 若它們仍被 size slider/InputFloat 用於暫存、dirty 狀態追蹤、互動同步或 rollback，則保留但重構為只操作 `m_working_holes`，不呼叫 `TakeSnapshot`，不寫 `mo->sla_drain_holes`。
  - 初步分析：`begin_size_change` 的唯一目的是儲存 pre-change 狀態供 `TakeSnapshot` 使用；`apply_size_change` 的唯一目的是 restore-old → TakeSnapshot → re-apply。兩者均不服務於 dirty flag 或互動 rollback。但實作前應 grep 確認無其他呼叫點。

## Open Questions

- `m_holes_stash` / `m_stash_initialized` 是否完全移除？建議移除以減少混淆，但需確認無其他路徑引用。
- `register_hole_raycasters_for_picking()` 目前讀 `mo->sla_drain_holes`（見 `drill-raycaster-registration-safety` spec）；改讀 `m_working_holes` 後，`data_changed` 的 `m_hole_raycasters.empty()` 觸發路徑邏輯是否保持不變需確認。