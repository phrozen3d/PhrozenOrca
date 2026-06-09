## 1. RasterCache thumb API 與版本

- [x] 1.1 在 [RasterCache.hpp](src/libslic3r/SLA/RasterCache.hpp) 新增 `write_thumb(const RasterCacheKey&, size_t lid, const unsigned char* data, size_t size)`（與 `vector` 多載），檔名規則 `layer_{lid:04d}_preview.rle`；並宣告輕量灰階 RLE 編解碼器 `rle_encode_gray` / `rle_decode_gray`
- [x] 1.2 在 [RasterCache.cpp](src/libslic3r/SLA/RasterCache.cpp) 實作 `write_thumb`：比照 `write_layer`，開檔/寫檔失敗即 `throw std::runtime_error`（存活性綁定的關鍵）；並實作 `rle_encode_gray`（填入重用 `out`）/ `rle_decode_gray`（`[u32 w][u32 h]` + `(value,run)` 對）
- [x] 1.3 新增 `read_thumb(const RasterCacheKey&, size_t lid)`（回傳 RLE bytes，缺檔回傳空 vector 供 UI 退向量），檔名同 1.1
- [x] 1.4 遞增 `CACHE_VERSION`（5 → 6），並在註解標明「新增 thumb 預覽快取」
- [x] 1.5 **驗證（編譯）**：`build_release_vs2022.bat slicer` 編譯通過，確認 RasterCache 新 API 無連結錯誤（使用者於 VS2022 確認通過）
- [x] 1.6 **驗證（測試）**：已新增 `tests/sla_print/test_raster_cache.cpp` 並註冊 CMake，覆蓋 round-trip / 空值 / 例外三路徑；（環境未配置測試執行檔，由人工判定 Pass 免除執行）

## 2. TBB worker 內 thumb 擷取（rasterize Phase 5）

- [x] 2.1 在 [SLAPrintSteps.cpp](src/libslic3r/SLAPrintSteps.cpp) 的 `TLSData`（約 line 1526-1531）新增 `cv::Mat thumb;` 與 `std::vector<uchar> thumb_rle;` 兩個跨層重用欄位
- [x] 2.2 在 worker 迴圈 `apply_picture_grayscale_lut(mat)`（line 1559）之後、`cv::rotate(...)`（line 1566）之前，插入 thumb 擷取：計算 `scale = 4096 / max(mat.cols, mat.rows)`，`scale < 1` 時 `cv::resize(mat, tls.thumb, dsize, 0, 0, cv::INTER_AREA)`，否則直接以 `mat` 為來源（panel 方位，等比，< 16M px）
- [x] 2.3 `RasterCache::rle_encode_gray(thumb_src->data, cols, rows, tls.thumb_rle)` 後呼叫 `RasterCache::write_thumb(cache_key, lid, tls.thumb_rle)`（移除 `cv::imencode`，避開 imgcodecs/libjpeg-turbo）
- [x] 2.4 確認全解析度 `mat` / `mat_rotated` 維持既有 TLS 重用、**不**新增每層銷毀；thumb/thumb_rle 僅覆寫不釋放（守 OOM 與 CRT heap lock 契約）
- [x] 2.5 確認 `mark_complete()`（line 1678）僅在 `parallel_for` 正常結束後呼叫；thumb 寫失敗的例外能正確中止整個 rasterize 並使快取 invalid
- [x] 2.6 **驗證（編譯）**：`build_release_vs2022.bat slicer` 編譯通過（使用者確認，LNK2005 已消除）
- [x] 2.7 **驗證（行為/磁碟）**：以開啟 `gamma>0` 或 `blur` 的 profile 切片一個模型，檢查 `temp/phrozen_sla_cache/<hash>/` 內每層同時存在 `layer_XXXX.rle` 與 `layer_XXXX_preview.rle`，且 `cache_complete` 存在（使用者確認）
- [x] 2.8 **驗證（bit-perfect 不回歸）**：匯出 PRZ，與本變更前版本逐 byte 比對 `.rle`/PRZ 輸出一致（thumb 為旁路，不得影響列印資料）（使用者確認）

## 3. UI 預覽改為只讀 thumb 快取

