## 0. AGGRaster 外部 Buffer 建構子（前置條件）

- [x] 0.1 在 `AGGRaster` 模板類別（`AGGRaster.hpp`）中新增外部 Buffer 建構子 overload：接受與現有建構子相同的參數，最後額外加入 `TValue* external_buf`；當 `external_buf != nullptr` 時，`m_buf` 以預設空 vector 初始化（零容量），`m_rbuf` 以 `external_buf` 為底指標建構，stride = `res.width_px * PixelRenderer::num_components`

- [x] 0.2 即時驗證（編譯層）：確認 `AGGRaster.hpp` 改動後整個 libslic3r 可正常編譯（`cmake --build build --target libslic3r`），不引入任何 warning

- [x] 0.3 即時驗證（行為層）：在 `RasterToCvMat.cpp` 中的 **debug-only / 暫時性** 位置（或 `tests/` 下新增一個 test case）構造一個 `std::vector<uint8_t> ext(64*64)` 作為外部 buffer，以外部 Buffer 模式在 Stack 上宣告 `RasterGrayscaleAA`，確認：① `m_buf.capacity() == 0`（無內部 Heap 分配）；② 建構子的 `clear(black)` 已將 `ext` 全部填 0；③ `draw()` 後 `ext` 中有非零像素（渲染確實進入外部 buffer）

---

## 1. TLS 基礎設施建立

- [x] 1.1 在 `expolygons_to_cvmat`（`void` 版本，`RasterToCvMat.cpp`）慢速路徑的進入點加入 `thread_local std::vector<uint8_t> small_buf`，沿用與 `fill_polygon_fast` 中 `contour_buf` 相同的 TLS 模式。`small_buf` 是像素資料的唯一緩衝，後續不再呼叫 `encode()` 也不需任何中間 memcpy

- [x] 1.2 在慢速路徑開頭，於任何 ROI 計算之前插入全圖歸零：以 `std::memset(dst.data, 0, W * H)` 取代隱性的殘留（驗證：連續兩層分別處理不同形狀，第二層輸出中不存在第一層的像素殘影）

---

## 2. Pixel-Space ROI 計算

- [x] 2.1 實作內部輔助函式（檔案範圍 static）：輸入 `ExPolygons`、`sx`、`sy`、`cx`、`cy`、`flipXY`，回傳像素空間 BBox `{px_lo, px_hi, py_lo, py_hi}`，套用 flipXY 的座標軸互換邏輯

- [x] 2.2 在 BBox 結果上加入 `mirror_x` / `mirror_y` 的翻轉與 lo/hi 對調：`mirror_x` → `(px_lo, px_hi) = (W − px_hi, W − px_lo)`；`mirror_y` 同理

- [x] 2.3 加入 `blur_pixel` 向外擴展與 image bounds 截斷，輸出最終 `{x0, y0, x1, y1}`（即時驗證：x0 ≥ 0，x1 ≤ W，y0 ≥ 0，y1 ≤ H；可加 assert 確認）

- [x] 2.4 實作 `trafo` 偏移修正公式：`mirror_x = false` 時 `new_center_x = trafo.center_x − x0/sx`；`mirror_x = true` 時 `new_center_x = trafo.center_x + (roi_w − W + x0)/sx`；`mirror_y` 同理套用 `sy`（詳見 design.md Step 3）

---

## 3. ROI 渲染管線重構

- [x] 3.1 在全圖歸零之後，以 `polys.empty()` 判斷空層並提前 return（Fast Exit）；此後所有步驟均不執行（即時驗證：空層輸出等於全黑矩陣，且無 AGGRaster 建構開銷）

- [x] 3.2 以 `small_buf.resize(roi_w * roi_h)` 分配 ROI Buffer；以 `new_trafo`（`new_center_x`、`new_center_y`）、`Resolution{roi_w, roi_h}` 及 `small_buf.data()` 為外部 buffer，在 Stack 上直接宣告 `RasterGrayscaleAA`（外部 Buffer 模式，取代原本的 `create_raster_grayscale_aa` unique_ptr 呼叫）；gamma 邏輯保持與 `create_raster_grayscale_aa` 相同的兩路分支（`gamma > 0` → `gamma_power(gamma)`；否則 → `gamma_threshold(0.5)`）

- [x] 3.3 呼叫 `raster.draw(poly)` 對所有 ExPolygons 進行渲染；渲染完成後像素資料已確實位於 `small_buf`，**不呼叫 `encode()`，無任何 memcpy**（即時驗證：在 debug build 中以 assert 確認 `raster.m_buf.capacity() == 0`）

- [x] 3.4 將 aa_steps 量化迴圈改為只掃 `small_buf`（`roi_w * roi_h` 次），不再掃全圖 `W × H`

- [x] 3.5 將 `agg::stack_blur_gray8` 的 `agg::rendering_buffer` 改為以 `small_buf.data()` 為底、`cols = roi_w`、`rows = roi_h`、`stride = roi_w`（即時驗證：`stride` 不再是 11,140，而是 `roi_w`）

- [x] 3.6 實作 row-by-row blit：以 `roi_h` 次 `memcpy`（每次 `roi_w` bytes）將 `small_buf` 複製回 `dst.data` 的 `(x0, y0)` 起點位置

---

## 4. 正確性測試

- [x] 4.1 建立像素級對比測試：相同 `ExPolygons`、`res`、`trafo`、`gamma`、`blur_pixel` 設定，分別走原始全圖路徑（暫時保留為參考實作）與新 ROI 路徑，驗證兩者輸出矩陣每個像素完全一致

- [x] 4.2 測試空層行為：`polys = {}`，驗證輸出為全黑矩陣，且執行時間遠低於非空層（fast exit 生效）

- [x] 4.3 測試 Graceful Fallback：構造一個 BBox 幾乎等於全圖的幾何，驗證 `roi_w ≈ W`、`roi_h ≈ H` 時輸出正確

- [x] 4.4 分別測試 `mirror_x = true`、`mirror_y = true`、`flipXY = true` 及兩兩組合，逐一與參考實作比對（重點：幾何位置不得偏移）

- [x] 4.5 確認快速路徑（`gamma ≈ 0 && blur_pixel == 0`）完全不受本次修改影響：執行路徑未進入新 ROI 邏輯，輸出與修改前一致

---

## 5. 效能驗證

- [x] 5.1 以超高解析度場景（11,140 × 6,230 px，360 層，AA 啟用，blur_pixel ≥ 2）執行完整切片，量測總耗時並記錄進度條在 55% 的停頓時長（目標：< 10 秒，原本 ~60 秒）

- [x] 5.2 使用 Windows Performance Analyzer 或 `tbb::tick_count` 確認 8 執行緒在穩態（第 2 層後）不再產生 Heap 鎖競爭（`small_buf` 容量足夠時 `resize` 為 no-op，AGGRaster 在 Stack 上宣告無 Heap 操作）

- [x] 5.3 確認 `stack_blur` 垂直 pass 的有效 stride 為 `roi_w`，可透過在 blur 呼叫前後加時間戳並與理論 O(roi_w × roi_h) 時間對照來間接驗證
