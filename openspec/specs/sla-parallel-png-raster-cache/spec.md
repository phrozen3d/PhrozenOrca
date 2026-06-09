# Spec: SLA 平行 PRZ-RLE 光柵化快取

## Purpose

定義 `SLAPrint::Steps::rasterize()` 的全層 TBB 平行光柵化與 `RasterCache` 落地:各 worker 獨立完成光柵化→（預覽 thumb 擷取）→stack blur→PRZ-RLE 編碼→磁碟寫入，以無鎖進度回報與 thread-local 緩衝重用達成高吞吐且記憶體峰值不隨層數成長。

## Requirements

### Requirement: 全層完全平行光柵化

`SLAPrint::Steps::rasterize()` SHALL 以 `tbb::parallel_for(blocked_range<size_t>(0, N))` 對所有 N 層同時執行，各執行緒獨立完成：光柵化（AGG，含 `cv::fillPoly` 快速路徑）→ `apply_picture_grayscale_lut` → **預覽 thumb 擷取（`cv::resize` + 灰階 RLE 編碼 `rle_encode_gray` + 寫入 RasterCache）** → stack blur（若啟用，於 PRZ 方位影像上）→ PRZ-RLE 編碼 → 磁碟寫入（RasterCache `.rle`），不使用 producer-consumer queue 或集中式輸出緩衝。thumb 擷取 SHALL 在 `cv::rotate` 與 `prz_orient_after_rotate` 之前，自 panel 方位影像取得。

#### Scenario: 全層並行完成，速度目標達成

- **WHEN** `rasterize()` 對 360 層 13320×5120 執行（無快取命中）
- **THEN** 所有層在 TBB scheduler 分配下並行處理，每層同時產生 `.rle` 與 `_preview.rle`，wall time 維持在原有量級（thumb 擷取的淨增成本相對 AGG 可忽略）

#### Scenario: 各執行緒獨立不共享輸出緩衝區

- **WHEN** N 個 TBB worker thread 同時處理不同層
- **THEN** 各執行緒使用獨立的 `cv::Mat`、thumb 緩衝、thumb-RLE 緩衝與 PRZ-RLE buffer，不存在輸出側的共享可寫資料結構（無 mutex 保護的共享容器）

#### Scenario: thumb 擷取位於 PRZ 方位轉換之前

- **WHEN** worker 處理某層
- **THEN** thumb SHALL 取自 `apply_picture_grayscale_lut` 之後、`cv::rotate(90°CW)` 之前的 panel 方位影像

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

### Requirement: RasterCache 格式為原生 PRZ-RLE，原子寫入

`RasterCache::write_layer(key, lid, rle_bytes)` SHALL 以 `layer_{lid:04d}.rle` 作為檔名，直接寫入原生 PRZ-RLE bytes（`przByte` 格式）；`RasterCache` SHALL 另提供 thumb 寫入，以 `layer_{lid:04d}_preview.rle` 為檔名寫入輕量灰階-RLE 編碼的預覽小圖，且寫入失敗 SHALL 拋出例外。目錄由 `ensure_dir()` 在平行寫入前單執行緒建立，避免 NTFS 目錄鎖競爭；`mark_complete()` SHALL 僅在所有層的 `.rle` 與 `_preview.rle` 皆成功落地後，才寫入 `cache_complete` sentinel 標示快取完整性；`CACHE_VERSION` SHALL 遞增以確保格式變更（含 thumb 新增）時舊快取自動失效。

#### Scenario: 並行寫入同一目錄不衝突

- **WHEN** 多個 TBB 執行緒同時對不同 `lid` 呼叫 thumb 與 rle 寫入
- **THEN** 各層檔名唯一（`layer_0000.rle`、`layer_0000_preview.rle` …），不衝突，最終所有層的兩種檔案均正確寫入

#### Scenario: 任一檔寫入失敗則快取不被認定為有效

- **WHEN** 光柵化過程中任一層的 `.rle` 或 `_preview.rle` 寫入失敗，或過程被強制終止，`mark_complete()` 未被呼叫
- **THEN** `is_valid()` 回傳 `false`，下次啟動觸發完整重新光柵化

#### Scenario: sentinel 蘊涵 rle 與 thumb 皆存在

- **WHEN** `is_valid()` 回傳 `true`
- **THEN** 該快取目錄內每一層 SHALL 同時存在 `.rle` 與 `_preview.rle`

#### Scenario: 舊版快取不命中

- **WHEN** 磁碟存在舊版格式快取（缺少 `_preview.rle` 或不同 `CACHE_VERSION`）
- **THEN** `is_valid()` 因 `CACHE_VERSION` 不符回傳 `false`，觸發完整重新光柵化

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
- **THEN** 任一時刻在記憶體中的 `cv::Mat` 數量 ≤ TBB max_concurrency；不隨 N 線性成長

#### Scenario: 無快取命中時不預先配置全量陣列
- **WHEN** `rasterize()` 啟動
- **THEN** 不存在長度為 N 的 `cv::Mat` 陣列；各層 Mat 隨 TBB 任務動態配置與釋放

---

## Known Limitations

**AGG 光柵化路徑效能**：當 `gamma > 0`（Anti-Aliasing）或 `blur_pixel >= 2`（模糊）時，無法使用 `cv::fillPoly` 快速路徑，退回 AGG 光柵化。預覽端已透過降採樣 thumb 快取（見 `sla-preview-thumb-cache`）解除 UI 卡頓；切片時的 AGG 路徑本身效能仍有進一步優化空間，留待未來處理。
