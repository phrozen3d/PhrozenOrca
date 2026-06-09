## Why

SLA 切層 2D 預覽目前採 `sla-on-demand-preview` 的「Strategy A」：每次滑動 layer bar、`m_layer_idx` 變動時，直接在 **UI render thread** 上同步呼叫 `sla::expolygons_to_cvmat()` 即時光柵化當前層。當使用者啟用抗鋸齒（`gamma > 0`）或模糊（`blur_pixel >= 2`）時，光柵化退回 **AGG sub-pixel 慢速路徑**，於是每滑一格就在單執行緒上跑一次繁重的 AGG coverage 計算，造成預覽嚴重卡頓。

更糟的是：GL texture 有 `4096×4096 (16.77M px)` 上限，現役機型（Mega 8K = 6480×3600 = 23.3M、Revo 16K = 12000×6750 = 81M）**全部超標**，那次昂貴的 AGG 結果算完後直接被丟棄、退回向量輪廓 —— AGG 在 UI thread 上純屬空轉，使用者卻看不到點陣預覽。

本變更要在**完全不改變 AGG 演算法與列印輸出（bit-perfect 不變）**的前提下，讓 UI 預覽**永不**在 render thread 上觸發 AGG 或全解析度解碼，徹底消除滑動卡頓。

## What Changes

- **新增「預覽專用降採樣快取（Thumb Cache）」**：在既有 `rasterize()` 平行管線（[SLAPrintSteps.cpp](src/libslic3r/SLAPrintSteps.cpp) Phase 5）的 worker 執行緒內，於高解析度 `cv::Mat` 完成後「順手」`cv::resize` 出一張符合 GL 安全上限（長邊 ≤ 4096、像素 < 16M）的小圖，與既有 `.rle` 同目錄落地為 `layer_{lid:04d}_preview.rle`。
- **預覽路徑改為「只讀快取」**：[SLASlice2DCanvas::render()](src/slic3r/GUI/SLASlice2DCanvas.cpp) 不再 on-demand 光柵化，改為以 cache key 找對應層 thumb、解碼、上傳 texture。
- **存活性綁定（Liveness Binding）**：thumb 與 `.rle` 共用單一 `cache_complete` sentinel；`mark_complete()` 只在兩者皆成功落地後呼叫；thumb 寫檔失敗必須 throw，使例外阻止 sentinel 寫入，整份快取判定 invalid。
- **遞增 `CACHE_VERSION`**：使所有不含 thumb 的舊快取自然失效，無需在 cache-hit 早退路徑加裝第二套獨立檢查。
- **方位契約**：thumb 嚴格取自 panel 方位的 `mat`（`apply_picture_grayscale_lut` 之後、`cv::rotate(90°CW)` 之前），確保成為現有 preview / letterbox 邏輯的 drop-in。
- **picture_grayscale 契約**：thumb 已烙入 LUT，預覽讀快取路徑**移除** `apply_picture_grayscale_lut` 重複呼叫，防雙重套用偏暗。
- **幾何 CRC 節流**：預覽**禁止**在每次 `render()` / 滑鼠移動 / 滑 bar 時重算 `RasterCache::compute_key`（會對全模型幾何跑 CRC32）；改為只在 print/slice 本質改變時算一次並快取於成員變數。
- **安全 fallback**：thumb 缺席（如磁碟滿）時，預覽**嚴格退回 `render_vector_fallback()`**，**絕不**在 UI thread 重跑哪怕一次 AGG。
- 非破壞性：PRZ 匯出的 `.rle` bytes、列印輸出像素、AGG 演算法皆不變；thumb 為旁路產物。

## Capabilities

### New Capabilities
- `sla-preview-thumb-cache`: 預覽專用降採樣 thumb 快取的完整契約 —— 生成時機與來源方位、輕量灰階 RLE 格式與檔名規則、降採樣參數（`INTER_AREA`、長邊 ≤ 4096、等比、< 16M px）、存活性綁定、picture_grayscale 烙入語意、預覽讀取與上傳路徑、cache key 節流、thumb-miss 退向量 fallback、TLS 記憶體重用約束。

### Modified Capabilities
- `sla-on-demand-preview`: 預覽由「on-demand 即時光柵化」改為「只讀 thumb 快取」；移除 render thread 上的 `expolygons_to_cvmat` 與 `apply_picture_grayscale_lut` 呼叫；新增 cache key 快取與 thumb-miss 退向量規則。
- `sla-parallel-png-raster-cache`: rasterize 平行管線在每層 worker 內新增 thumb 生成；`mark_complete()` 改為「RLE 與 thumb 皆成功」才標記完整（存活性綁定）；`CACHE_VERSION` 遞增。

## Impact

- **程式碼**
  - [src/libslic3r/SLA/RasterCache.hpp / .cpp](src/libslic3r/SLA/RasterCache.cpp)：新增 thumb 寫/讀 API（`layer_{lid:04d}_preview.rle`）、失敗 throw 語意；新增輕量灰階 RLE 編解碼器 `rle_encode_gray` / `rle_decode_gray`；遞增 `CACHE_VERSION`（5 → 6）。
  - [src/libslic3r/SLAPrintSteps.cpp](src/libslic3r/SLAPrintSteps.cpp) Phase 5（約 line 1554–1573）：於 panel 方位 mat 後插入 thumb 擷取（`cv::resize` + `rle_encode_gray` + write thumb）；thumb 暫存納入 `TLSData` 重用；存活性綁定。
  - [src/slic3r/GUI/SLASlice2DCanvas.cpp](src/slic3r/GUI/SLASlice2DCanvas.cpp)：`render()` 改讀 thumb 快取；移除 on-demand 光柵化與 line 806 LUT；新增 cache key 成員變數（於 `set_sla_print()` 重算）；thumb 缺席退向量。
- **磁碟**：每次切片於 `temp/phrozen_sla_cache/<hash>/` 額外寫入 N 張小 RLE 預覽圖（每張數百 KB 量級）。
- **記憶體**：thumb 暫存隨 worker 重用，峰值僅 `≤ 8 threads × 幾 MB`，不隨層數 N 線性累積。
- **相依**：沿用既有 OpenCV（僅 `core` / `imgproc`，**不使用 `imgcodecs`**，以免拉入第二份 libjpeg-turbo 造成 LNK2005）與 AGG；無新外部相依。
- **相容性**：列印輸出與 PRZ `.rle` bit-perfect 不變；舊快取因 `CACHE_VERSION` 遞增自動失效並重建。