# Spec: SLA On-Demand PRZ 批次匯出（Delta）

## MODIFIED Requirements

### Requirement: generate_prz 採批次並行 On-Demand 光柵化

`generate_prz()` SHALL 不再執行 on-demand 光柵化（`expolygons_to_cvmat()`），改為以批次大小 `EXPORT_BATCH = 8` 迭代所有 N 層，每批次以 `tbb::task_arena(EXPORT_BATCH)` 限制並行數，平行執行：讀取 PNG 快取（`RasterCache::read_layer`）→ `cv::imdecode` 整幅解碼 → RLE 編碼；批次內各層 RLE 結果收集至 `rle_results[0..batch_n]`，再循序 append 至 `std::ostream &out` 後立即釋放。

#### Scenario: PRZ 匯出成功且輸出與舊版一致
- **WHEN** 使用者點擊匯出 PRZ，且 RasterCache 快取有效（`is_valid()` 為 `true`）
- **THEN** 輸出的 PRZ 檔案中每層的 RLE 編碼像素資料，與重構前版本產生的結果完全相同

#### Scenario: 批次邊界正確處理
- **WHEN** 切層總數不為 EXPORT_BATCH 整數倍（例如 100 層，最後一批為 4 層）
- **THEN** 最後一批正確處理剩餘層數，不發生越界存取或遺漏層

#### Scenario: 批次內各層 RLE 在循序寫入後立即釋放
- **WHEN** 第 i 批次的循序 append 完成
- **THEN** `rle_results[i].clear()` 被呼叫，記憶體即時歸還，不等待整個匯出結束

#### Scenario: 記憶體峰值符合預期
- **WHEN** 匯出 360 層 13320×5120 解析度的 PRZ
- **THEN** 同一批次同時存在記憶體中的資料量 ≤ `EXPORT_BATCH × (PNG ~3 MB + cv::Mat ~68 MB + RLE ~5 MB) ≈ 608 MB`

#### Scenario: PNG 讀取失敗時中止並顯示錯誤
- **WHEN** `RasterCache::read_layer` 因磁碟錯誤拋出例外
- **THEN** `generate_prz()` 傳播例外，`ExportPRZJob` 捕捉後顯示錯誤訊息並中止，不產生損壞的 PRZ 檔案
