## Phase 1：建立 OpenSpec change 文件

- [x] 1.1 建立 `openspec/changes/improve-hollow-action-buttons/` 目錄及文件（proposal / design / tasks）
- [x] 1.2 建立 `openspec/specs/hollow-action-buttons/spec.md`
- [x] 1.3 建立 `openspec/changes/improve-hollow-action-buttons/specs/hollow-action-buttons/spec.md`

**驗收**：OpenSpec 文件結構完整，與現有 change 格式一致。

---

## Phase 2：UI 入口調整

- [x] 2.1 移除 `render_hollow_panel()` 中的 Row 1 checkbox 整個 block
  - 包含 `m_imgui->text(_(L("Hollow object when slicing")))` 標籤
  - 包含 `ImGui::BBLCheckbox("##hp_enable", &local_enable)` 及其顏色 push/pop
- [x] 2.2 移除 `render_hollow_panel()` 中的 Preview 按鈕 render（`ImGui::Button("Preview##hp", ...)`）
- [x] 2.3 更新 `on_init()` 中的 `m_desc`：移除 `hp_preview`，新增 `hp_hollow` / `hp_remove`
- [x] 2.4 移除 `row_w` 變數（僅用於 checkbox 定位，移除 checkbox 後不再需要）
- [x] 2.5 更新 `btn_w` 計算，改為取 `hp_hollow` / `hp_remove` 兩者文字寬度的最大值

**驗收**：Hollow gizmo panel 中不再顯示 "Hollow object when slicing" checkbox；不再顯示 Preview 按鈕。

---

## Phase 3：Hollow / Remove 按鈕新增與接線

- [x] 3.1 在 button row 中，`[?]` 圖示之後，新增 `Hollow` 按鈕（`##hp_on`）：
  - disabled 條件：`!is_input_enabled()`
  - handler：`mo->config.set("hollowing_enable", true)` → `m_enable_hollowing = true` → `update_and_show_object_settings_item()` → `config_changed = true` → `reslice_until_step(slaposDrillHoles)`
- [x] 3.2 緊接 `Hollow` 按鈕後，新增 `Remove` 按鈕（`##hp_off`）：
  - disabled 條件：`!is_input_enabled() || !m_enable_hollowing`
  - handler：`mo->config.set("hollowing_enable", false)` → `m_enable_hollowing = false` → `update_and_show_object_settings_item()` → `config_changed = true` → `reslice_until_step(slaposDrillHoles)`
- [x] 3.3 確認兩個新按鈕均使用各自獨立的 `disabled_begin` / `disabled_end` block，不放在 `disabled_begin(!hollow_active)` 範圍內

**驗收**：
- 按 Hollow → preview 立即更新為 hollow 結果；Remove 按鈕變 enabled
- 按 Remove → preview 立即更新為原始 mesh；Remove 按鈕變 disabled
- 未 hollow 時 Remove 按鈕呈 greyed out

---

## Phase 4：參數 slider 可編輯性調整

- [x] 4.1 將 `hollow_active` 定義從 `is_input_enabled() && m_enable_hollowing` 改為僅 `is_input_enabled()`
- [x] 4.2 ~~確認三個 slider 在未 hollow 時仍可拖曳並寫入 `mo->config`~~ → **Phase 4 Follow-up（見 4.3-4.6）**

### Phase 4 Follow-up：Hollow 參數 pending-apply 修正

測試發現調整 slider 立刻修改 `mo->config`，導致 preview 狀態被重置。改為 pending-apply 模式：

- [x] 4.3 在 `.hpp` 新增 `m_pending_offset`、`m_pending_quality`、`m_pending_closing_d`（`float`）和 `m_pending_owner`（`ModelObject*`）成員；移除舊的 `m_offset_stash`、`m_quality_stash`、`m_closing_d_stash`
- [x] 4.4 在 `on_render_input_window()` 加入物件切換偵測：`mo != m_pending_owner` 時從 `mo->config` 重讀三個 pending 值並更新 `m_pending_owner`
- [x] 4.5 在 `on_set_state()` Off 分支加入 `m_pending_owner = nullptr`，確保 gizmo 重新進入時一定重讀 config
- [x] 4.6 在 `render_hollow_panel()` 中：
  - 將 `float offset/quality/closing_d` 改為指向 pending 成員的 `float&` reference
  - 移除整個「Aggregate slider interaction events / Stash / config write / InputFloat commit」區塊（原本每次 slider 互動就寫入 `mo->config` 的邏輯）
  - Hollow 按鈕 handler 改為：先把三個 pending 值寫入 `mo->config` → 再設 `hollowing_enable=true` → `TakeSnapshot` → `reslice`
  - Remove 按鈕 handler 不套用 pending 參數（只設 `hollowing_enable=false` + reslice）

**驗收**：
- 未 hollow 時拖曳 slider → preview 不應改變（pending 值只在記憶體中，不觸發 reslice）
- 調整 slider 後離開 Hollow gizmo（切換工具或關閉）→ 再進入時 UI 顯示物件 config 中的原始值，不保留上次未套用的調整
- 調整 slider 後切換到另一物件 → 回到原物件時 UI 顯示物件 config 值，不保留未套用的調整
- 調整 slider 後按 Hollow → preview 使用新參數值
- 已 hollow 時調整 slider → preview 不立刻改變；再按 Hollow → 套用新參數重建 preview
- Remove 按鈕不套用 pending 參數；slider 顯示值維持 pending 狀態（不被 config 覆蓋）

---

## Phase 5：多物件與 slicing 驗收

- [x] 5.1 多物件切換：
  - A 物件按 Hollow → 切換至 B 物件 → B 的 Remove 按鈕為 disabled（B 未 hollow）
  - 切換回 A → A 的 Remove 按鈕仍為 enabled（A 仍 hollow）
- [x] 5.2 Drill holes 不受影響：
  - 物件有 drill holes 時，按 Hollow → preview 顯示 hollow+drill 結果
  - 按 Remove → preview 顯示去除 hollow 但 drill 資料仍在（`mo->sla_drain_holes` 不清空）
- [x] 5.3 Slicing 驗證：
  - 按 Hollow 後執行切片 → slicing 輸出包含 hollow 結果
  - 按 Remove 後執行切片 → slicing 輸出使用原始（未 hollow）mesh
- [x] 5.4 確認 `mo->config["hollowing_enable"]` 在各物件上獨立：
  - 對 A 按 Hollow，對 B 按 Remove，兩者 config 不互相污染

**驗收**：A hollow、B 未 hollow，切換時 UI 狀態不互相污染；slicing 輸出符合各物件的 hollow 狀態。
