## 1. 確認唯一觸發路徑

- [x] 1.1 grep `reslice_SLA_until_step(slaposObjectSlice` 確認僅 `GLGizmoLcdOverhangDetection.cpp:588` 觸發此路徑，無其他呼叫端。

## 2. 新增 OverhangDetect Preview Type

- [x] 2.1 在 `Plater.cpp` 的 `SlaGizmoPreviewType` enum 新增 `OverhangDetect` 值，更新 inline 說明。
- [x] 2.2 在 `reslice_SLA_until_step()` 中新增 `else if (step == slaposObjectSlice)` 分支，對應至 `OverhangDetect`。

## 3. 運作中文字 Override

- [x] 3.1 在 `on_slicing_update()` 的 prefix 邏輯中，新增 `OverhangDetect` 分支，完整覆蓋 `evt.status.text = _u8L("Overhang Detecting")`，不拼接 step label。

## 4. 完成文字

- [x] 4.1 在 `on_slicing_update()` 的完成 switch 新增 `case SlaGizmoPreviewType::OverhangDetect: override_text = _u8L("Overhang detection complete");`。

## 5. 取消文字

- [x] 5.1 在 `on_process_completed()` 的取消 switch 新增 `case SlaGizmoPreviewType::OverhangDetect: cancel_text = _u8L("Overhang detection cancelled");`。

## 6. 翻譯資源

- [x] 6.1 在 `localization/i18n/PhrozenOrca.pot` 新增三條 msgid：`"Overhang Detecting"`、`"Overhang detection complete"`、`"Overhang detection cancelled"`（msgstr 空）。
- [x] 6.2 在 `localization/i18n/en/PhrozenOrca_en.po` 新增對應三條（msgstr = msgid）。
- [x] 6.3 在 `localization/i18n/zh_TW/PhrozenOrca_zh_TW.po` 新增對應三條，msgstr 分別為 `"懸空偵測中"`、`"懸空偵測完成"`、`"懸空偵測已取消"`。

## 7. 驗證

- [x] 7.1 觸發 Overhang Detection「Detect selected」；確認運作中進度通知顯示「Overhang Detecting」，**不顯示**「Hollowing model」、「Drilling holes into model」、「Slicing model」或任何 "Slicing: " 前綴。**使用者實機驗證通過。**
- [x] 7.2 等待 Overhang Detection 完成；確認通知顯示「Overhang detection complete」，**不顯示**「Hollow/Drill complete」或「Slice complete」。**使用者實機驗證通過。**
- [x] 7.3 觸發 Overhang Detection 並取消；確認顯示「Overhang detection cancelled」，**不顯示**「Hollow/Drill cancelled」或「Slicing Canceled」，**不顯示**「Overhang detection complete」。**使用者實機驗證通過。**
- [x] 7.4 Overhang Detection 完成後執行完整切片；確認完整切片進度正確顯示「Slicing: 」前綴，完成後顯示「Slice complete」（preview 狀態無殘留）。**使用者實機驗證通過。**
- [x] 7.5 確認 Support / Hollow / Drill gizmo 的現有進度通知行為不受影響。**使用者實機驗證通過。**
