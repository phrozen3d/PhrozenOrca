## Context

Hollow 功能的 UI 入口在 `GLGizmoHollow::render_hollow_panel()`（`GLGizmoHollow.cpp`）。舊的 UI 結構為：

```
Row 1: "Hollow object when slicing" label + BBLCheckbox("##hp_enable")
         → on change: mo->config.set("hollowing_enable", val)
                      m_enable_hollowing = val
Rows 2–4: Wall thickness / Quality / Closing distance slider + InputFloat
         [disabled when hollow_active = is_input_enabled() && m_enable_hollowing is false]
Button row: [?] | Preview
         → on click: reslice_until_step(slaposDrillHoles)
```

Hollow 狀態資料流：
- `m_enable_hollowing`：每幀從 `mo->config.get("hollowing_enable")` 重讀的 frame-local cache
- `mo->config["hollowing_enable"]`：持久化狀態，slicing pipeline 在此讀取
- Preview 與 slicing 讀取同一份 config（無分離）

## Goals / Non-Goals

**Goals:**
- 使用者可透過單一按鈕完成「設定 hollow 狀態 + 預覽」
- 移除需要手動兩步操作的 checkbox + Preview 模式
- 參數 slider/input 在未 hollow 狀態下仍可編輯，使用者可先調好參數再按 Hollow
- 每個物件的 hollow 狀態與參數繼續獨立，切換物件時 UI 反映各自狀態

**Non-Goals:**
- 不改 hollow/SLA slicing pipeline、演算法或資料結構
- 不改 `hollowing_enable` config 的讀取路徑（slicing 行為不變）
- 不新增全域 hollow 狀態
- 不改 drill holes 邏輯（`slaposDrillHoles` step 仍包含 hollow + drill）

## Decisions

### D1：Checkbox 採用「移除 render」而非「隱藏」

**決策**：移除 checkbox 的渲染代碼（整個 Row 1 block 不 render），不保留隱藏狀態。

**放棄的替代方案**：ImGui 沒有 show/hide 切換的概念；「隱藏」在 ImGui 中等同「不呼叫該 widget 的渲染代碼」，無法比移除更乾淨。

**採用方案**：直接移除 Row 1 整個 block 的代碼。底層 `mo->config["hollowing_enable"]` config 邏輯完全保留，slicing 仍讀取此值，不受影響。

### D2：Preview 按鈕採用「移除 render」

**決策**：同 D1，直接不 render Preview 按鈕的代碼。Preview 功能已移入兩個行為按鈕的 handler 中。

**理由**：保留 Preview 按鈕會讓使用者困惑「何時需要按 Preview、何時按 Hollow 就夠了」；由兩個行為按鈕各自負責 preview 觸發是最清晰的分工。

### D3：Hollow / Remove 按鈕的 disable 條件

- `Hollow` 按鈕：`!is_input_enabled()` 時 disabled
  - 不依賴 hollow 狀態，使用者隨時可重新觸發（等同重按 Preview 的語意）
- `Remove` 按鈕：`!is_input_enabled() || !m_enable_hollowing` 時 disabled
  - 未 hollow 時無可移除的狀態，應 greyed out

**重要**：兩個按鈕不放在 `disabled_begin(!hollow_active)` 的範圍內，避免 Remove 按鈕在 hollow=true 時因 `hollow_active` 條件誤判而被 disable 自身。

### D4：`hollow_active` 條件調整讓 slider 永遠可用

**決策**：將 `hollow_active` 從 `is_input_enabled() && m_enable_hollowing` 改為僅 `is_input_enabled()`。

**效果**：三個 slider/input 在未 hollow 時仍可拖曳調整，但調整值只更新 UI 暫存（pending）成員，不立刻寫入 `mo->config`（見 D6）。使用者可先設好參數，再按 Hollow 一次完成。

### D5：`m_enable_hollowing` 的角色不變

`m_enable_hollowing` 仍只是每幀從 `mo->config.get("hollowing_enable")` 重讀的 frame-local cache，不升格為唯一資料來源。Hollow/Remove 按鈕 handler 同時寫入 `mo->config` 和更新 `m_enable_hollowing`，與原 checkbox handler 邏輯完全一致。

### D6：Hollow 參數採用 pending-apply 模式

**背景**：Phase 4 測試發現，在未按 Hollow 的情況下調整參數 slider，會立刻修改 `mo->config`，導致 preview 狀態被重置。

**決策**：新增三個 pending 成員（`m_pending_offset`、`m_pending_quality`、`m_pending_closing_d`）作為 UI session 中的暫存輸入，並新增 `m_pending_owner`（`ModelObject*`）追蹤 pending 值所屬的物件。

**初始化**：`on_render_input_window()` 每幀比對 `mo != m_pending_owner`；不相等時（物件切換）從 `mo->config` 重讀三個 pending 值並更新 `m_pending_owner`。Gizmo 關閉時（`on_set_state()` 偵測 Off 狀態）設 `m_pending_owner = nullptr`，確保下次進入時一定重讀。

**寫入時機**：只有按下 Hollow 按鈕時，才把 `m_pending_*` 值寫入 `mo->config`（連同 `hollowing_enable=true`），再觸發 `reslice_until_step(slaposDrillHoles)`。

**丟棄時機**：離開 Hollow gizmo（任何方式：切換工具、切換物件、關閉 gizmo）時 pending 值不寫入 config，下次進入時從 config 重新初始化。

**Remove 按鈕不套用 pending**：只設 `hollowing_enable=false` 並 reslice，不寫入 pending 參數，保留物件 config 中上次 Hollow 時的參數值。

## Risks / Trade-offs

- **`Hollow` 按鈕允許在已 hollow 時重複按**：允許重複觸發 `reslice_until_step(slaposDrillHoles)`，與原 Preview 按鈕行為一致（也允許重複按）。接受此行為，未來若需防重複可加 disable 條件。
- **Drill holes 不受影響**：`reslice_until_step(slaposDrillHoles)` 涵蓋 hollow + drill 整個 step；取消 hollow 後，drill 資料仍在 `mo->sla_drain_holes`，slicing 仍使用。

## Open Questions

- 是否需要在 panel 頂部顯示目前物件的 hollow 狀態指示文字（已 hollow / 未 hollow）？目前實作：Remove 按鈕的 enabled/disabled 狀態已隱含此資訊，未另加文字。
