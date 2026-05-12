# Spec: SLA On-Demand PRZ 批次匯出

## Requirement: generate_prz 採雙路徑匯出（快取命中 / 快取未命中）

`generate_prz()` SHALL 依 `RasterCache::is_valid()` 決定執行路徑：

**快取命中路徑**：以批次大小 `EXPORT_BATCH = 8` 迭代所有 N 層，每批次以 `tbb::task_arena(EXPORT_BATCH)` 限制並行數，平行呼叫 `RasterCache::read_layer` 讀取原生 PRZ-RLE bytes（`.rle` 檔案）至 `rle_results[]`；再循序寫入 per-layer header（`prz_layer_content`）、4-byte BE 長度、RLE payload、CRLF，最後一層附加 DLP end tag；每層寫完後立即 `clear()` + `shrink_to_fit()`；不執行光柵化、不使用 `cv::imdecode`。

**快取未命中路徑**：以批次大小 `BATCH_SZ = 8` 迭代 `print.print_layers()`，TBB 平行呼叫 `expolygons_to_cvmat()` on-demand 光柵化，RLE 編碼後串流輸出，`cv::Mat` 立即釋放。

### Scenario: 快取命中時 PRZ 匯出成功且輸出 Bit-Perfect
- **WHEN** 使用者點擊匯出 PRZ，且 `RasterCache::is_valid()` 為 `true`
- **THEN** 輸出的 PRZ 檔案中每層的 RLE 編碼像素資料，與重構前版本產生的結果完全相同（bit-perfect）

### Scenario: 批次邊界正確處理
- **WHEN** 切層總數不為 EXPORT_BATCH 整數倍（例如 100 層，最後一批為 4 層）
- **THEN** 最後一批正確處理剩餘層數，不發生越界存取或遺漏層

### Scenario: 批次內各層 RLE 在循序寫入後立即釋放
- **WHEN** 第 i 批次的循序 append 完成
- **THEN** `rle_results[i].clear()` 與 `shrink_to_fit()` 被呼叫，記憶體即時歸還，不等待整個匯出結束

### Scenario: 記憶體峰值符合預期
- **WHEN** 匯出 360 層 13320×5120 解析度的 PRZ（快取命中路徑）
- **THEN** 同一批次同時存在記憶體中的資料量 ≤ `EXPORT_BATCH × ~5 MB（RLE）≈ 40 MB`；峰值遠低於 650 MB

---

## Requirement: prz_header 改用 print_layers 計算層數

`prz_header()` 中所有 `print.layer_images().size()` 呼叫 SHALL 改為 `print.print_layers().size()`，以獲取正確的切層總數。

### Scenario: header 中的層數欄位正確
- **WHEN** `prz_header()` 被呼叫
- **THEN** header 中的 `total` 層數欄位值等於 `print.print_layers().size()`

---

## Requirement: 匯出按鈕 guard 條件更新

`Plater.cpp` 的匯出前 guard SHALL 改為檢查 `is_step_done(slapsRasterize) && raster_params().has_value()`，不再依賴 `layer_images().empty()`。

### Scenario: 切片完成後匯出按鈕可點擊
- **WHEN** `slapsRasterize` 步驟完成且 `raster_params()` 有值
- **THEN** 匯出按鈕不被 guard 攔截，`generate_prz()` 正常執行

### Scenario: 切片尚未完成時匯出被攔截
- **WHEN** `slapsRasterize` 步驟未完成或 `raster_params()` 無值
- **THEN** 匯出操作顯示錯誤訊息並中止

---

## Requirement: rasterize Pipeline 2 移除

`SLAPrintSteps.cpp::rasterize()` 中建構 `all_layers` 並呼叫 `expolygons_layers_to_cvmat()` 填入 `m_layer_images` 的所有程式碼 SHALL 被移除，改為計算並填入 `m_raster_params`。

### Scenario: rasterize 步驟不再填充 m_layer_images
- **WHEN** `slapsRasterize` 步驟完成
- **THEN** `print.layer_images()` 為空向量，`print.raster_params().has_value()` 為 `true`

### Scenario: rasterize 步驟時間縮短
- **WHEN** `slapsRasterize` 步驟執行
- **THEN** 執行時間僅包含 Pipeline 1（`draw_layers()`），不包含 Pipeline 2 的全量光柵化，等待時間約減少 50%

---

## Requirement: generate_prz 串流寫檔至 std::ostream

`generate_prz()` 簽章 SHALL 改為接收 `std::ostream &out`，每批次處理完成後直接呼叫 `out.write()` 串流寫入，不再在記憶體中累積完整的 PRZ 字串。

### Scenario: 匯出期間記憶體無大型 string buffer
- **WHEN** 匯出任意大小的 PRZ
- **THEN** 不存在持有完整 PRZ 資料的 `std::string`；各層 RLE 資料在寫入後即釋放

---

## Requirement: PRZ 匯出在背景執行緒執行（ExportPRZJob）

PRZ 匯出 SHALL 透過 `ExportPRZJob`（繼承 `Job`）在 `PlaterWorker` 背景執行緒執行，主執行緒不被封鎖。

### Scenario: 匯出期間 UI 保持響應
- **WHEN** 使用者點擊匯出 PRZ
- **THEN** 匯出在背景執行，主 UI 執行緒不凍結，使用者可繼續操作

### Scenario: 進度條正確更新
- **WHEN** `generate_prz()` 的 progress callback 以每批次為單位呼叫
- **THEN** 通知進度條顯示 0-100% 的匯出進度

### Scenario: 取消功能正常
- **WHEN** 使用者在匯出期間點擊取消
- **THEN** `progress` callback 返回 `false`，`generate_prz()` 中止，不完整的 PRZ 檔案被刪除
