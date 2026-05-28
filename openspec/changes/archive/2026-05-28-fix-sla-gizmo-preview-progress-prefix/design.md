## 背景與情境

`Plater::priv::on_slicing_update()`（`Plater.cpp`）是所有 SLA 切片進度事件（`EVT_SLICING_UPDATE`）的唯一處理器。它目前在將每個 status text 轉發給 `NotificationManager::set_slicing_progress_percentage()` 之前，無條件在所有文字前加上 `_u8L("Slicing")` 前綴（無 separator）。

同一條 `EVT_SLICING_UPDATE` 事件串流由以下兩個路徑產生：
- **完整切片** — 由 `on_action_slice_all()` → `background_process.set_task(PrintBase::TaskParams())` → `SLAPrint::process()` 觸發。
- **Gizmo preview partial reslice** — 由三個 gizmo 的 Apply/preview 動作 → `GLGizmoSlaBase::reslice_until_step()` → `Plater::reslice_SLA_until_step()` → `SLAPrint::process()` 觸發。

兩個路徑的結構性差異：

| | 完整切片 | Gizmo Preview |
|---|---|---|
| `TaskParams::single_model_object` | invalid（預設值） | valid（目標物件 ID） |
| 進入點 | `on_action_slice_all()` | `reslice_SLA_until_step()` |
| 執行步驟 | 全部（含 `slapsRasterize`） | 至特定 `SLAPrintObjectStep` 為止 |

`on_slicing_update()` 在事件處理時無法直接存取 `TaskParams`。

### 實作過程中發現的問題

**問題一：Gizmo preview 完成後顯示「Slice complete」**  
`SLAPrint::process()` 在最後一行無條件發送 `m_report_status(*this, 100, L("Slicing done"))` 事件。`on_slicing_update()` 收到 `percent == 100` 後呼叫 `set_slicing_progress_percentage(..., 1.0f)`，使 `SlicingProgressNotification` 進入 `SP_COMPLETED` 狀態。在此狀態下，`set_status_text()` 硬寫 `_u8L("Slice ok.")`（英文 locale 翻譯為 "Slice complete"），完全忽略傳入文字。這導致每次 gizmo preview 結束後都出現誤導性的「Slice complete」通知。

**問題二：Support gizmo preview 起始瞬間顯示前綴（疑似）**  
Support gizmo preview 一開始可能短暫出現「Slicing: 」前綴，疑因 `GLGizmoSlaBase::reslice_until_step()` 使用 `wxGetApp().CallAfter(...)` 推遲 `Plater::reslice_SLA_until_step()` 的呼叫，若第一個 status event 在 flag 設定前到達。**需 diagnostic 確認。**

**問題三（已決策）：完整切片前綴格式**  
原始設計為無 separator 的 `_u8L("Slicing") + text`，導致「SlicingHollowing model」或「SlicingSlicing model」等連字結果。已決策改為使用冒號格式 `_u8L("Slicing") + ": " + text`，讓前綴與 phase label 形成清楚的層級語意。「Slicing: Slicing model」為此格式下合法的預期結果，不需特殊處理。

**問題四：Gizmo preview 取消後顯示「Slicing Canceled」**  
`on_process_completed()` 在取消路徑（`evt.cancelled()`）硬寫 `_u8L("Slicing Canceled")` 傳入 `set_slicing_progress_canceled()`。`SP_CANCELLED` 狀態下的 `set_status_text()` 直接使用傳入文字顯示（不像 `SP_COMPLETED` 硬寫 "Slice ok."）。因此取消文字選擇邏輯需在 `on_process_completed()` 呼叫端處理。

**問題五：Support preview 中 "Slicing model" phase label 的顯示確認**  
Support gizmo 執行至 `slaposSupportPoints` 時，切片管線仍會先通過 `slaposObjectSlice`（phase label："Slicing model"）。在 preview 模式下（`m_sla_gizmo_preview_type == Support`）此 label 不加前綴，直接顯示 "Slicing model"。此為合法的原始 phase label，不視為錯誤。diagnostic log 用於確認此 event 到達時 type 已正確設定（非 None）。

## 目標與非目標

