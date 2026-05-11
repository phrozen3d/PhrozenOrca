## 1. Revert 三個 Commit，確立編譯基線

- [x] 1.1 執行 `git revert --no-commit 8d3fd8d73 b559a5df8 6572a711f`，將三個 commit 一次 revert 至暫存區
- [x] 1.2 手動刪除孤立檔案 `src/libslic3r/SLA/RleEncode.hpp`
- [x] 1.3 驗證：執行 `grep -r "RleEncode" src/`，確認零結果（無殘留引用）
- [x] 1.4 執行 `git add -u` 暫存所有 revert 變更
- [x] 1.5 驗證：執行 `build_release_vs2022.bat slicer`，確認 revert 後能乾淨編譯（零 error、零新 warning）
- [x] 1.6 提交 revert commit：message 為 `revert: remove producer-consumer pipeline and PRZ-RLE disk cache`

## 2. RasterCache 格式更新（`.prz_rle` → `.png`）

- [x] 2.1 在 `src/libslic3r/SLA/RasterCache.hpp` 中遞增 `CACHE_VERSION` 常數（使舊 `.prz_rle` 快取因版本不符而自然失效）
- [x] 2.2 更新 `write_layer(key, lid, bytes)`：temp 檔名與最終檔名改為 `layer_{lid:04d}.png`；bytes 內容為 PNG bytes，原樣寫入（temp + `std::filesystem::rename` 原子模式不變）
- [x] 2.3 更新 `read_layer(key, lid)`：讀取 `layer_{lid:04d}.png` 並回傳原始 bytes
- [x] 2.4 更新 `is_valid(key)`：sentinel 檢查改為偵測 `layer_0000.png` 是否存在（依現有 sentinel 邏輯調整）
- [x] 2.5 驗證：執行 `build_release_vs2022.bat slicer`，確認零編譯錯誤
- [x] 2.6 驗證快取失效：若磁碟存在舊版 `.prz_rle` 快取目錄，啟動 slicer 後 `is_valid()` 應回傳 `false`，日誌顯示 cache miss 並觸發重新光柵化

## 3. StatusReporter::m_st Data Race 修正

- [x] 3.1 在 `src/libslic3r/SLAPrint.hpp` 將 `StatusReporter::m_st` 型別由 `double` 改為 `std::atomic<double>`
- [x] 3.2 逐一確認 `m_st` 的所有讀寫點均兼容 atomic 語意（store/load，不使用複合賦值運算子）
- [x] 3.3 驗證：執行 `build_release_vs2022.bat slicer`，確認零編譯錯誤

## 4. Stack Blur 取代 Gaussian Blur

- [x] 4.1 在 `src/libslic3r/SLA/RasterToCvMat.cpp` 頂部加入 `#include <agg/agg_blur.h>`
- [x] 4.2 將 lines 74–79 的 `cv::GaussianBlur` 實作替換為 `agg::rendering_buffer` + `agg::pixfmt_gray8` + `agg::stack_blur_gray8(pixf, rp.blur_pixel, rp.blur_pixel)`（保留 `blur_pixel >= 2` 的 guard 條件）
- [ ] 4.3 驗證：執行 `build_release_vs2022.bat slicer`，確認零編譯錯誤（AGG 標頭路徑正確）
- [ ] 4.4 視覺驗證：在 slicer 中對含支撐結構的 SLA 模型切片，啟用 blur（例如 sp4），目視確認切層預覽中模糊效果存在且無明顯銳利偽像，品質可接受

## 5. rasterize() 全層完全平行 PNG 寫入管線

