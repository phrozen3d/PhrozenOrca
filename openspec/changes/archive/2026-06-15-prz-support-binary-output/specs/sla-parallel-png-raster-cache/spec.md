## MODIFIED Requirements

### Requirement: 全層完全平行光柵化

`SLAPrint::Steps::rasterize()` SHALL 以 `tbb::parallel_for(blocked_range<size_t>(0, N))` 對所有 N 層同時執行，各執行緒獨立完成：**雙軌光柵化（model-track 與 support-track 分別 AGG 光柵化）→ model 套 `apply_picture_grayscale_lut` → 雙軌合成（`output = max(model_after_LUT, support_255)`）** → 預覽 thumb 擷取（`cv::resize` + 灰階 RLE 編碼 `rle_encode_gray` + 寫入 RasterCache）→ stack blur（若啟用，於 model-track 光柵化內部完成）→ PRZ-RLE 編碼 → 磁碟寫入（RasterCache `.rle`），不使用 producer-consumer queue 或集中式輸出緩衝。

雙軌處理 SHALL 依層索引決定模式：`lid >= bottom_layer_count` 時 model 套 AA／灰階／模糊、support 走二值；`lid < bottom_layer_count` 時 model 與 support 皆走二值。support-track 恆不套 `picture_grayscale` LUT。此雙軌合成邏輯 SHALL 封裝為單一共用函式，供本主迴圈與 `generate_prz()` 快取未命中路徑共同呼叫，以保證兩路徑 byte 一致。

thumb 擷取 SHALL 在 `cv::rotate` 與 `prz_orient_after_rotate` 之前、雙軌合成之後，自 panel 方位影像取得。

#### Scenario: 全層並行完成，速度目標達成

- **WHEN** `rasterize()` 對 360 層 13320×5120 執行（無快取命中）
- **THEN** 所有層在 TBB scheduler 分配下並行處理，每層同時產生 `.rle` 與 `_preview.rle`，wall time 因雙軌新增第二次 AGG fill 與合成而上升（預估 +30~60% 光柵化時間），屬已知取捨

#### Scenario: 各執行緒獨立不共享輸出緩衝區

- **WHEN** N 個 TBB worker thread 同時處理不同層
- **THEN** 各執行緒使用獨立的 model `cv::Mat`、support `cv::Mat`、thumb 緩衝、thumb-RLE 緩衝與 PRZ-RLE buffer，皆為 thread-local 重用，不存在輸出側的共享可寫資料結構

#### Scenario: thumb 擷取位於 PRZ 方位轉換之前

- **WHEN** worker 處理某層
- **THEN** thumb SHALL 取自雙軌合成與 `apply_picture_grayscale_lut` 之後、`cv::rotate(90°CW)` 之前的 panel 方位影像

#### Scenario: 主迴圈與快取未命中路徑 byte 一致

- **WHEN** 同一份切片分別經由 `rasterize()` 主迴圈與 `generate_prz()` 快取未命中路徑產生 PRZ-RLE
- **THEN** 兩者每層 RLE bytes 完全相同（bit-perfect），因雙軌合成邏輯來自同一共用函式

## ADDED Requirements

### Requirement: RasterCache key 涵蓋雙軌幾何與底層數，且無 struct padding 依賴

`RasterCache::compute_key()` SHALL 依序對每層的 model-track 與 support-track 的 contour 與 hole 點分別取雜湊（含 track 分隔以避免邊界歧義），不再僅雜湊合併後的 `transformed_slices()`。`compute_key()` SHALL 將 `SLARasterParams` 以**逐欄位**方式納入雜湊（含新增的 `bottom_layer_count`），不得以 `reinterpret_cast + sizeof` 對整個 struct 做 raw-byte 雜湊，以消除未初始化 struct padding 造成的非決定性。`SLARasterParams` 的建構點 SHALL 以 value-initialization（`{}`）建立以歸零 padding 作為防禦縱深。`CACHE_VERSION` SHALL 遞增，使既有磁碟快取全數失效並重建。

#### Scenario: 改變底層數使快取失效

- **WHEN** 使用者調整 `bottom_layer_count`（其他設定不變）後重新切片
- **THEN** `compute_key()` 因 `bottom_layer_count` 納入雜湊而產生不同 key，舊快取不命中，觸發重新光柵化，底層二值範圍正確更新

#### Scenario: 僅變動支撐幾何使快取失效

- **WHEN** 模型幾何不變、僅支撐點/支撐幾何改變後重新切片
- **THEN** `compute_key()` 因 support-track 點集改變而產生不同 key，舊快取不命中

#### Scenario: 相同輸入產生相同 key（padding 不影響）

- **WHEN** 同一份切片與設定在不同進程、不同堆疊狀態下兩次計算 `compute_key()`
- **THEN** 兩次 key 完全相同，不受 `SLARasterParams` padding bytes 的殘留值影響