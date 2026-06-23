## MODIFIED Requirements

### Requirement: 全層完全平行光柵化

`SLAPrint::Steps::rasterize()` SHALL 以 `tbb::parallel_for(blocked_range<size_t>(0, N))` 對所有 N 層同時執行，各執行緒獨立完成：**雙軌光柵化（model-track AGG 光柵化；support-track 於局部 ROI 二值光柵化）→ model 套 `apply_picture_grayscale_lut` → support 局部 ROI 合成（`output = max(model_after_LUT, support_255)`，`composite_support_binary`）** → 預覽 thumb 擷取（`cv::resize` + 灰階 RLE 編碼 `rle_encode_gray` + 寫入 RasterCache）→ stack blur（若啟用，於 model-track 光柵化內部完成）→ **PRZ 層編碼（`prz_encode_layer_banded()`：條帶化 `cv::rotate(90°CW)` + 局部 `prz_orient_after_rotate` + PRZ-RLE 融合）** → 磁碟寫入（RasterCache `.rle`），不使用 producer-consumer queue 或集中式輸出緩衝。

並行 SHALL 維持 **per-layer 粒度**（`grain_size=1`，每層由單一執行緒完整處理）；條帶（band）SHALL 為層內**序列**處理，**不得**下放為層內並行，以保 RLE 狀態（`cur`/`count`/`sum`/`pos`）跨條帶序列攜帶的連續性。

support 合成 SHALL **不**使用每緒全幀 `cv::Mat`（`TLSData.support_mat` 移除）；改為在 support 局部 ROI（加 guard band、clamp 至影像）的 thread-local 小緩衝內二值光柵化，再以 ROI-local `cv::max` 合成。此 ROI 合成輸出 SHALL 與全幀合成逐像素相同。PRZ 層編碼 SHALL **不**使用每緒全幀旋轉副本（`TLSData.mat_rotated` 移除）；改由 `prz_encode_layer_banded()` 以 `≤ band_cols × M` 的條帶 tile 完成旋轉+翻轉+RLE。

雙軌處理 SHALL 依層索引決定模式：`lid >= bottom_layer_count` 時 model 套 AA／灰階／模糊、support 走二值；`lid < bottom_layer_count` 時 model 與 support 皆走二值。support-track 恆不套 `picture_grayscale` LUT。此雙軌合成邏輯與 PRZ 層編碼 SHALL 各封裝為單一共用函式，供本主迴圈與 `generate_prz()` 快取未命中路徑共同呼叫，以保證兩路徑 byte 一致。

thumb 擷取 SHALL 在 `prz_encode_layer_banded()`（即條帶化 `cv::rotate` 與 `prz_orient_after_rotate`）之前、雙軌合成之後，自 panel 方位影像取得。

#### Scenario: 全層並行完成，速度目標達成

- **WHEN** `rasterize()` 對 360 層 13320×5120 執行（無快取命中）
- **THEN** 所有層在 TBB scheduler 分配下並行處理（per-layer 粒度），每層同時產生 `.rle` 與 `_preview.rle`；PRZ 層編碼改走條帶融合（消除全幀 `mat_rotated`、省去 flip 全幀 pass 與 65MB DRAM 回讀）而 wall-clock 持平或更快

#### Scenario: 各執行緒獨立不共享輸出緩衝區

- **WHEN** N 個 TBB worker thread 同時處理不同層
- **THEN** 各執行緒使用獨立的 model `cv::Mat`、support 局部 ROI 小緩衝（非全幀）、`≤ band_cols × M` 的條帶 tile（非全幀旋轉副本）、thumb 緩衝、thumb-RLE 緩衝與 PRZ-RLE buffer，皆為 thread-local 重用，不存在輸出側的共享可寫資料結構

#### Scenario: thumb 擷取位於 PRZ 方位轉換之前

- **WHEN** worker 處理某層
- **THEN** thumb SHALL 取自雙軌合成與 `apply_picture_grayscale_lut` 之後、`prz_encode_layer_banded()` 內條帶旋轉之前的 panel 方位影像

#### Scenario: 主迴圈與快取未命中路徑 byte 一致

- **WHEN** 同一份切片分別經由 `rasterize()` 主迴圈與 `generate_prz()` 快取未命中路徑產生 PRZ-RLE
- **THEN** 兩者每層 RLE bytes 完全相同（bit-perfect），因雙軌合成與 PRZ 層編碼皆來自同一共用函式

#### Scenario: 條帶為層內序列，不下放層內並行

- **WHEN** 任一層被某執行緒處理，內部以多個條帶完成 PRZ 編碼
- **THEN** 該層的條帶 SHALL 在同一執行緒上序列處理，RLE 狀態跨條帶連續攜帶；不存在「同層多條帶並行」破壞 run-state 序列的設計