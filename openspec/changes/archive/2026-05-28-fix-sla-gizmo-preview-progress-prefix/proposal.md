## 為什麼

當 Support、Hollow、Drill gizmo 觸發 partial reslice（preview 執行）時，右下角進度通知出現以下問題：

1. **前綴誤顯**：通知在每個 phase label 前面不正確地顯示完整切片前綴「Slicing」（繁中：正在切片中），例如「正在切片中 Hollowing model」。這些 gizmo 操作不是完整切片，顯示此前綴會誤導使用者。

2. **完成通知誤顯**：Gizmo preview 完成後顯示「Slice complete」（msgid: "Slice ok."），此為完整切片的完成訊息，不應用於 preview 操作。Support 應顯示 "Support complete"，Hollow 與 Drill 應顯示 "Hollow/Drill complete"。

3. **取消通知誤顯**：Gizmo preview 被取消後顯示「Slicing Canceled」，此為完整切片的取消訊息，不應用於 preview 操作。Support preview 取消應顯示 "Support cancelled"，Hollow 與 Drill preview 取消應顯示 "Hollow/Drill cancelled"。

4. **"SlicingSlicing" 連字缺陷（舊方案已廢棄）**：舊設計曾考慮條件式不重複 prepend。最終決策改為統一使用 `"Slicing: "` 冒號格式，此方案下「Slicing: Slicing model」為合法預期結果，不再需要特殊判斷。

5. **Support preview 起始瞬間顯示前綴（疑似）**：Support gizmo 觸發 preview 的第一個 status event 可能在 flag 尚未設定時到達，造成短暫顯示完整切片前綴。此問題需 diagnostic 確認才能定案。

## 修改內容

- `Plater.cpp` 的 `on_slicing_update()` 現在無條件對所有 SLA status text 加上 `_u8L("Slicing")` 前綴。本次修改讓此邏輯具備情境感知：gizmo preview 執行中不加前綴；完整切片統一使用 `_u8L("Slicing") + ": "` 格式（例如「Slicing: Hollowing model」、「Slicing: Slicing model」）。
- 原本的 `bool m_sla_gizmo_preview_active` 升階為 `enum SlaGizmoPreviewType`（`None` / `Support` / `HollowOrDrill`），以支援功能別的完成文字與取消文字分派。分類依據為 `reslice_SLA_until_step()` 收到的 `step` 參數：`slaposSupportPoints` / `slaposPad` → Support；`slaposDrillHoles` → HollowOrDrill。
- `SlicingProgressNotification::set_status_text()` 在 `SP_COMPLETED` 狀態下新增 completion override 機制：preview 成功完成時由 `on_slicing_update()` 預先設定對應完成文字（"Support complete" / "Hollow/Drill complete"），使 notification 顯示功能別文字而非 "Slice ok."。完整切片路徑不設定 override，維持現有 "Slice ok." → "Slice complete" 行為。
- `Plater.cpp` 的 `on_process_completed()` 在取消路徑中，依重置前捕捉的 preview type 選擇對應取消文字：`SlaGizmoPreviewType::Support` → "Support cancelled"；`SlaGizmoPreviewType::HollowOrDrill` → "Hollow/Drill cancelled"；`None`（完整切片）→ "Slicing Canceled"（既有行為）。
- Support preview 起始閃現問題的修正視 diagnostic 結果而定。
- 不修改翻譯字串（新 msgid 需加入翻譯資源）、`OBJ_STEP_LABELS` 文字、gizmo 演算法、步驟執行順序或 UI 按鈕。

## 能力

### 新增能力

- `sla-gizmo-preview-progress-label`：SLA gizmo preview 操作（Support / Hollow / Drill partial reslice）進行中，進度通知不得包含「Slicing」前綴；preview 成功完成後顯示對應功能完成通知（"Support complete" / "Hollow/Drill complete"，而非「Slice complete」）；preview 取消後顯示對應功能取消通知（"Support cancelled" / "Hollow/Drill cancelled"，而非「Slicing Canceled」）；完整切片執行中使用 "Slicing: " 冒號格式前綴；完整切片完成後維持既有「Slice complete」完成通知；完整切片取消後維持既有「Slicing Canceled」取消通知。

### 修改能力

<!-- 無現有 spec 層級的需求變更 -->

## 影響範圍

- **修改檔案**：`src/slic3r/GUI/Plater.cpp`
  - `Plater::priv` struct：`bool m_sla_gizmo_preview_active` 替換為 `SlaGizmoPreviewType m_sla_gizmo_preview_type`
  - `Plater::priv::on_slicing_update()` — 條件式前綴（"Slicing: " 格式）、preview 100% 完成文字 override
  - `Plater::priv::on_process_completed()` — 重置 type，取消路徑依捕捉的 type 選擇取消文字
  - `Plater::reslice_SLA_until_step()` — 設定 preview type
  - `Plater::reslice()` / export 路徑 — 重置 type
  - `Plater::mark_sla_preview_pending()`（條件式，視 Problem 2 diagnostic 結果而定）
- **修改檔案**：`src/slic3r/GUI/SlicingProgressNotification.hpp` / `.cpp`
  - `set_completed_override()` — 新增 completion override setter
  - `set_status_text()` — 在 `SP_COMPLETED` 狀態使用 override（若有）
- **修改檔案**：`src/slic3r/GUI/NotificationManager.hpp` / `.cpp`
  - 新增 `set_slicing_progress_completed_override()` 方法
- **修改檔案**：翻譯資源 — 新增 "Support complete" / "Hollow/Drill complete" / "Support cancelled" / "Hollow/Drill cancelled" msgid
- **可能修改**（視 diagnostic 而定）：`src/slic3r/GUI/Gizmos/GLGizmoSlaBase.cpp` / `Plater.hpp`
- **不修改**：`SLAPrintSteps.cpp`、`SLAPrint.cpp/hpp`、`PrintBase.hpp`