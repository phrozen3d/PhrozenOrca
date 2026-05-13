## Context

切片的慢速路徑（`expolygons_to_cvmat` 中 gamma > 0 或 blur_pixel > 0 的分支）每層都會透過 `create_raster_grayscale_aa()` 建立一個 `AGGRaster` 物件，其內部 `m_buf` 以全圖解析度分配（11,140 × 6,230 = 66.2 MB）。在 8 執行緒並行條件下，Windows CRT Heap 的全域鎖導致這 66 MB 分配序列化，是 55% 卡頓的首要根源。

`create_raster_grayscale_aa()` 回傳 `unique_ptr<RasterBase>`，代表 AGGRaster 物件本身也在 Heap 上，加上內部 `m_buf` 的 `vector` 分配，每層實際有**兩次** Heap 分配。

其次，`stack_blur_gray8` 的垂直掃描以完整圖像寬度（11,140 bytes）為 Stride 跳躍，遠超 L2/L3 Cache Line 覆蓋範圍，造成嚴重的 Cache Miss。由於在特定 Phrozen 測試場景中，物件幾何僅佔全圖 10% 以下面積，98% 的垂直 Stride 跳躍都屬於無效記憶體存取。

`SLAPrintSteps.cpp` 中的 TLS 機制（`TLSData`）已解決 `cv::Mat`（65 MB）與 `rle_buf`（~34 MB）的 Heap 競爭，但遺漏了 AGGRaster 的兩次 Heap 分配。

## Goals / Non-Goals

**Goals:**

- 消滅慢速路徑中 AGGRaster 的每層 Windows Heap 分配（目標：首層後無 Heap 活動）
- 將 `stack_blur` 的 Stride 從 11,140 bytes 降至 `roi_w`（典型 < 1,000 bytes）
- 將 aa_steps 量化迴圈的像素數從 69.4 M 降至 `roi_w × roi_h`
- 在不修改任何第三方函式庫原始碼（`agg_blur.h`）的前提下達成上述目標

**Non-Goals:**

- 快速路徑（gamma ≈ 0 且 blur = 0 的 `cv::fillPoly` 路徑）不在本次優化範圍內
- GPU 加速或多執行緒圖像層內並行不在本次範圍內
- 對切片輸出影像像素值的任何可見改變（此為純效能優化）

## Decisions

### 決策 1：AGGRaster 外部 Buffer 模式（Option A）

**選擇**：為 `AGGRaster` 新增一個接受 `TValue* external_buf` 的建構子變體。在慢速路徑中：

```
thread_local std::vector<uint8_t> small_buf   ← TLS，grows-only
small_buf.resize(roi_w * roi_h)               ← 穩態後 no-op
RasterGrayscaleAA raster(                     ← Stack 上直接宣告，零 Heap 分配
    Resolution{roi_w, roi_h}, pxdim, new_trafo,
    gamma_fn, small_buf.data())               ← m_buf = empty, m_rbuf 指向 small_buf
raster.draw(poly) × N                         ← 直接渲染進 small_buf
// 完全不呼叫 encode()
aa_steps 迴圈 → small_buf
stack_blur     → small_buf (stride = roi_w)
row-blit       → small_buf → dst
```

**替代方案 A（拒絕）**：以 ROI 尺寸建立 AGGRaster，仍用 `unique_ptr` + 內部 `m_buf`（ROI 大小），再 `encode()` memcpy 進 TLS `small_buf`。缺陷：每層仍有兩次 Heap 分配（unique_ptr + vector m_buf），且 encode() 的 memcpy 是完全冗餘的——渲染目標與後處理目標是同一份資料，不需複製。

**替代方案 B（拒絕）**：以全圖尺寸的 `m_buf` 加入 `TLSData` 預分配。缺陷：AGGRaster 建構子仍對整個 `m_buf` 執行 value-initialization（零填充），每層仍需 O(W×H) 記憶體寫入；且 66 MB TLS × 8 執行緒對記憶體造成相當壓力。

