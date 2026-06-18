## MODIFIED Requirements

### Requirement: PRZ 縮圖渲染內容

匯出 PRZ 時，系統 SHALL 以 Orthographic camera 從當前板件（`plate_id = 當前板件 index`）渲染預覽縮圖，並嵌入 PRZ header 的 116×116 與 290×290 預覽欄位（RGB565 big-endian）。渲染 SHALL 使用 `printable_only=false`、`show_bed=true`、`transparent_background=true`，且渲染尺寸為 290×290 RGBA。

縮圖內容 SHALL 納入實際會列印的 SLA 支撐結構與 pad：`Plater::export_prz()` 傳入縮圖渲染的 `ThumbnailsParams.parts_only` SHALL 為 `false`（涵蓋 `composite_id.volume_id < 0` 的 SLA aux volume，如 `slaposSupportTree`、`slaposPad`）。此放寬 SHALL 僅作用於 PRZ 匯出路徑本地的 `ThumbnailsParams`，且 SHALL NOT 修改全域縮圖過濾邏輯（`GLCanvas3D.cpp:5969`）。

預覽圖轉換維持既有行為：RGBA 像素垂直翻轉（OpenGL 座標系修正）；290×290 直接轉 RGB565；116×116 由相同渲染結果 resize 後轉 RGB565。Fallback 優先序維持不變（有效縮圖 → `preview_image_path` PNG → 填零）。

#### Scenario: 正常切片後手動匯出（目視檢查）

- **WHEN** 使用者在 SLA 模式載入模型、生成支撐、完成切片（`slapsRasterize` 完成），透過工具列手動匯出 PRZ
- **THEN** 產生的 .prz 以裝置端或 PRZ 檢視工具開啟後，header 縮圖目視可見模型**連同支撐結構與 pad**，而非僅裸模型

#### Scenario: FDM 模式縮圖回歸不受影響（目視檢查）

- **WHEN** 使用者在 FDM 模式匯出 3MF（或其他既有縮圖路徑）
- **THEN** 其縮圖行為與本變更前一致，目視確認**未**因本變更誤含擦料塔、FDM 支撐或其他幾何（因 FDM 不走 PRZ 匯出路徑，且全域過濾邏輯未被修改）

## ADDED Requirements

### Requirement: 匯出前同步載入支撐結構

`Plater::export_prz()` 在渲染縮圖前 SHALL 以**同步**方式確保 SLA 支撐／pad 網格已存在於 Prepare 視圖場景（`m_volumes`），即使匯出當下 SLA 支撐 Gizmo 處於啟用狀態。系統 SHALL NOT 依賴「切換 UI 工具後等待非同步 reload」的方式載入支撐（該方式僅觸發 `set_as_dirty()` 與非同步 paint event，與緊接的同步 `generate_thumbnail()` 之間存在 race condition）。

實作上 SHALL 透過同步的 `reload_scene()` 強制載入支撐（採 design.md D3 決策：為 `reload_scene()` 增加預設 `false` 的 `force_load_sla_support` 參數，僅 PRZ 匯出顯式傳 `true`），且 SHALL NOT 改變既有 `reload_scene()` 呼叫端的行為。匯出渲染完成後，系統 SHALL 將 Prepare 視圖回復至匯出前的視覺狀態（gizmo / 選取 / 場景內容不被破壞）。

#### Scenario: 開著支撐 Gizmo 匯出（目視檢查）

- **WHEN** 使用者在 SLA 支撐 Gizmo 仍啟用的狀態下直接匯出 PRZ
- **THEN** 產生的縮圖目視仍正確包含支撐結構（不因 Gizmo 啟用而遺漏支撐）

#### Scenario: 匯出後 Prepare 視圖狀態還原（目視檢查）

- **WHEN** 使用者於開著支撐 Gizmo 的狀態完成 PRZ 匯出
- **THEN** Prepare 視圖回到匯出前的狀態——Gizmo 仍為啟用、選取與相機未被改動、畫面無支撐網格與 Gizmo 點預覽雙重渲染的殘留