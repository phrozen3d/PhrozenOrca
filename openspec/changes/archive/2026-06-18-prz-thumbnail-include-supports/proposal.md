## Why

目前匯出 PRZ 時嵌入 header 的預覽縮圖，是以 `parts_only=true` 從 Prepare 視圖渲染，會把 SLA 支撐（與 pad）排除在外，縮圖只剩裸模型。Phrozen 印表機的檔案總覽與其他切片軟體（如 Chitubox）一致顯示「含支撐」的成品外觀，因此縮圖應反映實際會被列印的支撐結構，讓使用者在裝置端就能辨識正確的列印件。

## What Changes

- 匯出 PRZ 的縮圖渲染改為**納入 SLA 支撐與 pad**：僅在 `Plater::export_prz()` 自有的 `ThumbnailsParams` 將 `parts_only` 由 `true` 改為 `false`。**不修改**全域共用的縮圖過濾邏輯（`GLCanvas3D.cpp:5969`），因此 3MF / FDM / 板件縮圖完全不受影響（PRZ 匯出按鈕本就只在 SLA 模式存在，FDM 永遠不會走此路徑）。
- 渲染縮圖前，於 `export_prz()` 內**同步**確保支撐網格已載入 Prepare 視圖場景，避免「正開著 SLA 支撐 Gizmo 匯出 → 縮圖沒支撐」的邊際案例：
  - 因 `reload_scene()` 內的 `load_sla_support_pad_in_scene`（`GLCanvas3D.cpp:2412`）會就地讀取當前 gizmo 狀態，故需先中性化 SLA 支撐 gizmo 狀態（或為 `reload_scene` 增加 `force_load` override），再**同步**呼叫 `reload_scene()`，最後還原場景狀態。
  - **不採用**「切回 Move 工具、等系統自行 reload」的作法——切換 gizmo 只發 `set_as_dirty()` + 非同步 paint event，會與緊接著的 `generate_thumbnail()` 產生 race condition，導致支撐來不及載入。
- 連帶結果：PRZ 縮圖將同時包含 pad（`slaposPad`），此為預期且與 Chitubox 行為一致。

## Capabilities

### New Capabilities
<!-- 無新增 capability。 -->

### Modified Capabilities
- `prz-preview-thumbnail`: 縮圖內容定義從「僅模型零件（`parts_only=true`）」變更為「含 SLA 支撐與 pad」，並新增「渲染前須同步載入支撐、不依賴非同步 UI 行為」的時序要求。

## Impact

- **程式碼**：
  - `src/slic3r/GUI/Plater.cpp` — `Plater::export_prz()`：縮圖 `ThumbnailsParams.parts_only` 改為 `false`；新增渲染前的同步支撐載入與狀態還原邏輯。
  - `src/slic3r/GUI/GLCanvas3D.cpp`（視實作選項）— 可能為 `reload_scene()` 增加 `force_load_sla_support` override 參數；**不更動** `5969` 的全域過濾條件。
- **不受影響**：3MF / FDM / G-code / 板件縮圖、PRZ 格式本身與 RGB565 轉換邏輯、`preview_image_path` fallback。
- **驗證策略（使用者指定：以手動操作與視覺檢查為主）**：
  - 以 SLA 模式載入模型、生成支撐並切片完成後，手動匯出 PRZ，用 Phrozen 裝置端 / PRZ 檢視工具開啟並**目視確認 header 縮圖含支撐與 pad**。
  - 邊際案例手動驗證：在**開著 SLA 支撐 Gizmo** 的狀態下直接匯出 PRZ，確認縮圖仍正確含支撐，且匯出後 Prepare 視圖的 gizmo / 場景狀態未被破壞。
  - 回歸目視檢查：FDM 模式匯出 3MF，確認其縮圖未因本變更而誤含擦料塔或其他幾何。
  - 不以自動化單元測試為主要驗證手段（縮圖為 OpenGL 離屏渲染結果，倚賴視覺判讀）。

## Technical Debt / Known Issues

以下兩項問題於本變更的手動驗證過程中被發現，**經判定為系統既有 Bug，與本次縮圖修改無關，留待未來的獨立變更處理**：

- **FDM 多物件切片崩潰（`SpinInput::SetValue` nullptr）**：在 FDM 模式下載入多個物件並按下切片時崩潰，例外為 `text_ctrl == nullptr` 的讀取存取違規。崩潰堆疊位於物件設定 UI 路徑：`Plater::select_view_3D → ObjectList::update_selections → part_selection_changed → ObjectSettings::update_settings_list → TabPrintModel::set_model_config → reload_config → SpinCtrl::set_value → SpinInput::SetValue`。此路徑完全在 FDM 物件設定 Tab / OptionsGroup 元件，與本變更僅觸及的 `reload_scene` 旗標與 PRZ 縮圖渲染無交集。
- **SLA 多板件匯出第二板黑屏**：多板件情境下匯出 PRZ 時，第一板縮圖正確，但第二板預覽圖全黑且 PRZ 無法讀取。本變更的縮圖渲染僅針對當前板件（`get_curr_plate_index()`），非當前板件的縮圖／PRZ 處理屬另一條既有路徑；單板（含單板多物件）情境驗證皆正常。