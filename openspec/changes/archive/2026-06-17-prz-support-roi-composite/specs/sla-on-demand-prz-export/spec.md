## MODIFIED Requirements

### Requirement: generate_prz 採雙路徑匯出（快取命中 / 快取未命中）

`generate_prz()` SHALL 依 `RasterCache::is_valid()` 決定執行路徑：

**快取命中路徑**：以批次大小 `EXPORT_BATCH = 8` 迭代所有 N 層，每批次以 `tbb::task_arena(EXPORT_BATCH)` 限制並行數，平行呼叫 `RasterCache::read_layer` 讀取原生 PRZ-RLE bytes（`.rle` 檔案）至 `rle_results[]`；再循序寫入 per-layer header（`prz_layer_content`）、4-byte BE 長度、RLE payload、CRLF，最後一層附加 DLP end tag；每層寫完後立即 `clear()` + `shrink_to_fit()`；不執行光柵化、不使用 `cv::imdecode`。

**快取未命中路徑**：以批次大小 `BATCH_SZ = 8` 迭代 `print.print_layers()`，TBB 平行對每層執行**雙軌光柵化**——取 model-track 與 support-track 幾何（各平移 `rp.shift`），依 `lid < bottom_layer_count` 決定 model 是否走二值，support 恆走二值，model 套 `apply_picture_grayscale_lut` 後與 support 合成（`output = max(model_after_LUT, support_255)`）。support 合成 SHALL 走 **support 局部 ROI**（`composite_support_binary`），**不**配置全幀 support 緩衝；`enumerable_thread_specific<cv::Mat> support_tls` SHALL 移除。此雙軌合成 SHALL 呼叫與 `rasterize()` 主迴圈相同的共用函式，確保 byte 一致；其後接 `cv::rotate(90°CW)` 與 `prz_orient_after_rotate(prz_x_mirror)`，RLE 編碼後串流輸出，`cv::Mat` 立即釋放。

ROI 合成輸出 SHALL 與全幀合成逐像素相同，`CACHE_VERSION` 不變，既有快取續用。

#### Scenario: 快取命中時 PRZ 匯出成功且輸出 Bit-Perfect
- **WHEN** 使用者點擊匯出 PRZ，且 `RasterCache::is_valid()` 為 `true`
- **THEN** 輸出的 PRZ 檔案中每層的 RLE 編碼像素資料，與重構前版本產生的結果完全相同（bit-perfect）

#### Scenario: 快取未命中路徑與主迴圈輸出一致
- **WHEN** 快取未命中，`generate_prz()` 以雙軌光柵化 on-demand 產生各層 RLE
- **THEN** 每層 RLE bytes 與 `rasterize()` 主迴圈經 RasterCache 寫入者完全相同（bit-perfect），支撐區域為純二值且豁免 `picture_grayscale`

#### Scenario: cache-miss 路徑不持有全幀 support 緩衝
- **WHEN** 快取未命中路徑並行光柵化各層
- **THEN** 不存在每緒一張全幀 support `cv::Mat`（`support_tls` 已移除）；support 合成僅使用局部 ROI 緩衝

#### Scenario: ROI 合成與全幀版逐 byte 一致（不 bump 版本）
- **WHEN** 同一份切片分別以 Opt-2 ROI 合成與全幀合成（Opt-2 前版）匯出 PRZ
- **THEN** 兩者每層 RLE bytes 完全相同；`CACHE_VERSION` 未變，Opt-2 前建立的 disk cache 在 Opt-2 後仍 `is_valid()` 命中且與 cache-miss 輸出一致

#### Scenario: 批次邊界正確處理
- **WHEN** 切層總數不為 EXPORT_BATCH 整數倍（例如 100 層，最後一批為 4 層）
- **THEN** 最後一批正確處理剩餘層數，不發生越界存取或遺漏層

#### Scenario: 批次內各層 RLE 在循序寫入後立即釋放
- **WHEN** 第 i 批次的循序 append 完成
- **THEN** `rle_results[i].clear()` 與 `shrink_to_fit()` 被呼叫，記憶體即時歸還，不等待整個匯出結束

#### Scenario: 記憶體峰值符合預期
- **WHEN** 匯出 360 層 13320×5120 解析度的 PRZ（快取命中路徑）
- **THEN** 同一批次同時存在記憶體中的資料量 ≤ `EXPORT_BATCH × ~5 MB（RLE）≈ 40 MB`；峰值遠低於 650 MB