**目標：**
- 在 `on_slicing_update()` 中，gizmo preview partial reslice 執行中抑制「Slicing: 」前綴。
- 對所有真正的完整切片執行，使用 `"Slicing: " + phase label` 格式（包括 "Slicing: Slicing model"）。
- Gizmo preview 成功完成後，依觸發功能顯示對應的完成通知：Support → "Support complete"；Hollow / Drill → "Hollow/Drill complete"。
- Gizmo preview 取消後，依觸發功能顯示對應的取消通知：Support → "Support cancelled"；Hollow / Drill → "Hollow/Drill cancelled"。
- 完整切片取消後，維持既有「Slicing Canceled」取消通知。
- Preview 取消不得顯示任何 ... complete 成功文字。
- 完整切片成功完成後，維持既有「Slice complete」（msgid: "Slice ok."）通知。
- 確保殘留的 preview 狀態不會污染後續的完整切片執行。
- 盡量將所有修改限於 `Plater.cpp`、`SlicingProgressNotification.cpp/hpp`、`NotificationManager.cpp/hpp`。

**非目標：**
- 修改翻譯字串或 `OBJ_STEP_LABELS` 文字（新 msgid 除外）。
- 修改 gizmo 演算法、步驟執行順序或 Apply/Preview 按鈕行為。
- 修改 `SLAPrintSteps.cpp` 或 gizmo `.cpp` 檔案（除非 Problem 2 diagnostic 必要）。
- 消除「Slicing: Slicing model」的語意冗余（此格式為已確認可接受的結果）。
- 修改 error 路徑取消文字（本輪僅修正 user cancel；若測試另外發現 error state 也誤用 full-slice 文字，再另行處理）。

## 設計決策

### 決策一：區分機制 — `Plater::priv` 狀態欄位

**選用方案**：`Plater::priv` flag（原本為 `bool m_sla_gizmo_preview_active`，升階為 `SlaGizmoPreviewType`）。

**理由**：`reslice_SLA_until_step()` 是所有 gizmo preview partial reslice 的**唯一**進入點（三個 gizmo 均委派至 `GLGizmoSlaBase::reslice_until_step()` → `Plater::reslice_SLA_until_step()`）。所有完整切片進入點已在 restart 前呼叫 `background_process.set_task(PrintBase::TaskParams())`，提供確定性的重置點。

### 決策二：Preview Type 辨識 — `SlaGizmoPreviewType` enum

因 Hollow 與 Drill 共用 "Hollow/Drill complete"，且 Support 需顯示 "Support complete"，原本的 `bool` 不足以表達三種狀態。升階為 enum：

```cpp
enum class SlaGizmoPreviewType {
    None,           // 完整切片 / export；使用 "Slicing: " 前綴；完成顯示 "Slice ok."；取消顯示 "Slicing Canceled"
    Support,        // Support gizmo preview；不加前綴；完成顯示 "Support complete"；取消顯示 "Support cancelled"
    HollowOrDrill   // Hollow 或 Drill gizmo preview；不加前綴；完成顯示 "Hollow/Drill complete"；取消顯示 "Hollow/Drill cancelled"
};
```

**Target step 與 gizmo 對應（已確認）：**

| Gizmo | `reslice_until_step()` 傳入的 step | 對應 type |
|---|---|---|
| GLGizmoHollow | `slaposDrillHoles` | `HollowOrDrill` |
| GLGizmoDrill | `slaposDrillHoles` | `HollowOrDrill` |
| GLGizmoSlaSupports | `slaposSupportPoints` 或 `slaposPad` | `Support` |

分類邏輯（在 `reslice_SLA_until_step()` 中）：
```cpp
if (step == slaposSupportPoints || step == slaposPad)
    this->p->m_sla_gizmo_preview_type = SlaGizmoPreviewType::Support;
else if (step == slaposDrillHoles)
    this->p->m_sla_gizmo_preview_type = SlaGizmoPreviewType::HollowOrDrill;
else
    this->p->m_sla_gizmo_preview_type = SlaGizmoPreviewType::HollowOrDrill; // generic preview fallback
```

所有三個 gizmo 均使用「no-step constructor」（`m_min_sla_print_object_step = -1`），因此 `data_changed()` 的自動 reslice 保護條件 `if (required_step >= 0)` 不會觸發。實際的 `reslice_until_step()` 呼叫均來自使用者明確操作，step 值確定且可靠。

### 決策三：Preview Type 重置生命週期