- [x] 3.1 在 [SLASlice2DCanvas.hpp] 新增成員 `std::optional<sla::RasterCacheKey> m_cache_key;`（含 `<optional>` 與 `RasterCache.hpp` include）
- [x] 3.2 `set_sla_print()` 與 `reset_print()` 內 `m_cache_key.reset()`（標記失效，惰性於 render 重算 — 採選項 A）
- [x] 3.3 重寫 `render()` 的點陣分支：以 `m_cache_key` + `m_layer_idx` 讀 `_preview.rle`，`RasterCache::rle_decode_gray` 解回 `cv::Mat` 後 `upload_grayscale_texture`（移除 `cv::imdecode`）；**已移除** on-demand `expolygons_to_cvmat` 與 `apply_picture_grayscale_lut`
- [x] 3.4 thumb 讀取失敗/空 → 不上傳 → `m_tex_id` 維持 0 → `drew=false` → `render_vector_fallback()`；此路徑絕不呼叫 `expolygons_to_cvmat`
- [x] 3.5 guard：點陣分支需同時滿足 `is_step_done(slapsRasterize)`、`raster_params().has_value()` 與 `m_cache_key.has_value()`；保留 `m_cached_layer` 單層去重
- [x] 3.6 確認 `render()`（除 emptiness-guarded 惰性一次外）/ `on_gl_mouse_motion()` 路徑中無 `compute_key()` 每幀呼叫（節流契約）
- [x] 3.7 **驗證（編譯）**：`build_release_vs2022.bat slicer` 編譯通過（使用者確認）
- [x] 3.8 **驗證（視覺：不卡頓）**：開 `gamma>0` + `blur` 切片後快速拖曳 layer bar，確認預覽流暢、無卡頓（使用者確認）
- [x] 3.10 **修正（預覽方位，動態 UV）**：3.9 兩輪攔截揭露 portrait 機型方位隨 `display_mirror_x` 不同而顧此失彼（16K mx=0 vs 8K mx=1）。**還原寫死 UV 常數**,於 `render_texture_letterbox` 依 `m_print->raster_params()->trafo.mirror_x/mirror_y` 動態組 UV：portrait base=rot90CW,`trafo.mirror_x` → 垂直翻轉、`!trafo.mirror_y` → 水平翻轉;landscape base=`FullTextureUVs` 對稱處理。預覽一律 canonical bed 方位、PRZ 不受影響。詳見 design.md D7
- [x] 3.9 **驗證（視覺：不畫錯）**：Revo 16K / Mega 8K S / Mega 8K V2 全機型總複驗通過——預覽完全無鏡射、方位與 3D bed 一致、無偏暗（使用者確認）

## 4. 邊際案例與整體回歸驗證

- [x] 4.1 **驗證（存活性/退向量）**：程式碼路徑確認——`read_thumb` 缺檔回傳空 → `render()` 不上傳 → `m_tex_id` 維持 0 → `render_vector_fallback()`，全程不呼叫 `expolygons_to_cvmat`（[SLASlice2DCanvas.cpp:830-834](src/slic3r/GUI/SLASlice2DCanvas.cpp)）。（執行期手動刪檔觀測為使用者選擇性確認）
- [x] 4.2 **驗證（快取整體失效）**：程式碼路徑確認——`write_thumb` 開檔/寫檔失敗 `throw std::runtime_error`（[RasterCache.cpp:109,113](src/libslic3r/SLA/RasterCache.cpp)）→ 例外傳出 `parallel_for` → `mark_complete()`（[SLAPrintSteps.cpp:1711](src/libslic3r/SLAPrintSteps.cpp)，位於迴圈之後）不被呼叫 → `is_valid()` 為 false
- [x] 4.3 **驗證（舊快取失效）**：`CACHE_VERSION = 6`（[RasterCache.hpp:94](src/libslic3r/SLA/RasterCache.hpp)）併入 `compute_key` CRC32 → 舊版（無 thumb，VER=5）hash 不符、必不命中
- [x] 4.4 **驗證（OOM 不回歸）**：程式碼確認——`thumb`/`thumb_rle` 納入 `TLSData` 跨層重用；全解析度 `mat`/`mat_rotated` 維持既有 TLS 不每層銷毀；無新增每層 65MB 配置（執行期記憶體 profiler 觀測為使用者選擇性確認）
- [x] 4.5 **驗證（測試套件）**：`ctest`（含 `sla_print_tests` 的 RasterCache 測試）——環境未配置測試執行檔，經人類實機全機型總複驗全綠，特許免除並判定 Pass
- [x] 4.6 **驗證（OpenSpec）**：`openspec validate sla-preview-thumb-cache` → **Change is valid**（已執行通過）