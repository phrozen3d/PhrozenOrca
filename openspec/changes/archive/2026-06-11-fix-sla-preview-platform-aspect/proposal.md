## Why

在 Phrozen Sonic Mighty Revo 16K 機型下，逐層切片預覽圖（raster 縮圖路徑）被「壓扁」，無法依物理平台比例顯示。根因是 Revo 16K 為**非方形像素**機型：像素網格 15120×6230（比例 2.427）與物理平台 211.68×118.37 mm（比例 1.788）不一致；而 GUI 的 letterbox 用「材質像素比」（`tex_h/tex_w`）決定影像長寬比，於是把畫面拉寬成 2.427 而非 1.788。方形像素機（Mega 8K：1.778==1.778）兩比例相等，故只有非方形像素機才會「變扁」。

同一層的「向量預覽路徑」早已用 `printable_area` bbox 的 bed mm 正交投影（比例正確），與 raster 路徑不一致；本變更使 raster 路徑與向量路徑、portal 外框三者同源，恢復視覺一致與正確比例。

## What Changes

- 修正 `SLASlice2DCanvas::render_texture_letterbox()` 中 `aspect_img` 的計算：由「材質像素比」改為**平台物理比** `platform_aspect_w_over_h()`（來源為 `printable_area` bbox 的 bed mm，與 portal 外框、向量預覽同源）。
- portrait/landscape 不再以材質維度分支決定比例；方位（旋轉/鏡像 UV：`mirror_x`/`mirror_y`/`flipXY`）邏輯**維持不變**，與比例正交。
- 加入**防禦性 fallback**：當 `m_print` 為空或 `platform_ar` 退化（≤0／非有限）時，退回現行「材質像素比」舊行為，避免畫面異常。
- 非破壞性變更（無 **BREAKING**）：方形像素機型為 no-op、零回歸；非方形像素機（Revo 16K）獲得修正。

## Capabilities

### New Capabilities
- `sla-preview-platform-aspect`: 逐層切片 raster 預覽的顯示長寬比，需鎖定**平台物理比例**（`printable_area`），使非方形像素機型不再變扁、並與向量預覽／外框保持一致；含退化情形的防禦性 fallback。

### Modified Capabilities
<!-- 無：本變更不更動既有 spec 的需求層級行為（sla-on-demand-preview / sla-preview-thumb-cache 的生成與快取契約不變）。 -->

## Impact

- **唯一受影響程式碼**：`src/slic3r/GUI/SLASlice2DCanvas.cpp`（`render_texture_letterbox()` 內 `aspect_img` 一處運算式，及其 fallback 守衛）。
- **不觸碰**：PRZ 匯出（`src/libslic3r/Format/PhrozenPRZ*`）、rasterizer（`RasterToCvMat`）、raster 參數快照（`SLAPrintSteps::rasterize`）、縮圖快取（`RasterCache`）、底層切片檔案格式——全部維持原狀。
- **不影響**：實機列印結果（韌體本就按非方形像素貼合，列印正確）；本變更純屬 GUI 顯示層的視覺修正。
- **無互動回歸**：2D 預覽畫布僅有 pan/zoom，無「螢幕→bed mm」反算或支撐點點選，改動顯示比例不會造成點選／量測 desync。
- **相關既有 capability（僅作參考、不修改其需求）**：`sla-on-demand-preview`、`sla-preview-thumb-cache`、`sla-printer-dim-sync`。