| 事件 | Type 動作 |
|---|---|
| `reslice_SLA_until_step()` 被呼叫 | 設定為 `Support` 或 `HollowOrDrill` |
| `reslice()`（完整切片） | 重置為 `None` |
| `export_gcode()` / export 路徑 | 重置為 `None` |
| `on_process_completed()` 觸發（任何結果） | 在函式第一行重置為 `None`（需在使用前捕捉） |

輔助方法：
```cpp
bool is_sla_gizmo_preview_active() const {
    return m_sla_gizmo_preview_type != SlaGizmoPreviewType::None;
}
```

**Q1（已回答）**：`EVT_PROCESS_COMPLETED` 在完整切片與 gizmo preview 完成（Finished / Cancelled / Error）時均觸發。`CANCELED_INTERNAL` 不觸發，但已由完整切片進入點的提前重置覆蓋。

**Q2（已回答）**：不需在 gizmo 關閉時額外重置；`EVT_PROCESS_COMPLETED` 在使用者取消時觸發。

### 決策四：完整切片前綴格式 — "Slicing: "

在 `on_slicing_update()` 中，當 `m_sla_gizmo_preview_type == SlaGizmoPreviewType::None` 時，統一使用冒號格式：

```cpp
if (m_sla_gizmo_preview_type == SlaGizmoPreviewType::None) {
    evt.status.text = _u8L("Slicing") + ": " + evt.status.text;
}
```

預期英文結果（均為合法）：
- `Slicing: Hollowing model`
- `Slicing: Generating support points`
- `Slicing: Slicing model`（含冗余但可接受）
- `Slicing: Slicing supports`（含冗余但可接受）

**不需特殊判斷 phase label 是否已以 "Slicing" 開頭。**

### 決策五：Preview 成功完成文字 — Completion Override 機制

**核心問題**：`SlicingProgressNotification::set_status_text()` 在 `SP_COMPLETED` 狀態下硬寫 `_u8L("Slice ok.")`，忽略傳入的 `text`。直接讓傳入非空 text 覆蓋 "Slice ok." 不可行，因為完整切片的 100% event 也帶有 "Slicing done" 非空文字，會意外覆蓋 "Slice ok."。

**設計**：在 `SlicingProgressNotification` 加入 completion override：

```cpp
// SlicingProgressNotification.hpp
void set_completed_override(const std::string& text) { m_completed_override = text; }

// set_status_text() in SP_COMPLETED case:
std::string completed_text = m_completed_override.empty() ? _u8L("Slice ok.") : m_completed_override;
m_completed_override.clear(); // consume
NotificationData data{..., completed_text};
```

override 在 `SP_NO_SLICING` 與 `SP_BEGAN` 狀態重置時也清除，確保不殘留至下一次執行。

**呼叫端（`on_slicing_update()` 中的 preview + 100% 路徑）：**
```cpp
if (is_sla_gizmo_preview_active() && evt.status.percent >= 100) {
    std::string override_text;
    switch (m_sla_gizmo_preview_type) {
        case SlaGizmoPreviewType::Support:      override_text = _u8L("Support complete"); break;
        case SlaGizmoPreviewType::HollowOrDrill: override_text = _u8L("Hollow/Drill complete"); break;
        default: break;
    }
    if (!override_text.empty())
        notification_manager->set_slicing_progress_completed_override(override_text);
}
// 不 early return — 讓 set_slicing_progress_percentage 正常轉進 SP_COMPLETED，使用已設定的 override
```

**Full Slice fallback 不受影響**：完整切片的 100% event 時 `m_sla_gizmo_preview_type == None`，不呼叫 `set_slicing_progress_completed_override()`，override 維持空字串，`set_status_text()` fallback 至 `_u8L("Slice ok.")`。✓

**Preview 取消不受影響**：取消或錯誤時 100% event 不會發送，`on_slicing_update()` 不設定 override，取消通知路徑（`SP_CANCELLED`）不使用 override。✓

### 決策六：Preview 取消文字 — 在 `on_process_completed()` 捕捉型別

**核心問題**：`on_process_completed()` 在函式第一行立即重置 `m_sla_gizmo_preview_type = None`（確保任何路徑的狀態清除），但取消通知呼叫在此之後的 `evt.cancelled()` branch。若直接讀取 type 時已是 `None`，無法判斷本次是 preview 還是完整切片取消。

**設計**：在重置之前捕捉 type，用於後續取消文字選擇：

