## 0. 影像後處理 Helpers

- [x] 0.1 在 `src/libslic3r/SLA/RasterToCvMat.hpp` 的 `sla` namespace 中宣告 `void apply_picture_grayscale_lut(cv::Mat& mat, uint8_t level);`
- [x] 0.2 在 `src/libslic3r/SLA/RasterToCvMat.cpp` 中實作 `sla::apply_picture_grayscale_lut()`（使用 `cv::LUT`，level==255 時 fast-path 直接返回）

## 1. 資料結構：SLARasterParams

- [x] 1.1 在 `src/libslic3r/SLAPrint.hpp` 的 `SLAPrint` class 宣告之前，新增 `SLARasterParams` struct（包含 res, pxdim, trafo, gamma, aa_steps, gray_lo, gray_hi, blur_pixel, shift, picture_grayscale 欄位）
- [x] 1.2 在 `SLAPrint` class 中新增 `std::optional<SLARasterParams> m_raster_params` private 成員
- [x] 1.3 在 `SLAPrint` class 中新增 `const std::optional<SLARasterParams>& raster_params() const { return m_raster_params; }` public accessor
- [x] 1.4 在 `src/libslic3r/SLAPrint.cpp` 的 `invalidate_step()` 中，`slapsRasterize` 分支加入 `m_raster_params = std::nullopt;` 重設邏輯

## 2. rasterize() Pipeline 2 移除

- [x] 2.1 在 `src/libslic3r/SLAPrintSteps.cpp::rasterize()` 中，提取現有的光柵化參數計算（res, pxdim, trafo, gamma, aa_steps, gray_lo, gray_hi, blur_pixel, shift, picture_grayscale）為 `SLARasterParams` 結構並填入 `m_print->m_raster_params`
- [x] 2.2 移除 `rasterize()` 中的 Pipeline 2：刪除 `all_layers` 向量建構、`expolygons_layers_to_cvmat()` 呼叫、`m_layer_images` 填充，以及後續的 `picture_grayscale` LUT 套用迴圈
- [x] 2.3 確認 `m_layer_images` 在 `rasterize()` 結束後為空向量（不填充）

## 3. PRZ 匯出：Batched On-Demand 邏輯

- [x] 3.1 確保 `src/libslic3r/Format/PhrozenPRZ.cpp` 已引入 `src/libslic3r/SLA/RasterToCvMat.hpp`，以供後續呼叫 `sla::apply_picture_grayscale_lut()`
- [x] 3.2 修改 `prz_header()` 中所有 `print.layer_images().size()` 呼叫，改為 `print.print_layers().size()`
- [x] 3.3 重寫 `generate_prz()` 的主迴圈：移除 `const auto &layer_images = print.layer_images()` 及 `if (layer_images.empty()) return {}` guard，改為讀取 `print.raster_params()` 並以批次大小 8（`constexpr size_t BATCH_SZ = 8`）迭代 `print.print_layers()`
- [x] 3.4 在批次迴圈中實作三階段流程：[A] 收集 batch 的 ExPolygons 並套用 `shift` 座標平移；[B] TBB `parallel_for` 呼叫 `expolygons_to_cvmat()` 及 `sla::apply_picture_grayscale_lut()`；[C] sequential 迴圈執行 `prz_layer_content()` + RLE 編碼 + `batch_mats[i].release()`
- [x] 3.5 確認最後一批（切層總數不為 BATCH_SZ 整數倍時）的邊界計算正確（`batch_end = std::min(batch_start + BATCH_SZ, N)`）

## 4. 匯出按鈕 Guard 更新

- [x] 4.1 修改 `src/slic3r/GUI/Plater.cpp` 約 line 12414 的匯出前 guard，將 `sla_print().layer_images().empty()` 改為 `!sla_print().is_step_done(slapsRasterize) || !sla_print().raster_params().has_value()`，並更新對應的錯誤訊息

## 5. GUI 預覽：Strategy A 同步單層快取

