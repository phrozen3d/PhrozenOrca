## Why

現有 SLA 切片管線在 `rasterize()` 步驟採用 producer-consumer 架構，single-threaded consumer 依序進行 RLE 編碼與磁碟寫入，形成嚴重瓶頸，導致 360 層 13320×5120 切片耗時約 120s；Prusa Slicer 同規格採用完全平行的 PNG 管線，僅需約 41s（3× 差距）。此次重構以 **Revert + Rebuild** 策略取代舊架構，並將 PRZ-RLE 磁碟快取改為 PNG 格式，使切片與匯出管線徹底解耦。

## What Changes

- **BREAKING** Revert commits `6572a71`、`b559a5d`、`8d3fd8d`，移除現有 producer-consumer pipeline 與 PRZ-RLE 磁碟快取
- 刪除孤立檔案 `src/libslic3r/SLA/RleEncode.hpp`
- `SLAPrint::Steps::rasterize()` 改以 `tbb::parallel_for` 對全部 N 層完全平行執行：光柵化 → stack blur → PNG 編碼 → 磁碟寫入
- `agg::stack_blur_gray8` 取代 `cv::GaussianBlur`（O(radius) vs. O(radius²)，3–5× 速度提升，經 Prusa 實務驗證品質可接受）
- `RasterCache` 快取格式由 `.prz_rle` 改為 `.png`（`tdefl_write_image_to_png_file_in_memory` 在記憶體中編碼，原子寫入磁碟）
- `StatusReporter::m_st` 補上 `std::atomic<double>` 保護，消除多執行緒 data race
- `generate_prz()` 快取命中路徑改為：批次並行（上限 8 threads）讀取 PNG → `cv::imdecode` 整幅解碼 → RLE 編碼 → 記憶體排序 → 循序 append 至 PRZ stream

## Capabilities

### New Capabilities

- `sla-parallel-png-raster-cache`：在 `rasterize()` 步驟以完全平行管線（TBB parallel_for、stack blur、miniz PNG、RasterCache 原子寫入）將各層光柵化結果存為 PNG 磁碟快取；涵蓋執行緒安全進度回報與 OOM 防護

### Modified Capabilities

- `sla-on-demand-prz-export`：匯出路徑由「匯出時 on-demand TBB 批次光柵化 → RLE 串流」改為「讀取 PNG 快取 → 批次平行解碼（≤8 threads）→ RLE 編碼 → 循序 append」；記憶體峰值約 8 × 78 MB ≈ 624 MB，移除舊版 on-demand 光柵化需求

## Impact

**核心程式碼：**
- `src/libslic3r/SLAPrintSteps.cpp`（`rasterize()` 全面重寫，約 lines 1394–1733）
- `src/libslic3r/SLA/RasterCache.cpp` / `.hpp`（快取格式 `.prz_rle` → `.png`，write/read/is_valid 介面更新）
- `src/libslic3r/SLA/RasterToCvMat.cpp`（blur 實作替換，lines 74–79）
- `src/libslic3r/Format/PhrozenPRZ.cpp`（`generate_prz()` 快取命中路徑重寫，約 lines 629–692）
- `src/libslic3r/SLAPrint.hpp`（`StatusReporter::m_st` 型別改為 `std::atomic<double>`）

**依賴項目：**
- AGG（`agg/agg_blur.h`，現有依賴，無需新增）
- miniz（`tdefl_write_image_to_png_file_in_memory`，現有依賴）
- OpenCV（`cv::imdecode`，現有依賴）
- TBB（`tbb::parallel_for`，現有依賴）

**需刪除：**
- `src/libslic3r/SLA/RleEncode.hpp`（revert 後孤立，直接刪除）
