## 1. 確認 Flag 生命週期進入點

- [x] 1.1 確認 `EVT_PROCESS_COMPLETED`（或其在 `Plater::priv` 的 handler）在 gizmo preview partial reslice 的正常完成與取消/錯誤情況下均會觸發，確認其為 flag 的可靠重置點
- [x] 1.2 列舉 `Plater.cpp` 中所有在完整切片或 export restart 前呼叫 `background_process.set_task(PrintBase::TaskParams())` 的位置；確認這些是完整的完整切片進入點集合，均需重置 flag

## 2. 引入 Preview 狀態 Flag（初版 bool）

- [x] 2.1 在 `Plater.cpp` 的 `Plater::priv` struct 加入 `bool m_sla_gizmo_preview_active = false;`
- [x] 2.2 在 `Plater::reslice_SLA_until_step()` 開頭，於 `restart_background_process()` 呼叫之前，設定 `p->m_sla_gizmo_preview_active = true;`
- [x] 2.3 在 `Plater::reslice()`（由 `on_action_slice_all()` 呼叫的實際派發點）中，於 `set_task(PrintBase::TaskParams())` 之前立即重置 `m_sla_gizmo_preview_active = false;`
- [x] 2.4 在每個呼叫 `background_process.set_task(PrintBase::TaskParams())` 並 restart 的 export / send-to-printer 路徑中重置 `m_sla_gizmo_preview_active = false;`（`export_gcode()` 兩個 overload）
- [x] 2.5 在 `EVT_PROCESS_COMPLETED` event handler（`on_process_completed()`）中重置 `m_sla_gizmo_preview_active = false;`，無條件在每次 background run 結束時清除

## 3. 進度 Handler 中的條件式前綴（初版）

- [x] 3.1 在 `Plater::priv::on_slicing_update()` 中，將現有的 `title_text` 前綴賦值包裹於條件式中：僅在 `!m_sla_gizmo_preview_active` 時加上 `_u8L("Slicing")`

## 4. 驗證 — Gizmo Preview（無前綴、功能別完成通知、功能別取消通知）

- [x] 4.1 執行應用程式，開啟 SLA printer 的模型，開啟 Support gizmo，觸發 Apply / 自動生成；確認進度通知顯示 phase label（例如「Generating support points」）**不附加**「Slicing: 」前綴；確認 "Slicing model" 若出現時**不帶**前綴（合法）；確認完成後顯示「Support complete」（非「Slice complete」）。**使用者實機驗證通過。**
- [x] 4.2 開啟 Hollow gizmo，觸發 Apply；確認通知顯示「Hollowing model」（或其翻譯）**不附加**前綴；確認完成後顯示「Hollow/Drill complete」（非「Slice complete」）。**使用者實機驗證通過。**
- [x] 4.3 開啟 Drill gizmo，觸發 Apply；確認通知顯示「Drilling holes into model.」（或其翻譯）**不附加**前綴；確認完成後顯示「Hollow/Drill complete」（非「Slice complete」）。**使用者實機驗證通過。**
- [x] 4.4 取消 Support preview（在完成前取消）；確認顯示「Support cancelled」，**不顯示**「Slicing Canceled」，**不顯示**「Support complete」。**使用者實機驗證通過。**
- [x] 4.5 取消 Hollow preview（在完成前取消）；確認顯示「Hollow/Drill cancelled」，**不顯示**「Slicing Canceled」，**不顯示**「Hollow/Drill complete」。**使用者實機驗證通過。**
- [x] 4.6 取消 Drill preview（在完成前取消）；確認顯示「Hollow/Drill cancelled」，**不顯示**「Slicing Canceled」，**不顯示**「Hollow/Drill complete」。**使用者實機驗證通過。**

## 5. 驗證 — 完整切片

- [x] 5.1 透過 Slice All 按鈕觸發完整切片；確認進度通知對每個 phase label 顯示「Slicing: 」冒號格式前綴（例如「Slicing: Hollowing model」、「Slicing: Slicing model」）；確認完成後顯示「Slice complete」通知。**使用者實機驗證通過。**
- [x] 5.2 執行任意 gizmo preview（Support / Hollow / Drill 任一）至完成，然後立即觸發完整切片；確認完整切片進度顯示「Slicing: 」前綴，完成後顯示「Slice complete」。**使用者實機驗證通過。**
- [x] 5.3 啟動 gizmo preview，在完成前取消，然後觸發完整切片；確認完整切片進度正確顯示「Slicing: 」前綴，完成後顯示「Slice complete」。**使用者實機驗證通過。**
- [x] 5.4 取消完整切片；確認顯示「Slicing Canceled」（既有行為不受影響）。**使用者實機驗證通過。**

## 6. 範圍外確認

- [x] 6.1 確認 `SLAPrint.cpp:920` 的 `L("Slicing done")` 文字在完整切片路徑中最終進入 `SP_COMPLETED` 狀態並顯示「Slice ok.」。採用 "Slicing: " 前綴格式後，此事件的 status text 為 "Slicing: Slicing done"，但進入 `SP_COMPLETED` 後 notification 顯示 "Slice ok."（hardcoded fallback），最終顯示不受影響。

## 7. Preview Type 辨識與 Flag 升階

