## Why

SLA 切片完成後，`rasterize()` 步驟會將所有切層的光柵化影像（`cv::Mat`，CV_8UC1）一次性存入 `m_layer_images`，並在整個 session 期間常駐記憶體。以 Mega 8K 印表機（6480×3600 解析度）切片 100 層為例，峰值記憶體佔用超過 2.3 GB，且 Pipeline 2 的執行時間使匯出按鈕的等待時間翻倍。需要在不犧牲多核心 TBB 效能的前提下，將記憶體峰值降低 90% 以上。

手動功能測試後進一步發現兩個匯出期問題：(1) `generate_prz()` 將所有層的 RLE 資料累積於單一 `std::string`（峰值 ~1 GB），完成後才整批寫入磁碟；(2) 整個匯出流程在 UI 主執行緒同步執行，造成介面凍結直至完成。

## What Changes

- **新增** `SLARasterParams` struct（值型別，POD-safe）於 `SLAPrint.hpp`，用以封裝光柵化所需的所有參數快照
- **新增** `std::optional<SLARasterParams> m_raster_params` 於 `SLAPrint`，並提供公開 accessor `raster_params()`
- **移除** `rasterize()` 中的 Pipeline 2（將全部層存入 `m_layer_images` 的邏輯），改為只填入 `m_raster_params`
- **移除** `m_layer_images` 的填充邏輯；`m_layer_images` 改為永遠為空向量（最終可移除宣告）
- **移除** `rasterize()` 中的 Pipeline 1（`draw_layers()` 呼叫、`lvlfn` lambda、`PNGRasterEncoder` 呼叫）— 因查明 Phrozen Orca 所有匯出均透過 `generate_prz()` 完成，`SL1Archive::m_layers` 永遠不會被讀取，Pipeline 1 的 AGG 繪圖與 PNG 壓縮屬於無效工作
- **重寫** `generate_prz()` 為批次並行（Batched On-Demand）模式：每批次 TBB 平行光柵化 → sequential RLE 編碼 → 即時 `release()`
- **修改** `prz_header()` 中所有 `layer_images().size()` 呼叫，改為 `print_layers().size()`
- **修改** `SLASlice2DCanvas::render()` 改為同步單層快取（Strategy A）：讀 `print_layers()` 及 `raster_params()`，呼叫 `expolygons_to_cvmat()`，以 `m_cached_layer` 作索引快取
- **修改** `Plater.cpp` 匯出按鈕 guard，改為檢查 `is_step_done(slapsRasterize) && raster_params().has_value()`
- **修改** `generate_prz()` API：從回傳 `std::string` 改為接受 `std::ostream &out` 直接串流寫檔，消除匯出期間的 ~1 GB 記憶體積壓（方向 A）
- **修改** `Plater::export_prz()`：改為先開啟 `std::ofstream`，再呼叫 `generate_prz(ofs, ...)`，不再持有 `prz_data` 字串副本（方向 A）
- **待實作** `ExportPRZJob`：將匯出邏輯封裝進背景 Job，透過 `PlaterWorker` 提交，徹底解除 UI 主執行緒封鎖（方向 B）

## Capabilities

### New Capabilities

- `sla-raster-params-snapshot`: 封裝 SLA 光柵化參數的值型別快照結構，使得任意時間點、任意呼叫端均可重現任一切層的 `cv::Mat`，無需存取設定物件
- `sla-on-demand-prz-export`: PRZ 匯出時採批次並行策略，僅在編碼前即時光柵化當前批次的切層，編碼完即釋放，不再持有全部影像
- `sla-on-demand-preview`: GUI 切層預覽採同步單層快取策略，layer index 切換時即時光柵化該層，顯示後釋放（以 `m_cached_layer` 避免重複計算）

### Modified Capabilities

（無現有 spec 層級需求變更）

## Impact

**修改的核心檔案：**
- `src/libslic3r/SLAPrint.hpp` — 新增 `SLARasterParams` struct 及 `m_raster_params` 成員
- `src/libslic3r/SLAPrint.cpp` — `invalidate_step(slapsRasterize)` 時重設 `m_raster_params`
- `src/libslic3r/SLAPrintSteps.cpp` — `rasterize()` 移除 Pipeline 2，填入 `m_raster_params`
- `src/libslic3r/Format/PhrozenPRZ.cpp` — `generate_prz()` 及 `prz_header()` 改用 on-demand 邏輯；`generate_prz()` 進一步改為串流寫檔
- `src/slic3r/GUI/SLASlice2DCanvas.cpp` — `render()` 改為 Strategy A 單層快取
- `src/slic3r/GUI/Plater.cpp` — 匯出按鈕 guard 條件更新

**依賴：**
- `src/libslic3r/SLA/RasterToCvMat.hpp/cpp` — `expolygons_to_cvmat()`（單層）已存在，無需新增 API
- TBB `parallel_for` 於批次光柵化中繼續使用

**追加優化（架構迭代）：**
- `rasterize()` 中的 Pipeline 1（`draw_layers()` → `SL1Archive::m_layers` PNG 編碼）已一併移除。`slapsRasterize` 步驟從「每層 AGG 繪圖 + PNG 壓縮」簡化為純參數計算，近乎瞬間完成，徹底消除了切片期間的效能瓶頸

**不受影響：**
- `m_printer_input`（`std::vector<PrintLayer>`）仍持有 ExPolygon 多邊形資料，為 on-demand 光柵化的資料來源
- 現有 SLA 支撐計算、切片、預覽等步驟不受影響