**替代方案 C（拒絕）**：使 AGGRaster 本身為 `thread_local`，搭配 per-layer reattach。缺陷：AGGRaster 建構需要 gamma、pxdim、trafo 等執行時參數，無法靜態初始化；需引入 `optional<>` 或 placement new，增加不必要的複雜度。

### 決策 2：thread_local 而非 TLSData 傳遞

**選擇**：在 `expolygons_to_cvmat` 慢速路徑內，以 `thread_local std::vector<uint8_t>` 宣告 `small_buf`（與現有 `fill_polygon_fast` 中 `thread_local contour_buf` 的模式一致）。`SLAPrintSteps.cpp` 的 `TLSData` 結構**不需修改**。

**拒絕方案**：修改函式簽章，增加 `small_buf` 參數從 `TLSData` 傳入。破壞現有公開 API，且 `thread_local` 已是此程式庫中 TLS 的慣用模式。

**`std::vector` 容量重用機制**：`small_buf.resize(roi_w * roi_h)` 僅在需要增大時才觸發 `realloc`；在絕大多數層中，ROI 尺寸穩定後不會產生任何 Heap 活動。

### 決策 3：全圖 memset(0) 而非髒區域追蹤

**選擇**：在每層 ROI 處理前，對完整的 TLS `cv::Mat`（66 MB）執行 `dst.setTo(0)` 或 `std::memset(dst.data, 0, W * H)`。

**效能評估**：66 MB × 360 層 = 23.8 GB，典型記憶體頻寬（~80 GB/s）下約需 0.875 ms / 層，360 層總計 ~315 ms，相比 ~60 秒的卡頓完全可以忽略。

**拒絕方案**：追蹤前一層的 ROI 範圍並僅歸零髒區域。增加架構複雜度，且在幾何差異大的連續層之間歸零面積反而可能更大。

### 決策 4：不修改 agg_blur.h

`stack_blur_gray8` 的垂直 pass 外迴圈以 `unsigned x = 0; x < w; ++x` 硬寫為全圖寬，無 ROI 參數。

**選擇**：將 `stack_blur` 完整應用於 `small_buf`（stride = `roi_w`），不修改第三方原始碼。

**拒絕方案**：為 `agg_blur.h` 增加 ROI 版本。需要 fork 上游程式碼，引入長期維護負擔。

### 決策 5：AGGRaster 外部 Buffer 建構子的設計

**選擇**：在 `AGGRaster` 模板類別中新增一個 constructor overload，以 `TValue* external_buf` 作為最後一個參數（與 GammaFn 並列）。當 `external_buf != nullptr` 時，`m_buf` 以空 vector 初始化（零容量），`m_rbuf` 直接指向 `external_buf`。`encode()` 方法保持原樣（供舊路徑使用），但在外部 Buffer 模式下不應被呼叫（新 ROI 路徑不呼叫它）。

```
// 舊建構子（保留）：
AGGRaster(res, pd, trafo, fg, bg, gammafn)
  → m_buf = vector(res.pixels())   // Heap 分配
  → m_rbuf → m_buf.data()

// 新建構子（外部 Buffer 模式）：
AGGRaster(res, pd, trafo, fg, bg, gammafn, external_buf)
  → m_buf = vector()               // 空，零 Heap 分配
  → m_rbuf → external_buf
```

`agg::rendering_buffer` 在 `m_rbuf` 初始化後僅持有原始指標，不進行所有權管理，因此外部 buffer 由 TLS `small_buf` 擁有的生命週期完全安全。

## Coordinate Transform Mathematics

### Step 1：World BBox → Pixel BBox

```
sx = SCALING_FACTOR / pxdim.w_mm
sy = SCALING_FACTOR / pxdim.h_mm
cx = trafo.center_x * sx      // full-image AGGRaster 的平移量 X
cy = trafo.center_y * sy      // full-image AGGRaster 的平移量 Y

bb = get_extents(polys)       // World BBox，Slic3r scaled integer 座標

若 flipXY = false：
  px_lo = bb.min.x() * sx + cx,  px_hi = bb.max.x() * sx + cx
  py_lo = bb.min.y() * sy + cy,  py_hi = bb.max.y() * sy + cy

若 flipXY = true（portrait，X/Y 軸互換）：
  px_lo = bb.min.y() * sy + cx,  px_hi = bb.max.y() * sy + cx
  py_lo = bb.min.x() * sx + cy,  py_hi = bb.max.x() * sx + cy

套用 mirror_x（若啟用）：
  (px_lo, px_hi) = (W - px_hi, W - px_lo)    // 水平翻轉，注意需對調 lo/hi

套用 mirror_y（若啟用）：
  (py_lo, py_hi) = (H - py_hi, H - py_lo)    // 垂直翻轉
```

