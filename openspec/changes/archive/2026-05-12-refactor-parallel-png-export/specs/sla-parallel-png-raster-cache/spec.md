# Spec: SLA 平行 PNG 光柵化快取

## ADDED Requirements

### Requirement: 全層完全平行光柵化
`SLAPrint::Steps::rasterize()` SHALL 以 `tbb::parallel_for(blocked_range<size_t>(0, N))` 對所有 N 層同時執行，各執行緒獨立完成：光柵化（AGG）→ stack blur（若啟用）→ PNG 編碼（miniz）→ 磁碟寫入（RasterCache），不使用 producer-consumer queue 或集中式輸出緩衝。

#### Scenario: 全層並行完成，速度目標達成
- **WHEN** `rasterize()` 對 360 層 13320×5120 執行（無快取命中）
- **THEN** 所有層在 TBB scheduler 分配下並行處理，wall time ≤ 50s

#### Scenario: 各執行緒獨立不共享輸出緩衝區
- **WHEN** N 個 TBB worker thread 同時處理不同層
- **THEN** 各執行緒使用獨立的 `cv::Mat` 與 PNG buffer，不存在輸出側的共享可寫資料結構（無 mutex 保護的共享容器）

---

### Requirement: Stack Blur 取代 Gaussian Blur
`SLA/RasterToCvMat.cpp` 中的模糊實作 SHALL 以 `agg::stack_blur_gray8(pixf, rx, ry)` 取代 `cv::GaussianBlur`，並 `#include <agg/agg_blur.h>`。僅在 `blur_pixel >= 2` 時觸發，Stack blur 為 O(radius) 分離式演算法。

#### Scenario: blur 啟用時使用 stack blur
- **WHEN** `rp.blur_pixel >= 2`
- **THEN** 對 `cv::Mat` 建立 `agg::rendering_buffer` + `agg::pixfmt_gray8` 包裝，呼叫 `agg::stack_blur_gray8(pixf, rp.blur_pixel, rp.blur_pixel)`；不呼叫 `cv::GaussianBlur`

#### Scenario: blur 停用時跳過
- **WHEN** `rp.blur_pixel < 2`
- **THEN** 不執行任何模糊操作，`cv::Mat` 保持光柵化原始輸出

---

### Requirement: miniz in-memory PNG 編碼與 RAII 釋放
各執行緒 SHALL 以 `tdefl_write_image_to_png_file_in_memory(mat.data, cols, rows, 1, &png_len)` 在記憶體中編碼完整 PNG 影像；回傳的 malloc buffer 必須以 RAII wrapper 封裝，確保在 `RasterCache::write_layer` 呼叫後（無論成功或例外）立即執行 `MZ_FREE`。

#### Scenario: PNG 編碼成功
- **WHEN** `tdefl_write_image_to_png_file_in_memory` 被呼叫且影像有效
- **THEN** 回傳非 null 指標與正確 `png_len`，完整 PNG bytes 傳遞至 `write_layer`

#### Scenario: PNG buffer 不洩漏
- **WHEN** `write_layer` 完成後（無論成功或拋出例外）
- **THEN** `MZ_FREE(png_data)` 被呼叫，不發生記憶體洩漏

---

### Requirement: RasterCache 格式改為 PNG 並原子寫入
`RasterCache::write_layer(key, lid, png_bytes)` SHALL 以 `layer_{lid:04d}.png` 作為檔名，透過 temp 檔案 + `std::filesystem::rename` 原子寫入，防止並行寫入時讀到不完整檔案；`CACHE_VERSION` SHALL 遞增，使舊 `.prz_rle` 快取自然失效。

#### Scenario: 並行寫入同一目錄不衝突
- **WHEN** 多個 TBB 執行緒同時對不同 `lid` 呼叫 `write_layer`
- **THEN** 各層檔名唯一（`layer_0000.png`、`layer_0001.png` …），rename 操作不衝突，最終所有層均正確寫入

#### Scenario: 舊版快取不命中
- **WHEN** 磁碟存在舊版 `.prz_rle` 快取
- **THEN** `is_valid()` 因 `CACHE_VERSION` 不符回傳 `false`，觸發完整重新光柵化

#### Scenario: 磁碟寫入失敗時拋出例外
- **WHEN** `write_layer` 因磁碟錯誤無法完成 rename
- **THEN** 拋出例外，`rasterize()` 在 TBB 層傳播並中止，不產生損壞的快取

---

### Requirement: 無鎖 CAS 進度回報節流
`rasterize()` 進度回報 SHALL 使用 `std::atomic<int> completed_layers{0}` 與 `std::atomic<int> last_reported_pct{-1}` 實作 CAS throttle：每個百分點（0–100）最多由一個執行緒呼叫 `report_status()`，整個 rasterize 步驟總呼叫次數 ≤ 100 次。

#### Scenario: 每百分點最多回報一次
- **WHEN** 多個執行緒在同一百分點完成
- **THEN** 僅有 `compare_exchange_strong` 成功的執行緒呼叫 `report_status()`，其餘執行緒跳過

#### Scenario: 100% 進度確實發送
- **WHEN** 最後一層完成，`done == N`
- **THEN** `pct == 100`，`report_status(100, L("Rasterizing layers..."))` 被呼叫一次

---

### Requirement: StatusReporter m_st 消除 data race
`SLAPrint.hpp` 中 `StatusReporter::m_st` 的型別 SHALL 由 `double` 改為 `std::atomic<double>`，消除多執行緒並行寫入時的 undefined behavior。

#### Scenario: 無 data race 警告
- **WHEN** 以 ThreadSanitizer（TSan）執行含多 TBB worker 的 rasterize 步驟
- **THEN** 不出現 `m_st` 相關的 data race 報告

---

### Requirement: 各執行緒 OOM 防護
各 TBB 執行緒的 `cv::Mat` SHALL 在 `write_layer` 完成後立即透過 mat 析構或 `mat.release()` 釋放；不在記憶體中同時持有所有 N 層的影像資料。

#### Scenario: 記憶體峰值不隨層數成長
- **WHEN** 對 360 層 13320×5120 執行 rasterize
- **THEN** 任一時刻在記憶體中的 `cv::Mat` 數量 ≤ TBB max_concurrency，峰值約為 `max_concurrency × 71 MB`；不隨 N 線性成長

#### Scenario: 無快取命中時不預先配置全量陣列
- **WHEN** `rasterize()` 啟動
- **THEN** 不存在長度為 N 的 `cv::Mat` 陣列；各層 Mat 隨 TBB 任務動態配置與釋放