- [x] 5.1 確保 `src/slic3r/GUI/SLASlice2DCanvas.cpp` 已引入 `src/libslic3r/SLA/RasterToCvMat.hpp`，以供後續呼叫 `sla::apply_picture_grayscale_lut()`
- [x] 5.2 修改 `SLASlice2DCanvas::render()` 的「切片完成」guard：改為同時檢查 `m_print->is_step_done(slapsRasterize)` 與 `m_print->raster_params().has_value()`；任一為 false 時銷毀 texture 並呼叫 `render_vector_fallback()`
- [x] 5.3 在 `render()` 的 raster 分支中，當 `m_layer_idx != m_cached_layer` 時，讀取 `raster_params()`，取 `print_layers()[m_layer_idx].transformed_slices()`，套用 `shift` 平移，呼叫 `expolygons_to_cvmat()` 及 `sla::apply_picture_grayscale_lut()`，上傳 texture，更新 `m_cached_layer`
- [x] 5.4 確認 `cv::Mat` 在 `render()` 函式範圍內即釋放（不存入成員變數），`m_cached_layer` 快取正確防止重複計算

## 6. 編譯驗證

- [x] 6.1 全量編譯（`build_release_vs2022.bat slicer`），確認無編譯錯誤及警告
- [x] 6.2 搜尋全專案確認無殘留的 `layer_images()` 讀取呼叫（除 `SLAPrint.hpp` 宣告本身外）
  - 搜尋結果：AnycubicSLA.cpp 中的 `layer_images` 為完全獨立的本地變數，與 SLAPrint 無關
  - 已修正 3 處過時舊版註解：PhrozenPRZ.hpp:12、SLASlice2DCanvas.cpp:101、SLASlice2DCanvas.hpp:26

## 6.5 追加優化：移除 `draw_layers()` PNG 編碼

- [x] 6.5.1 移除 `rasterize()` 中的 `draw_layers()` 呼叫及相關 lambda（`lvlfn`、進度追蹤變數、SpinningMutex）
  - 原因：`SL1Archive::m_layers` 在 Phrozen Orca 中從未被讀取（`generate_prz()` 完全不使用它）
  - `slapsRasterize` 步驟從「每層 AGG 繪圖 + PNG 壓縮」簡化為「純參數計算 → 填入 `m_raster_params`」
  - 切片速度大幅提升，消除了最後的效能瓶頸
- [x] 6.5.2 重新全量編譯，確認無錯誤 *(註：額外修復了 `SLAPrintSteps.cpp` 中 `roPortrait` 模式下的影像比例失真問題)*

## 8. 串流寫檔（方向 A）

- [x] 8.1 在 `PhrozenPRZ.hpp` 中，將 `std::string generate_prz(...)` 改為 `void generate_prz(std::ostream &out, ...)`，並將 `#include <string>` 改為 `#include <iosfwd>`
- [x] 8.2 在 `PhrozenPRZ.cpp` 中新增 `#include <ostream>` 及 `write_be(std::ostream&, T)` 的 overload
- [x] 8.3 重寫 `generate_prz()` 函式體：移除 `std::string out; out.reserve(...); return out;`；改為以本地 string 呼叫 `prz_header` / `prz_layer_content` 後立即 `out.write()`；`przByte` 同樣 `out.write()` 後即消滅
- [x] 8.4 修改 `Plater.cpp::export_prz()`：改為先開啟 `std::ofstream ofs`，再呼叫 `Slic3r::generate_prz(ofs, ...)`，不再持有 `prz_data` 字串

## 9. 背景執行緒（方向 B，待實作）

- [x] 9.1 新增 `src/slic3r/GUI/Jobs/ExportPRZJob.hpp/cpp`：繼承 `Job`，於 `process(Ctl&)` 呼叫 `generate_prz(ofs, ...)`，透過 `ctl.update_status()` 每批次回報進度
- [x] 9.2 修改 `Plater::export_prz()`：將匯出邏輯封裝至 `ExportPRZJob`，透過 `PlaterWorker` 提交，解除主執行緒封鎖
- [x] 9.3 編譯並手動測試：確認匯出期間 UI 不卡死，進度條正確更新，取消功能正常

## 7. 手動功能測試（等待人類測試）

- [x] 7.1 載入 SLA 模型，執行完整切片，確認切片步驟正常完成且 `raster_params().has_value()` 為 true
- [x] 7.2 在 GUI 切層預覽中滑動 layer slider，確認每層影像正確顯示，且同一層不重複計算
- [x] 7.3 執行 PRZ 匯出，比對輸出檔案與舊版的 RLE 資料一致性（建議使用同一模型比對 byte-level 差異）
- [x] 7.4 使用 Task Manager 或 Process Monitor 監控匯出期間的記憶體使用，確認峰值在預期範圍內（≤200 MB for cv::Mat）
- [x] 7.5 測試在切片尚未完成時點擊匯出，確認正確顯示錯誤訊息
