## ADDED Requirements

### Requirement: Thumb 生成時機與來源方位（panel，LUT 之後、rotate 之前）

`SLAPrint::Steps::rasterize()` 的平行 worker 在每層處理時，SHALL 於 `apply_picture_grayscale_lut(mat)` 之後、`cv::rotate(... ROTATE_90_CLOCKWISE ...)` 之前，從 **panel 方位**的全解析度 `cv::Mat`（`mat`）擷取預覽 thumb。thumb 的擷取 MUST 早於任何 PRZ 專屬的旋轉與 X-mirror（`prz_orient_after_rotate`）介入。

#### Scenario: thumb 取自 panel 方位且 LUT 已烙入

- **WHEN** worker 完成某層的 `expolygons_to_cvmat` 與 `apply_picture_grayscale_lut`
- **THEN** thumb SHALL 從該 panel 方位、已套 `picture_grayscale` 的 `mat` 產生，且 `cv::rotate` 與 `prz_orient_after_rotate` 尚未對該影像作用

#### Scenario: PRZ 方位轉換不影響 thumb

- **WHEN** 同一層接續執行 `cv::rotate(90°CW)` 與 `prz_orient_after_rotate`
- **THEN** 這些 PRZ 專屬轉換 SHALL 僅作用於 RLE 編碼用的影像，thumb 內容 SHALL 不受其旋轉或鏡像影響

---

### Requirement: Thumb 降採樣參數（等比、長邊 ≤ 4096、像素 < 16M）

thumb SHALL 以單一縮放比 `scale = 4096 / max(width, height)` 對兩軸**等比**縮小（當 `scale >= 1` 時得保留原尺寸）。縮放演算法 MUST 使用 `cv::INTER_AREA`。產生的 thumb 尺寸 MUST 滿足 GL texture 上限：長邊 ≤ 4096 且總像素 < 16.77M（4096×4096）。

#### Scenario: 超大來源等比縮小至上限內

- **WHEN** 來源 `mat` 為 12000×6750（81M px）
- **THEN** thumb 長邊 SHALL 縮為 4096、保持原 aspect ratio，且總像素 SHALL < 16.77M，可被 `upload_grayscale_texture` 接受

#### Scenario: 縮放保持長寬比

- **WHEN** 任意來源尺寸被降採樣
- **THEN** thumb 的寬高比 SHALL 與來源一致，避免 letterbox 顯示比例失真

---

### Requirement: Thumb 落地格式與檔名

thumb SHALL 以輕量灰階 RLE 編碼（`RasterCache::rle_encode_gray`，格式 `[u32 w LE][u32 h LE]` + `(value:u8, run:u8∈1..255)` 對），並透過 `RasterCache` 寫入與該層 `.rle` 相同的快取目錄，檔名格式為 `layer_{lid:04d}_preview.rle`。thumb 編碼 MUST NOT 使用 OpenCV imgcodecs（PNG/JPEG），以免拉入第二份 libjpeg-turbo 造成連結符號衝突（LNK2005）。

#### Scenario: thumb 與 rle 同目錄且檔名唯一

- **WHEN** worker 寫出第 `lid` 層
- **THEN** 同一 `key.dir` 內 SHALL 同時存在 `layer_{lid:04d}.rle` 與 `layer_{lid:04d}_preview.rle`，各層檔名唯一不衝突

---

### Requirement: 存活性綁定（RLE 與 thumb 同生共死）

`RasterCache` 的 thumb 寫入失敗 SHALL 拋出例外（比照既有 `write_layer` 的失敗語意），使例外傳出平行迴圈、`mark_complete()` 不被呼叫，整份快取被判定為 invalid。`cache_complete` sentinel 的存在 MUST 蘊涵「所有層的 `.rle` 與 `_preview.rle` 皆已成功落地」。`CACHE_VERSION` SHALL 遞增，使所有不含 thumb 的舊快取整體失效。

#### Scenario: thumb 寫入失敗使整份快取失效

