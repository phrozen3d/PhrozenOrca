## Why

在超高解析度（11,140 × 6,230 px，約 6,940 萬像素）的樹脂切片場景中，開啟 Anti-aliasing 或 Blur 時，切片進度條會長時間卡頓在 55%（每組 360 層約需 1 分鐘），根本原因有二：AGGRaster 每層在 Windows 全域 Heap 分配大型像素緩衝（8 執行緒互搶堆積鎖），以及 `stack_blur` 垂直掃描以 11,140 bytes 為 Stride 逐行跳躍，造成嚴重的 L2/L3 Cache Miss。

## What Changes

- **擴充 AGGRaster 支援外部 Buffer 建構子**：`AGGRaster.hpp` 新增一個以 `TValue* external_buf` 作為像素儲存空間的建構子變體。當外部 Buffer 傳入時，內部 `m_buf` 維持空（容量為 0），`agg::rendering_buffer` 直接指向外部指標，AGGRaster 物件本身在 Stack 上宣告。這是達成「零 Heap 分配」的關鍵前提。

- 導入 **Pixel Space ROI 機制**：每層以 `get_extents(polys)` 取得 World BBox，統一映射至像素座標系（含 flipXY 轉換）後，向外擴展 `blur_pixel` 像素並裁剪至圖像邊界，得到精確的處理矩形 ROI。

- 建立 **TLS 動態小 Buffer（`small_buf`）**：在 `RasterToCvMat.cpp` 慢速路徑入口以 `thread_local std::vector<uint8_t>` 宣告，沿用與 `fill_polygon_fast` 中 `contour_buf` 相同的 TLS 模式。`small_buf` 是像素資料的**唯一**緩衝：Stack 上的 `RasterGrayscaleAA` 以外部 Buffer 模式直接渲染進 `small_buf.data()`，隨後的 aa_steps 量化迴圈與 `stack_blur` 也在同一塊記憶體完成。整個慢速路徑不再呼叫 `encode()`，無任何中間 memcpy。`stack_blur` 的 Stride 降至 `roi_w`（典型情況 < 1,000 bytes），徹底消滅 Cache Miss。

- 實作 **TLS 大矩陣每層全圖歸零**：在進入 ROI 處理前，對全圖 TLS `cv::Mat`（66 MB）執行 `memset(0)`，確保非 ROI 區域不殘留前一層資料，整體開銷約 315 ms / 360 層（可忽略）。

- 加入 **邊界條件防護**：
  - *Fast exit*：`polys` 為空時直接跳過 ROI 處理（矩陣已歸零即為正確輸出）。
  - *Graceful fallback*：ROI 擴展至等同全圖尺寸時，退化為原始流程但仍使用 TLS 緩衝，不回到 Heap 分配路徑。

## Capabilities

### New Capabilities

- `sla-roi-aa-raster-pipeline`：Pixel Space ROI 界定的 AA 光柵化管線，涵蓋 TLS small_buf 的生命週期管理、AGGRaster 外部 Buffer 模式建構、trafo 偏移計算、stack_blur 局部執行、ROI→全圖 blit，以及空層 Fast exit 與全圖退化條件的行為規格。

### Modified Capabilities

（無——此變更不改變任何使用者可見的輸出規格或現有 spec 合約。）

## Impact

- **AGG 光柵器包裝層**：`src/libslic3r/SLA/AGGRaster.hpp`——新增外部 Buffer 建構子，實現零 Heap 分配。這是本次修改範圍最廣的單一改動。
- **核心渲染模組**：`src/libslic3r/SLA/RasterToCvMat.cpp`（`expolygons_to_cvmat` 慢速路徑）、`src/libslic3r/SLA/RasterToCvMat.hpp`。
- **依賴不變**：`deps_src/agg/agg_blur.h`、`deps_src/agg/agg_gamma_functions.h`、`SLAPrintSteps.cpp` 的 `TLSData` 結構皆不需修改。
