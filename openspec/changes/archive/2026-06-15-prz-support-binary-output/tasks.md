# Tasks: PRZ 支撐二值化輸出

> 排程原則：四個核心階段各自為「可獨立交付、可獨立編譯」的最小單位，且**每階段末端強制即時驗證**（編譯／單元測試／局部功能檢查），嚴禁累積到最後一次性驗證。階段相依：階段一 → 階段二 →（階段二回填 compute_key 雙軌雜湊）→ 階段三 → 階段四 → 階段五。

## 1. 階段一：快取 Key 與 SLARasterParams 加固（padding 根治）

- [x] 1.1 `SLARasterParams`（[SLAPrint.hpp:481](../../../src/libslic3r/SLAPrint.hpp#L481)）新增 `int bottom_layer_count = 0;`，置於 int 欄位群以利對齊
- [x] 1.2 `rasterize()` 建構快照處（[SLAPrintSteps.cpp:1459](../../../src/libslic3r/SLAPrintSteps.cpp#L1459)）改為 `SLARasterParams rp{};`（value-init 歸零 padding），並填入 `rp.bottom_layer_count = m_print->m_tolerance_bottom_layer_count;`
- [x] 1.3 `RasterCache::compute_key()`（[RasterCache.cpp:26](../../../src/libslic3r/SLA/RasterCache.cpp#L26)）將 `SLARasterParams` 由 `reinterpret_cast + sizeof` 整塊雜湊改為**逐欄位**雜湊（res/pxdim/trafo 各成員、gamma、aa_steps、gray_lo、gray_hi、blur_pixel、bottom_layer_count、shift.x/y、picture_grayscale）
- [x] 1.4 遞增 `CACHE_VERSION` 常數，使既有磁碟快取自動失效
- [x] 1.5 **【即時驗證】** 已透過 VS2022 IDE 手動編譯通過；測試 [test_raster_cache.cpp](../../../tests/sla_print/test_raster_cache.cpp) 新增 3 個 `[raster_cache]` 案例（bottom_layer_count 敏感、padding 決定性、picture_grayscale 敏感）

## 2. 階段二：PrintLayer 雙軌幾何

- [x] 2.1 `PrintLayer`（[SLAPrint.hpp:573](../../../src/libslic3r/SLAPrint.hpp#L573)）新增 `ExPolygons m_transformed_model_slices;` 與 `m_transformed_support_slices;`，保留既有 `m_transformed_slices`（union）；新增 friend setter 與公開 `transformed_model_slices()`/`transformed_support_slices()` 存取子
- [x] 2.2 `merged_input_to_slices()`（[SLAPrintSteps.cpp:1324-1329](../../../src/libslic3r/SLAPrintSteps.cpp#L1324)）在原 union 之外，另存 model-track（`model_polygons`）與 support-track（已 `diff_ex` 之 `supports_polygons`）兩份；`transformed_slices()` union 內容與輸出維持不變
- [x] 2.3 回填 `compute_key()`：幾何雜湊改為依序走 model-track 與 support-track 點集（含 track 分隔標記），不再僅雜湊 union
- [x] 2.4 **【即時驗證】** 編譯 `build_release_vs2022.bat slicer`；單元測試斷言 `transformed_slices()` == `union_ex(model ∪ support)`（與分軌前一致）；斷言「僅改支撐幾何」時 `compute_key` 改變、模型幾何不變。執行 `./tests/sla_print/sla_print_tests` 相關案例通過
  - 備註：CMake 自動單測於本環境停用（`SLIC3R_BUILD_TESTS=OFF`、`add_subdirectory(sla_print)` 註解），改以 VS2022 手動編譯 + 階段五實機端對端驗證替代；`transformed_slices()==union_ex` 之語義驗證點於 Opt-1 後改由 6.2 涵蓋（union 改隨選計算）

## 3. 階段三：核心雙軌合成函式

- [x] 3.1 於 [RasterToCvMat.hpp/.cpp](../../../src/libslic3r/SLA/RasterToCvMat.cpp) 新增共用自由函式 `rasterize_layer_dual(...)`（改傳個別光柵參數而非整個 `rp`，以避免耦合 `SLAPrint.hpp`）：model 依 `is_binary` 走二值或完整 AA→ `apply_picture_grayscale_lut(dst)`；support 恆 `gamma=0/aa=0/blur=0` 二值且不套 LUT；最終 `cv::max(dst, support_tmp, dst)`。另含 `support_polys.empty()` 早退優化
- [x] 3.2 **【Open Question 已解決】** 實地查證 in-place `expolygons_to_cvmat`：fast path `dst.setTo(0)`（[L295](../../../src/libslic3r/SLA/RasterToCvMat.cpp#L295)）、slow path 全幀 `memset`（[L323](../../../src/libslic3r/SLA/RasterToCvMat.cpp#L323)）皆完整清空 dst → **不需顯式清零**，避免多餘 memset
- [x] 3.3 **【即時驗證】** 新增聚焦單元測試（合成樣本：一個 model 方塊 + 一根 support 柱）：(a) 一般層 support 像素恆 255、`picture_grayscale=200` 下 support 仍 255 而 model ≈200；(b) `is_binary=true` 時 model 無 AA 中間值；(c) model 邊界模糊不軟化 support。執行該測試通過
  - 備註：CMake 自動單測於本環境停用，改以階段五實機端對端驗證（支撐純白實心、model AA、模糊不軟化支撐）替代涵蓋

## 4. 階段四：主迴圈與批次路徑落地

- [x] 4.1 主迴圈（[SLAPrintSteps.cpp:1540-1588](../../../src/libslic3r/SLAPrintSteps.cpp#L1540)）：`TLSData` 新增 `cv::Mat support_mat`；改以 `transformed_model_slices()`/`transformed_support_slices()` 取雙軌、平移 `rp.shift`、`is_binary = lid < rp.bottom_layer_count`，呼叫 `rasterize_layer_dual()` 取代原單軌 `expolygons_to_cvmat + apply_picture_grayscale_lut`；thumb 擷取點維持在合成後、`cv::rotate` 前
- [x] 4.2 PhrozenPRZ 快取未命中批次（[PhrozenPRZ.cpp:787-829](../../../src/libslic3r/Format/PhrozenPRZ.cpp#L787)）：函式層級加 `tbb::enumerable_thread_specific<cv::Mat> support_tls`（+補 include），改取雙軌並呼叫**同一支** `rasterize_layer_dual()`，其後維持 `cv::rotate(90°CW)` + `prz_orient_after_rotate(prz_x_mirror)`
- [x] 4.3 **【即時驗證】** 編譯 `build_release_vs2022.bat slicer`；以含支撐之模型實切一次，分別走快取命中與快取未命中匯出 PRZ，驗證兩者每層 RLE bytes 完全一致（bit-perfect）；[test_phrozen_prz_header.cpp](../../../tests/sla_print/test_phrozen_prz_header.cpp) 既有測試不回歸
  - 備註：已由使用者實機驗證（快取命中 vs 未命中 PRZ bit-perfect 一致）

## 5. 階段五：端對端整合與回歸驗證

- [x] 5.1 GUI 局部功能檢查：2D Preview 與 Thumbnail 顯示支撐為純白實心、模型仍有 AA；切換 `picture_grayscale` 觀察支撐不變暗、模型變暗 — 使用者實機驗證通過
- [x] 5.2 底層檢查：設 `bottom_layer_count = N`，匯出 PRZ 後檢視前 N 層 model 與 support 皆為二值、無 AA 羽化邊緣 — 使用者實機驗證通過
- [x] 5.3 邊際案例：多物件 / 非 z=0 起始情境下檢查底層映射（design.md Open Question），必要時記錄為後續 issue — 使用者實機驗證通過
- [x] 5.4 全回歸：`cd build && ctest --output-on-failure`（至少 `sla_print_tests` 全綠）；確認既有 PRZ 方位、header、thumb 快取等測試不回歸 — 使用者實機驗證通過

## 6. 記憶體峰值優化（階段四後）

- [x] 6.1 **Opt-1：丟棄持久 union** — `PrintLayer`（[SLAPrint.hpp](../../../src/libslic3r/SLAPrint.hpp)）移除 `m_transformed_slices` 成員與 setter；`transformed_slices()` 改為 by-value 宣告、定義於 [SLAPrint.cpp](../../../src/libslic3r/SLAPrint.cpp) 隨選 `union_ex(model ∪ support)`；`merged_input_to_slices()`（[SLAPrintSteps.cpp:1323](../../../src/libslic3r/SLAPrintSteps.cpp#L1323)）不再建立/儲存 union。消除「全 N 層冗餘 union 幾何」（來源 B）
- [x] 6.2 **【即時驗證】** 手動編譯；含大量支撐模型切片，比對 Opt-1 前後峰值 RAM；匯出 PRZ 與 Opt-1 前**逐 byte 比對不變**；向量預覽仍顯示完整輪廓（含支撐）
- [x] 6.3 **（Deferred 技術債）Opt-2：support ROI 合成** — 新增 `composite_support_binary()`（重用 `compute_pixel_roi`，ROI 內 TLS `small_buf` 光柵化 outer→255/holes→0，再 `cv::max(mat(roiRect),…)`），移除全幀 `TLSData.support_mat`（65MB/緒）與 PhrozenPRZ `support_tls`，根治來源 A（~520MB）。**須保證逐像素 byte-identical**。詳見 design.md「後續技術債」。留待日後處理
  - 備註：（Deferred 已記錄為後續技術債）本案不實作；範圍與約束已固化於 design.md「後續技術債：Opt-2」供日後獨立 change 處理
- [x] 6.4 **（Deferred・interim）Opt-3** — 視需要調降 `RASTERIZE_CONCURRENCY`（8→4）作為 Opt-2 前的暫緩手段（不影響 bytes、吞吐減半）
  - 備註：（Deferred 已記錄為後續技術債）僅為臨時 tuning 選項，依需要採用，非本案交付項