- [ ] 5.1 在 `SLAPrintSteps.cpp` 的 `rasterize()` 函式開頭宣告 CAS 進度計數器：`std::atomic<int> completed_layers{0}` 與 `std::atomic<int> last_reported_pct{-1}`
- [ ] 5.2 以 `tbb::parallel_for(tbb::blocked_range<size_t>(0, N), lambda)` 取代原有 producer-consumer loop；lambda 負責：光柵化（AGG）→ stack blur（已於 Phase 4 整合至 `expolygons_to_cvmat`）→ miniz PNG 編碼 → RasterCache write_layer → CAS 進度回報
- [ ] 5.3 在 lambda 中以 RAII wrapper 封裝 `tdefl_write_image_to_png_file_in_memory` 的回傳 buffer，確保無論成功或拋出例外，`MZ_FREE` 皆在 `write_layer` 後立即執行
- [ ] 5.4 在 lambda 末尾實作 CAS throttle（D5 片段）：`fetch_add` → 計算 pct → `compare_exchange_strong` → 成功者呼叫 `report_status(pct, L("Rasterizing layers..."))`
- [ ] 5.5 驗證：執行 `build_release_vs2022.bat slicer`，確認零編譯錯誤
- [ ] 5.6 速度驗收：對 360 層 13320×5120 模型切片，記錄 wall time（目標 ≤ 50s）；若無此規格模型，以縮放比例推估並記錄
- [ ] 5.7 OOM 驗證：切片期間以工作管理員監控記憶體，確認峰值不隨 N 線性成長，切片結束後記憶體回落
- [ ] 5.8 進度驗證：確認 UI 進度條在切片期間持續平滑更新，不出現凍結或事件洪流卡頓
- [ ] 5.9 快取內容驗證：切片完成後，在快取目錄中確認存在 N 個 `layer_XXXX.png` 檔案，且可用圖片檢視器正常開啟（確認 PNG 格式正確）

## 6. generate_prz() 快取命中路徑重寫

- [ ] 6.1 在 `src/libslic3r/Format/PhrozenPRZ.cpp` 的 `generate_prz()` 快取命中分支中宣告 `constexpr int EXPORT_BATCH = 8` 及 `tbb::task_arena arena(EXPORT_BATCH)`
- [ ] 6.2 以批次迴圈取代現有 on-demand 光柵化程式碼：以 `EXPORT_BATCH` 為步長迭代 N 層，每批次透過 `arena.execute([&]{ tbb::parallel_for(...) })` 平行執行：`RasterCache::read_layer` → `cv::imdecode(buf, cv::IMREAD_GRAYSCALE)` → RLE 編碼 → 結果暫存至 `rle_results[i]`
- [ ] 6.3 批次平行階段完成後，以循序迴圈 append `rle_results[i]` 至 `std::ostream &out`，每層寫完後呼叫 `rle_results[i].clear()` 立即釋放（保序 + OOM 防護）
- [ ] 6.4 確認例外傳播路徑：`read_layer` 拋出時例外傳至 `ExportPRZJob` 捕捉，顯示錯誤訊息並中止，不產生損壞的 PRZ 檔案
- [ ] 6.5 驗證：執行 `build_release_vs2022.bat slicer`，確認零編譯錯誤
- [ ] 6.6 輸出正確性驗證：以重構前的參考 PRZ 檔案為基準，比對重構後匯出的 PRZ 逐層 RLE 資料是否一致（hex diff 或 layer-by-layer 目視比對）
- [ ] 6.7 記憶體驗證：匯出 360 層 13320×5120 PRZ 期間，確認記憶體峰值 ≤ 650 MB
- [ ] 6.8 取消功能驗證：匯出進行中點擊取消，確認作業中止且不留下損壞的 PRZ 檔案於磁碟

## 7. 最終整合驗收與提交

- [ ] 7.1 端對端流程：首次切片（wall time ≤ 50s）→ 同模型再次切片（應 < 2s 快取命中，不重新光柵化）→ 匯出 PRZ → 確認 PRZ 可正常使用
- [ ] 7.2 執行 SLA 測試套件：`cd build && ctest -R sla_print --output-on-failure`，確認零失敗
- [ ] 7.3 死碼清理驗證：搜尋 `RleEncode`、舊 producer-consumer 符號，確認已完全移除，無殘留
- [ ] 7.4 提交 Phase 2–6 所有變更：以單一 commit 提交，message 包含實測速度基準數據（例如 `feat: parallel PNG rasterize pipeline, 120s → Xs`）