- **WHEN** 任一層的 thumb 寫檔失敗（例如磁碟已滿）
- **THEN** 例外 SHALL 傳出 `tbb::parallel_for`，`mark_complete()` SHALL 不被呼叫，`is_valid()` 回傳 `false`，下次切片觸發完整重建

#### Scenario: sentinel 蘊涵 thumb 必存在

- **WHEN** `RasterCache::is_valid(key)` 回傳 `true`
- **THEN** 該 `key.dir` 內每一層的 `_preview.rle` SHALL 皆存在

#### Scenario: 舊版（無 thumb）快取因版本失效

- **WHEN** 磁碟存在本變更前產生的快取（缺少 `_preview.rle`）
- **THEN** 因 `CACHE_VERSION` 遞增，`compute_key` 產生不同 hash，舊快取 SHALL 不命中

---

### Requirement: UI 預覽僅讀 thumb 快取，永不觸發 AGG 或全解析度解碼

`SLASlice2DCanvas::render()` 的點陣預覽路徑 SHALL 僅以已快取的 cache key 與 `m_layer_idx` 讀取對應的 `_preview.rle`、解碼、上傳 texture。此路徑 MUST NOT 呼叫 `expolygons_to_cvmat`（AGG / fast-path）或解碼任何全解析度 `.rle`。

#### Scenario: 滑動 bar 只讀小圖

- **WHEN** 使用者移動 layer slider 使 `m_layer_idx` 改變
- **THEN** `render()` SHALL 讀取並解碼對應 `_preview.rle` 後上傳 texture，且 SHALL NOT 呼叫 `expolygons_to_cvmat`

#### Scenario: 同層不重複解碼

- **WHEN** `render()` 被呼叫且 `m_layer_idx == m_cached_layer`
- **THEN** SHALL 直接使用現有 GPU texture，不重新讀檔或解碼

---

### Requirement: cache key 節流（綁定後惰性計算一次，emptiness-guarded）

`SLASlice2DCanvas` SHALL 將 `RasterCache::compute_key()` 的結果快取於成員變數（`m_cache_key`）。`set_sla_print()` 與 `reset_print()` SHALL 重置（清空）該成員。key 的計算 SHALL 於 `render()` 內以 emptiness guard 進行：僅當成員為空、`is_step_done(slapsRasterize)` 為 `true` 且 `raster_params().has_value()` 為 `true` 時計算一次。整個綁定週期內 `compute_key()` 的呼叫次數 MUST 最多為一次（重切片導致 `set_sla_print` 再次呼叫並重置後，得再計算一次）；`render()` MUST NOT 每幀或每次滑動 bar 重複呼叫 `compute_key()`。

#### Scenario: render 不每幀重算全模型 CRC

- **WHEN** key 已快取後，`render()` 因重繪、滑鼠移到 slider 或滑動 bar 而被反覆呼叫
- **THEN** SHALL 使用成員中已快取的 cache key，SHALL NOT 再次呼叫 `compute_key()`（不對全模型幾何重算 CRC32）

#### Scenario: rasterize 完成後惰性計算一次

- **WHEN** `set_sla_print()` 時 `raster_params()` 尚為空（早於 `slapsRasterize`），其後 rasterize 完成、`render()` 再次被呼叫
- **THEN** `render()` SHALL 在偵測到 `m_cache_key` 為空且 `raster_params().has_value()` 為 `true` 時計算一次 cache key 並快取

#### Scenario: 換 print 或重置時清除

- **WHEN** `set_sla_print()` 以新指標被呼叫，或 `reset_print()` 被呼叫
- **THEN** SHALL 清空 `m_cache_key`，使下一次 `render()` 在條件滿足時重新計算一次

---

### Requirement: 預覽不重複套用 picture_grayscale

由於 thumb 在 worker 端（生成時）已烙入 `picture_grayscale` LUT，`SLASlice2DCanvas::render()` 的讀快取路徑 MUST NOT 再次呼叫 `apply_picture_grayscale_lut`。