- [x] 7.1 **調查**：確認 Support 與 HollowOrDrill 的分類方式。已確認：Support gizmo 的所有 Apply 路徑傳入 `slaposSupportPoints` 或 `slaposPad`；Hollow/Drill gizmo 的所有 Apply 路徑傳入 `slaposDrillHoles`。三個 gizmo 均使用 no-step constructor（`m_min_sla_print_object_step = -1`），`data_changed()` 的自動 reslice 不會觸發。可由 step 可靠分類，不需在 gizmo 呼叫端傳入額外資訊。
- [x] 7.2 **實作**：將 `Plater::priv` 中的 `bool m_sla_gizmo_preview_active` 替換為 `SlaGizmoPreviewType m_sla_gizmo_preview_type`（enum 含 `None`、`Support`、`HollowOrDrill`）；在 `reslice_SLA_until_step()` 中依 step 設定對應 type；更新所有重置點（`reslice()`、`export_gcode()` 兩個 overload、`on_process_completed()`）改為重置至 `SlaGizmoPreviewType::None`；更新 `on_slicing_update()` 的條件判斷改為 `m_sla_gizmo_preview_type == SlaGizmoPreviewType::None`

## 8. Preview 成功完成通知（功能別文字）

- [x] 8.1 **修改 `SlicingProgressNotification`**：在 `SlicingProgressNotification.hpp` 加入 `set_completed_override(const std::string& text)` 方法與 `std::string m_completed_override` 成員；修改 `set_status_text()` 在 `SP_COMPLETED` 狀態使用 override（若非空），空字串 fallback 至 `_u8L("Slice ok.")`；在 `set_progress_state(SP_NO_SLICING)` 與 `set_progress_state(SP_BEGAN)` 中清除 override
- [x] 8.2 **新增 `NotificationManager` 方法**：在 `NotificationManager.hpp` 宣告並在 `NotificationManager.cpp` 實作 `set_slicing_progress_completed_override(const std::string& text)`，將 override 設定到 `SlicingProgressNotification` 上
- [x] 8.3 **實作 preview completion 分派**：在 `on_slicing_update()` 中，當 `is_sla_gizmo_preview_active() && evt.status.percent >= 100` 時，依 `m_sla_gizmo_preview_type` 決定完成文字並呼叫 `notification_manager->set_slicing_progress_completed_override()`（Support → `_u8L("Support complete")`；HollowOrDrill → `_u8L("Hollow/Drill complete")`）；不 early return，讓 `set_slicing_progress_percentage()` 正常轉進 `SP_COMPLETED` 並消耗 override
- [x] 8.4 **翻譯資源（完成文字）**：在翻譯 `.po` 模板中新增 msgid `"Support complete"` 與 `"Hollow/Drill complete"`；英文與繁中填入對應文字；其他 locale 保留空 msgstr

## 9. 完整切片前綴格式修正（"Slicing: " 格式）

- [x] 9.1 在 `Plater::priv::on_slicing_update()` 中，將前綴邏輯改為 `_u8L("Slicing") + ": " + evt.status.text`（冒號加空格），確認英文 locale 下「Slicing: Slicing model」等為合法預期結果

## 10. 診斷並修正 Support Gizmo Preview 起始閃現

- [x] 10.1 **診斷結果**：Diagnostic log 已於實機 Support preview 測試中確認，第一個 `EVT_SLICING_UPDATE` 事件到達時 `m_sla_gizmo_preview_type == 1`（Support），`evt.status.text` 為 "Slicing model"（合法的原始 `slaposObjectSlice` phase label）。`m_sla_gizmo_preview_type` 在第一個事件到達前已由 `reslice_SLA_until_step()` 正確設定，不存在 `CallAfter` 時間差導致的錯誤前綴閃現。"Slicing model" 在 Support preview 中無 "Slicing: " 前綴，為可接受的合法顯示。**Diagnostic log 已移除。**
- [x] 10.2 **不需實作**：10.1 diagnostic 確認不存在 `CallAfter` 時間差問題，`m_sla_gizmo_preview_type` 在第一個 event 到達前已正確設定。不需新增 `Plater::mark_sla_preview_pending()` 機制。
- [x] 10.3 **不需調查**：10.1 diagnostic 已確認目前顯示為合法 phase label，無其他根本原因需追蹤。

## 11. Preview 取消通知（功能別取消文字）

- [x] 11.1 **調查取消呼叫鏈**：已確認 `on_process_completed()` 在函式第一行重置 `m_sla_gizmo_preview_type = None`（line 7510），在 `evt.cancelled()` branch（line 7578）呼叫 `set_slicing_progress_canceled(_u8L("Slicing Canceled"))`（line 7580）。重置早於取消通知，需在重置前捕捉 type。`SP_CANCELLED` 狀態下 `set_status_text()` 直接使用傳入 text（不 hardwire），因此只需在呼叫端傳入正確文字，不需修改 `NotificationManager` 或 `SlicingProgressNotification`。
- [x] 11.2 **實作取消文字選擇**：在 `on_process_completed()` 函式開頭，重置前捕捉 `SlaGizmoPreviewType completed_preview_type = m_sla_gizmo_preview_type`；在 `evt.cancelled()` branch 中依 `completed_preview_type` 選擇 cancel text（Support → `_u8L("Support cancelled")`；HollowOrDrill → `_u8L("Hollow/Drill cancelled")`；None → `_u8L("Slicing Canceled")`），傳入 `set_slicing_progress_canceled()`
- [x] 11.3 **翻譯資源（取消文字）**：在翻譯 `.po` 模板（`PhrozenOrca.pot`）與所有 `.po` 檔案中新增 msgid `"Support cancelled"` 與 `"Hollow/Drill cancelled"`；英文填入對應文字（"Support cancelled" / "Hollow/Drill cancelled"）；繁中（`zh_TW`）填入對應翻譯；其他 locale 保留空 msgstr