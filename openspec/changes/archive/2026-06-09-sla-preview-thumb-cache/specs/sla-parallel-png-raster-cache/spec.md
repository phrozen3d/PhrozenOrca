## MODIFIED Requirements

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