#### Scenario: 讀快取路徑不再套 LUT

- **WHEN** 預覽從 `_preview.rle` 取得影像
- **THEN** SHALL 直接上傳該影像，SHALL NOT 對其再套 `picture_grayscale` LUT，避免亮度雙重衰減

---

### Requirement: Thumb 缺席時嚴格退回向量輪廓

當預覽所需的 `_preview.rle` 不存在（極端情境，如部分檔案遺失）時，`render()` SHALL 退回 `render_vector_fallback()`。此情境下 MUST NOT 在 UI 執行緒上觸發任何 AGG 次像素計算或全解析度光柵化／解碼。

#### Scenario: thumb 檔缺席退向量

- **WHEN** 目標層的 `_preview.rle` 讀取失敗或不存在
- **THEN** `render()` SHALL 呼叫 `render_vector_fallback()` 顯示向量輪廓，且 SHALL NOT 呼叫 `expolygons_to_cvmat`

---

### Requirement: Thumb 生成的記憶體峰值有界且不隨層數成長

thumb 生成 SHALL 重用既有的 `tbb::enumerable_thread_specific<TLSData>` 機制：降採樣目的地與灰階-RLE 編碼緩衝皆為 thread-local 並跨層重用（內容覆寫而非釋放重配）。全解析度 `cv::Mat` SHALL 維持既有的跨層重用、不每層銷毀重配。任一時刻記憶體中的 thumb 相關資料量 SHALL 受並行度上限約束，不隨層數 N 線性成長。

#### Scenario: thumb 緩衝跨層重用

- **WHEN** 同一 worker 執行緒連續處理多層
- **THEN** thumb 與灰階-RLE 編碼緩衝 SHALL 重用既有配置（僅覆寫內容），不產生每層的大塊配置／釋放

#### Scenario: 記憶體峰值不隨 N 成長

- **WHEN** 對 360 層大解析度模型執行 rasterize
- **THEN** thumb 相關記憶體峰值 SHALL ≤ 並行度 × 單執行緒緩衝量，不隨層數線性累積

---

### Requirement: 預覽顯示方位依 trafo mirror 動態決定（與 LCD 鏡像脫鉤）

由於 thumb 在 panel 方位、且已套用 `trafo`（`mirror_x`/`mirror_y`/`flipXY`），`SLASlice2DCanvas::render_texture_letterbox` 的 UV 對映 MUST NOT 使用與機型無關的寫死常數，SHALL 依當前 `m_print->raster_params()->trafo` 的 `mirror_x`/`mirror_y` 動態組裝，使預覽一律呈現 canonical bed 方位（完全無鏡射），與印表機 `display_mirror_*` 設定脫鉤。PRZ 匯出的方位（獨立管線）MUST NOT 受此影響。

#### Scenario: portrait 機型方位隨 mirror_x 正確

- **WHEN** 預覽 portrait 機型，`trafo.mirror_x == true`（如 Revo 16K，`display_mirror_x=0`）相對 `trafo.mirror_x == false`（如 Mega 8K，`display_mirror_x=1`）
- **THEN** 兩者 SHALL 各自顯示為相同的 canonical bed 方位（無上下顛倒），即 UV 在 `trafo.mirror_x == true` 時對 rot90 base 施加螢幕垂直翻轉

#### Scenario: 預覽方位不依賴寫死 UV 常數

- **WHEN** 檢視 `render_texture_letterbox` 實作
- **THEN** portrait/landscape 分支的 UV SHALL 由 `trafo.mirror_x`/`mirror_y` 條件式組裝，不存在與機型無關、對全系列共用的單一寫死方位常數

#### Scenario: PRZ 方位不受預覽 UV 影響

- **WHEN** 預覽 UV 動態調整後匯出 PRZ
- **THEN** PRZ 各層 RLE bytes SHALL 與調整前 bit-perfect 相同（預覽僅影響 GPU 顯示，不觸及 RLE/PRZ 管線）