### Step 2：ROI 外擴與截斷

```
x0 = max(0, floor(px_lo) - blur_pixel)
y0 = max(0, floor(py_lo) - blur_pixel)
x1 = min(W, ceil(px_hi)  + blur_pixel)
y1 = min(H, ceil(py_hi)  + blur_pixel)

roi_w = x1 - x0
roi_h = y1 - y0
```

### Step 3：ROI-Local AGGRaster 的 trafo 修正

ROI-local 座標系的要求：對於全圖像素座標 `(px_global, py_global)`，在 `small_buf` 中應落於 `(px_global − x0, py_global − y0)`。

由於 AGGRaster 的映射公式為 `px = p(...) * s_ + cx`（含可能的 mirror），調整中心偏移量如下：

```
若 mirror_x = false：
  new_center_x = trafo.center_x - x0 / sx
  // 推導：new_cx = cx - x0，故 new_center_x = new_cx / sx = trafo.center_x - x0/sx

若 mirror_x = true：
  new_center_x = trafo.center_x + (roi_w - W + x0) / sx
  // 推導：mirror 公式為 px_roi = roi_w - (p(...)*s_ + new_cx)
  //       需使 roi_w - new_cx = W - x0 - cx，故 new_cx = roi_w - W + x0 + cx

若 mirror_y = false：
  new_center_y = trafo.center_y - y0 / sy

若 mirror_y = true：
  new_center_y = trafo.center_y + (roi_h - H + y0) / sy
```

**注意**：上述公式對 `flipXY = true / false` 均適用，不需額外分支。flipXY 改變的是 world 座標哪個分量對應哪個像素軸，但 cx/cy 的偏移修正量（以像素計）是相同的。

最終以此 `new_trafo`（其中僅 `center_x` 和 `center_y` 被修改）與 `Resolution{roi_w, roi_h}` 建立 ROI-local Stack AGGRaster（外部 Buffer 模式）。

## ROI Blit 機制

AGG 渲染與 `stack_blur` 完成後，`small_buf` 的內容需要以 row-by-row memcpy 貼回全圖 TLS 矩陣：

```
dst_row_stride = W                  // dst (cv::Mat) 的 stride，連續布局
src_row_stride = roi_w              // small_buf 的 stride，等於 ROI 寬度

for dy in [0, roi_h):
  dst_offset = (y0 + dy) * dst_row_stride + x0     // 目標矩陣中的行起點
  src_offset = dy * roi_w                           // small_buf 中的行起點
  memcpy(dst.data + dst_offset,
         small_buf.data() + src_offset,
         roi_w)
```

每次 `memcpy` 僅複製 `roi_w` bytes（典型 < 1,000 bytes），共執行 `roi_h` 次，總量為 `roi_w × roi_h` bytes。在典型場景（ROI = 10% 全圖面積）下，blit 開銷約為 `6.9 MB` 而非 `66 MB`。

## Risks / Trade-offs

**[Risk] trafo 數學錯誤** → 若 mirror 公式實作錯誤，幾何位置在 mirror 配置下會偏移或映射到 ROI 外。  
**緩解**：在單元測試中比對同一層幾何在「ROI 路徑」與「原始全圖路徑」的像素輸出，驗證完全一致。

**[Risk] get_extents() 在多邊形極端頂點分散時低估 BBox** → 不會發生。`get_extents()` 對所有頂點求 min/max，是精確的 BBox，不是近似值。