```cpp
void Plater::priv::on_process_completed(SlicingProcessCompletedEvent &evt)
{
    // 在重置前捕捉本次 run 的 preview type，供取消通知路徑使用
    SlaGizmoPreviewType completed_preview_type = m_sla_gizmo_preview_type;
    m_sla_gizmo_preview_type = SlaGizmoPreviewType::None; // 立即重置
    ...
    if (evt.cancelled()) {
        std::string cancel_text;
        switch (completed_preview_type) {
            case SlaGizmoPreviewType::Support:       cancel_text = _u8L("Support cancelled");      break;
            case SlaGizmoPreviewType::HollowOrDrill: cancel_text = _u8L("Hollow/Drill cancelled"); break;
            default:                                 cancel_text = _u8L("Slicing Canceled");        break;
        }
        this->notification_manager->set_slicing_progress_canceled(cancel_text);
        is_finished = true;
    }
    ...
}
```

**SP_CANCELLED 路徑不需修改**：`set_status_text()` 在 `SP_CANCELLED` 狀態下直接使用傳入 `text`，不 hardwire（與 `SP_COMPLETED` 不同），因此只需在呼叫端傳入正確文字。✓

**Full Slice fallback 不受影響**：完整切片時 `completed_preview_type == None`，switch 進入 `default` → `_u8L("Slicing Canceled")`，與現有行為完全一致。✓

**不需修改 `NotificationManager` 或 `SlicingProgressNotification`**：取消文字選擇完全在 `on_process_completed()` 呼叫端處理。✓

### 決策七：Problem 2（Support 起始閃現）— Diagnostic 優先

`GLGizmoSlaBase::reslice_until_step()` 使用 `wxGetApp().CallAfter(...)` 推遲 `Plater::reslice_SLA_until_step()` 的呼叫。若 background process 在此時間窗口內發出第一個 status event，該事件會在 `m_sla_gizmo_preview_type` 設定前到達，錯誤顯示前綴。

**方針**：先加 diagnostic log 確認。若確認：新增 `Plater::mark_sla_preview_pending()` public method，在 `GLGizmoSlaBase::reslice_until_step()` 的 `CallAfter` lambda **之前**同步設定 type。**在診斷確認前，不正式擴充 `Plater.hpp` 或 `GLGizmoSlaBase.cpp`。**

**Support preview 中的 "Slicing model" 顯示**：Support preview 執行至 `slaposSupportPoints` 時，切片管線先通過 `slaposObjectSlice`（phase label："Slicing model"）。此 label 在 preview 模式下不加前綴，直接顯示 "Slicing model"。此為合法的原始 phase label，diagnostic log 用於確認 event 到達時 `m_sla_gizmo_preview_type` 已是 Support（非 None）。

## 風險與取捨

- **[風險] `CallAfter` 時間差（Problem 2）** — 已確認 `GLGizmoSlaBase::reslice_until_step()` 使用 `CallAfter`。若第一個 status event 在 flag 設定前到達，會短暫顯示前綴。需 diagnostic 確認；緩解：`mark_sla_preview_pending()`。
- **[風險] Export 路徑未覆蓋** — 緩解：`EVT_PROCESS_COMPLETED` handler 無條件重置 type。
- **[風險] `CANCELED_INTERNAL` 不觸發 `EVT_PROCESS_COMPLETED`** — 緩解：所有完整切片進入點在 `set_task()` 前重置 type。
- **[取捨] "Slicing: Slicing model" 語意冗余** — 已確認為可接受的結果，不需修正。
- **[取捨] LcdOverhangDetection 等其他 gizmo preview 的完成與取消文字** — 使用 `HollowOrDrill` fallback（完成顯示 "Hollow/Drill complete"，取消顯示 "Hollow/Drill cancelled"）。若未來有需要可加入 `Generic` state。
- **[取捨] Error 路徑取消文字** — 本輪僅修正 user cancel（`evt.cancelled()`）路徑；error 路徑（`evt.error()`）保持現有行為，不加入本輪範圍。

## 待解問題

- **Q1、Q2（已回答）**：如前所述。
- **Q3（待決）**：Support preview 起始閃現的實際根本原因，待 diagnostic 確認（Task 10.1）。
- **Q4（已決策）**：取消通知文字選擇採用「重置前捕捉型別」方式，不需 cancellation override 機制（決策六）。