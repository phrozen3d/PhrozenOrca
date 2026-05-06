## ADDED Requirements

### Requirement: generate_prz 採批次並行 On-Demand 光柵化

`generate_prz()` SHALL 不再讀取 `print.layer_images()`，改為以批次大小 `BATCH_SZ = 8` 迭代 `print.print_layers()`，每批次 TBB 平行呼叫 `expolygons_to_cvmat()`，編碼完成後立即釋放 `cv::Mat`。

#### Scenario: PRZ 匯出成功且輸出與舊版一致
- **WHEN** 使用者點擊匯出 PRZ，且 `raster_params().has_value()` 為 `true`
- **THEN** 輸出的 PRZ 檔案中每層的 RLE 編碼像素資料，與舊版（透過 `m_layer_images`）產生的結果完全相同

#### Scenario: 批次邊界正確處理
- **WHEN** 切層總數不為 BATCH_SZ 整數倍（例如 100 層，最後一批為 4 層）
- **THEN** 最後一批正確處理剩餘層數，不發生越界存取或遺漏層

#### Scenario: 每層 cv::Mat 在 RLE 編碼後立即釋放
- **WHEN** 第 i 層的 RLE 編碼完成
- **THEN** `batch_mats[i].release()` 被呼叫，記憶體即時歸還，不等待批次或匯出結束

#### Scenario: 記憶體峰值符合預期
- **WHEN** 匯出 100 層 Mega 8K 解析度的 PRZ
- **THEN** `cv::Mat` 相關記憶體峰值 ≤ `BATCH_SZ × 約 23 MB = 約 184 MB`（相較舊版 ~2.3 GB）

---

### Requirement: prz_header 改用 print_layers 計算層數

`prz_header()` 中所有 `print.layer_images().size()` 呼叫 SHALL 改為 `print.print_layers().size()`，以獲取正確的切層總數。

#### Scenario: header 中的層數欄位正確
- **WHEN** `prz_header()` 被呼叫
- **THEN** header 中的 `total` 層數欄位值等於 `print.print_layers().size()`

---

### Requirement: 匯出按鈕 guard 條件更新

`Plater.cpp` 的匯出前 guard SHALL 改為檢查 `is_step_done(slapsRasterize) && raster_params().has_value()`，不再依賴 `layer_images().empty()`。

#### Scenario: 切片完成後匯出按鈕可點擊
- **WHEN** `slapsRasterize` 步驟完成且 `raster_params()` 有值
- **THEN** 匯出按鈕不被 guard 攔截，`generate_prz()` 正常執行

#### Scenario: 切片尚未完成時匯出被攔截
- **WHEN** `slapsRasterize` 步驟未完成或 `raster_params()` 無值
- **THEN** 匯出操作顯示錯誤訊息並中止

---

### Requirement: rasterize Pipeline 2 移除

`SLAPrintSteps.cpp::rasterize()` 中建構 `all_layers` 並呼叫 `expolygons_layers_to_cvmat()` 填入 `m_layer_images` 的所有程式碼 SHALL 被移除，改為計算並填入 `m_raster_params`。

#### Scenario: rasterize 步驟不再填充 m_layer_images
- **WHEN** `slapsRasterize` 步驟完成
- **THEN** `print.layer_images()` 為空向量，`print.raster_params().has_value()` 為 `true`

#### Scenario: rasterize 步驟時間縮短
- **WHEN** `slapsRasterize` 步驟執行
- **THEN** 執行時間僅包含 Pipeline 1（`draw_layers()`），不包含 Pipeline 2 的全量光柵化，等待時間約減少 50%
