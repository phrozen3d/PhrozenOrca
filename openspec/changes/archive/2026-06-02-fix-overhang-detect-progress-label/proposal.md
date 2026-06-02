## 為什麼

當 Overhang Detection gizmo（`GLGizmoLcdOverhangDetection`）按下「Detect selected」觸發 partial reslice 時，進度通知顯示錯誤文字：

1. **運作中誤顯 pipeline step label**：通知顯示「Hollowing model」、「Drilling holes into model」、「Slicing model」等 SLA pipeline 內部步驟名稱，使使用者誤以為正在執行 Hollow / Drill 操作，與實際的懸空偵測操作無關。

2. **完成誤顯 "Hollow/Drill complete"**：Overhang Detection preview 完成後顯示「Hollow/Drill complete」。這是因為 `reslice_SLA_until_step(slaposObjectSlice, ...)` 未對應到 `OverhangDetect` preview type，直接 fallthrough 至 `HollowOrDrill` fallback。

3. **取消誤顯 "Hollow/Drill cancelled"**：同上原因，取消時顯示「Hollow/Drill cancelled」而非與懸空偵測對應的文字。

## 修改內容

- `Plater.cpp` 新增 `SlaGizmoPreviewType::OverhangDetect` enum 值。
- `reslice_SLA_until_step()` 中新增 `slaposObjectSlice` 分支，對應至 `OverhangDetect`。
- `on_slicing_update()` 中，當 preview type 為 `OverhangDetect` 時，完整覆蓋 `evt.status.text` 為 `"Overhang Detecting"`，不拼接任何 pipeline step label。
- 完成通知新增 `OverhangDetect` → `"Overhang detection complete"`。
- 取消通知新增 `OverhangDetect` → `"Overhang detection cancelled"`。
- 翻譯資源（`.pot`、`en`、`zh_TW`）補入三條對應 msgid。

## 能力

### 修改能力

- `sla-gizmo-preview-progress-label`：擴展現有規格，新增 Overhang Detection gizmo 的 partial reslice 進度通知行為。

## 影響範圍

- **修改檔案**：`src/slic3r/GUI/Plater.cpp`
- **修改檔案**：`localization/i18n/PhrozenOrca.pot`、`localization/i18n/en/PhrozenOrca_en.po`、`localization/i18n/zh_TW/PhrozenOrca_zh_TW.po`
- **不修改**：`SLAPrintSteps.cpp`、任何 gizmo `.cpp`、`SlicingProgressNotification`、`NotificationManager`
