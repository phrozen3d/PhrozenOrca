# Spec: SLA 平行 PRZ-RLE 光柵化快取

## Purpose

定義 `SLAPrint::Steps::rasterize()` 的全層 TBB 平行光柵化與 `RasterCache` 落地:各 worker 獨立完成光柵化→（預覽 thumb 擷取）→stack blur→PRZ-RLE 編碼→磁碟寫入，以無鎖進度回報與 thread-local 緩衝重用達成高吞吐且記憶體峰值不隨層數成長。

## Requirements

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

---

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
