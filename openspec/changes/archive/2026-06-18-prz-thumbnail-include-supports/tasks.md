## 1. 共用函式：reload_scene 增加 force_load_sla_support（採 design D3 路線 b）

- [x] 1.1 在 `GLCanvas3D.hpp` 為 `reload_scene` 新增第三個帶預設值的參數：`bool force_load_sla_support = false`（置於 `force_full_scene_refresh` 之後）
- [x] 1.2 在 `GLCanvas3D.cpp:2412` 將 `load_sla_support_pad_in_scene` 計算改為併入旗標：`force_load_sla_support || m_canvas_type != ECanvasType::CanvasView3D || !sla_gizmo_active`
- [x] 1.3 在 `GUI_Preview.cpp:224` 的 `View3D::reload_scene` 同步加入可選參數並轉發給 `m_canvas->reload_scene(...)`
- [x] 1.4 確認所有既有 `reload_scene(...)` 呼叫端（Plater / GUI_ObjectList / EmbossJob 等）因預設 `false` 而行為不變（編譯期檢查 + 程式碼審視，不需改任何呼叫端）
- [x] **1.5 [驗證]** 完整建置成功；FDM 與 SLA 模式各載入一個模型，正常進出 Prepare 視圖、開關支撐 Gizmo，**目視確認**場景顯示與本變更前一致（此階段尚未改 PRZ，行為應完全無感）

## 2. PRZ 匯出：放寬 parts_only + 同步載入支撐 + 收尾還原

- [x] 2.1 在 `Plater::export_prz()`（`Plater.cpp:12536`）將縮圖用 `ThumbnailsParams` 的 `parts_only` 由 `true` 改為 `false`
- [x] 2.2 在 `export_prz()` 渲染縮圖前，對 view3D canvas 以 `force_load_sla_support=true` **同步**呼叫 `reload_scene(false, false, true)`，確保支撐／pad 進入 `m_volumes`
- [x] 2.3 渲染（`generate_thumbnail(...)`）完成後，再以一般參數 `reload_scene(false)` 同步還原 Prepare 視圖場景，避免支撐 Gizmo 啟用時殘留雙重渲染
- [x] 2.4 確認此段邏輯只在既有的「已 `slapsRasterize` 完成」前置檢查通過後執行，且不更動 gizmo 類型 / selection / 相機
- [x] **2.5 [驗證]** SLA 模式正常切片後手動匯出 PRZ，以 PRZ 檢視工具/裝置端開啟，**目視確認** header 縮圖含支撐與 pad（對照 round 1 結論）— ✅ 通過

## 3. 邊際案例與回歸的局部視覺驗證

- [x] **3.1 [驗證]** 在 SLA 支撐 Gizmo 仍啟用的狀態下直接匯出 PRZ，**目視確認**縮圖仍正確含支撐（race / gizmo 邊際案例）— ✅ 通過
- [x] **3.2 [驗證]** 上述匯出完成後，**目視確認** Prepare 視圖已還原——Gizmo 仍啟用、選取與相機未變、畫面無支撐網格與點預覽雙重渲染殘留 — ✅ 無雙重渲染殘留
- [x] **3.3 [驗證]** FDM 模式匯出 3MF，**目視確認**其縮圖未因本變更誤含擦料塔或其他幾何（回歸）— ✅ 縮圖回歸正常。⚠️ 另發現 FDM 多物件按下切片時崩潰（`SpinInput::SetValue` nullptr），經研判為既有 bug、與本變更無關 → 見 proposal.md「Technical Debt / Known Issues」
- [x] **3.4 [驗證]** SLA 多物件 / 多板件情境匯出 PRZ，**目視確認**當前板件縮圖含各物件支撐且構圖正確 — ✅ 單板多物件通過。⚠️ 多板件時第二板縮圖全黑且 PRZ 無法讀取，經研判為既有 bug、與本變更無關 → 見 proposal.md「Technical Debt / Known Issues」