**[Trade-off] 第一層後的 small_buf 增長** → 若連續層 ROI 尺寸單調遞增（例如漸大的物件截面），每層都會重分配。在切片前預先掃描全部層取得最大 ROI 並預分配 `small_buf` 可避免此問題，但增加了實作複雜度。初版不處理，視 profiling 結果再決定。

**[Trade-off] 全圖 memset 開銷** → 315 ms / 360 層，對比 ~60 秒卡頓完全可忽略。然而若未來圖像解析度繼續提升（例如 16K × 10K），memset 開銷將線性增長，屆時需改為髒區域追蹤策略。

**[Note] create_raster_grayscale_aa 的 Dead Code** → 工廠函式中的 `else if (abs(gamma - 1.0) < 1e-6)` 分支（gamma_none）在邏輯上永遠不可達，因為 `gamma ≈ 1.0 > 0` 已被第一分支捕獲。新 ROI 路徑直接複製兩路有效邏輯（`gamma > 0` → `gamma_power`；否則 → `gamma_threshold(0.5)`），不延續此死碼。

## Open Questions

- Phrozen 機型是否有同時啟用 `mirror_x = true` 與 `flipXY = true` 的配置？此組合需要在整合測試中明確覆蓋。
- `blur_pixel` 的典型範圍（2–8）對 ROI 外擴量影響有限，但若未來支援更大半徑（如 20+），ROI 與全圖的面積比差距將縮小，需重新評估優化效益。

## Future Work / Known Issues

### [Issue] UI 卷軸切換層預覽延遲（AA/Blur 啟用時）

**現象**：實機測試中發現，啟用 Anti-aliasing 或 Blur 切片後，在 UI 介面上拉動卷軸快速切換各層預覽圖時會發生明顯延遲；關閉 AA/Blur 時則完全正常。

**初步分析**：此問題與本次 `optimize-slice-performance` 優化的範疇不同——本次優化針對的是**切片計算**（`expolygons_to_cvmat` 熱路徑），而 UI 層預覽延遲屬於**顯示渲染**路徑。可能原因包括：
- 灰階影像（AA/Blur 產生非 0/255 像素值）在 UI 端需要額外的 gamma 轉換或 tone-mapping，而純二值影像可能走更短的快取路徑
- `cv::Mat` 轉 OpenGL 紋理時，灰階浮點轉換路徑（或 `CV_8UC1` → `GL_LUMINANCE` 上傳）比二值路徑慢
- 預覽圖的快取失效策略可能在灰階影像時觸發更頻繁的重新解碼

**建議後續追蹤方向**：
1. 在 UI 卷軸事件處理中插入計時點，確認延遲發生在「讀取 `cv::Mat`」、「轉換紋理格式」還是「OpenGL 上傳」階段
2. 確認預覽圖是否有 LRU Cache，以及灰階影像是否意外繞過快取機制（例如 key 不包含 gamma 參數）
3. 作為獨立的 `optimize-preview-scroll` 變更追蹤，不屬於本次 ROI 渲染優化的範疇

---

### [Issue] PhrozenPRZ 匯出路徑未套用 ROI 優化（W2）

**現象**：`PhrozenPRZ.cpp:732`（PRZ 匯出流程中呼叫 `expolygons_to_cvmat` 的位置）使用的是**回傳 `cv::Mat`** 的舊版 overload，而非本次優化的 **void 原地寫入** overload。

**影響**：PRZ 匯出路徑仍然：
- 透過 `create_raster_grayscale_aa()` 每層分配一次 66 MB 的 `m_buf`
- 呼叫 `encode()` 執行額外的 memcpy
- `stack_blur` 以全圖寬（11,140 bytes）為 Stride

主線切片流程（`SLAPrintSteps.cpp:1547`）已改用優化後的 void overload，所以切片匯出 `.slc` / `.ctb` 等格式時完全受益於本次優化；PRZ 匯出是目前已知唯一的漏網路徑。

**建議**：作為獨立的後續優化任務，將 `PhrozenPRZ.cpp:732` 改為使用 void 原地 overload，並提供預先分配好的 `cv::Mat` 緩衝，預期可對 PRZ 匯出耗時帶來相同量級的改善（~10× speedup）。
