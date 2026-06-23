<!--
驗證策略（v3 — VS 2022 IDE 獨立沙盒 Sandbox，取代大專案 CMake 整合）

動機演進：清除大專案 root CMake 快取後，配置管線卡在缺乏全域 Boost 1.83.0，
整條 configure 被阻擋。故「在大專案內掛 micro-target」也走不通。最高指導原則：
**徹底繞過大專案編譯系統**，改用一個完全獨立、只看 OpenCV 的沙盒。

本變更的核心邏輯（融合編碼器）不依賴任何 slicer 型別，只需 OpenCV。沙盒位於
`src/libslic3r/Format/prz_pure_sandbox/`，內含一個自帶 `project()`、不 include 專案
根目錄的極簡 `CMakeLists.txt` + `CMakePresets.json`，直接編 `../PhrozenPRZRle.cpp`
的 `#ifdef PRZ_RLE_TEST` 內聯測試，連結 deps 安裝樹的 OpenCV 靜態庫。

依賴鐵律：`PhrozenPRZRle.{hpp,cpp}` 依賴邊界僅 `<opencv2/core.hpp>` +
`<opencv2/imgproc.hpp>`，**嚴禁** include `PhrozenPRZOrient.hpp` / `PrintConfig.hpp`
（否則拖入 libslic3r，破壞沙盒獨立性）。翻轉以 `cv::flip(mat, mat, code)` 就地完成，
`code = final_x_mirror ? 1 : 0` 的 byte 語義單一來源即本 TU；呼叫端只傳 bool。
（此精煉了 design.md D3 的依賴邊界：flip 不經 PhrozenPRZOrient.hpp，但語義等價。）

驗證鐵律（全程適用）：
- 每個實作子任務後緊跟一個【驗證】子任務，當下即建、即跑、即看，禁止「全做完才驗證」。
- 所有【驗證】= 操作者在 VS 2022 IDE 內滑鼠一鍵建置/執行/偵錯 → 將 IDE 視窗噴出的
  PASS/FAIL（含層號 / K / 首個分歧 byte offset）敲回給 Claude Code 後才推進。
  禁止無法直觀監控的背景黑箱。
- `main()` 跑完所有 suite 後 SHALL 印出單行 `OVERALL: PASS` 或 `OVERALL: FAIL`，
  並以對應 exit code 結束（PASS=0、FAIL=非0）。

★ 沙盒驗證流程（VS 2022「開啟資料夾」一鍵法）：

  1. VS 2022 →「檔案 → 開啟 → 資料夾…」選 `src/libslic3r/Format/prz_pure_sandbox/`。
  2. VS 自動讀 `CMakePresets.json`，選組態 `sandbox-x64`（x64 / RelWithDebInfo / MD）。
  3. 工具列選啟動項 `prz_rle_test.exe`，按「► 執行(不偵錯)」或 F5（可中斷點偵錯）。
  4. 把「輸出 / 偵錯主控台」視窗印出的內容（應含 `OVERALL: PASS`）敲回給 Claude Code。

  備註：沙盒只連 OpenCV（deps 安裝樹 vc17 staticlib），不碰 libslic3r/Boost；
  靜態 OpenCV 為 /MD 建置，沙盒已強制 `MultiThreadedDLL` 全組態套用，避免 LNK2038。
  若 deps 路徑不同，於 CMake 快取覆寫 `OPENCV_INSTALL_DIR` 即可。
-->

## 1. 廢棄 Catch2 路徑、建立 OpenCV-only 內聯測試地基

- [x] 1.1 移除 `tests/sla_print/test_prz_band_rle.cpp`，並還原 `tests/sla_print/CMakeLists.txt` 的 `test_prz_band_rle.cpp` 掛載行（清除已廢棄的 Catch2 重型方案殘留）。
- [x] 1.2 新增 `src/libslic3r/Format/PhrozenPRZRle.hpp`：依賴邊界僅 OpenCV core；宣告 `std::size_t prz_encode_layer_banded(const cv::Mat&, bool final_x_mirror, int band_cols, std::vector<char>& out);` 與測試預言 `std::size_t prz_encode_layer_reference(const cv::Mat&, bool final_x_mirror, std::vector<char>& out);`。
- [x] 1.3 新增 `src/libslic3r/Format/PhrozenPRZRle.cpp`：僅 `#include <opencv2/core.hpp> <opencv2/imgproc.hpp>` 與自身 hpp；於檔末以 `#ifdef PRZ_RLE_TEST` 包裹獨立 `main()` 骨架（印 `scaffold ok` 後 `OVERALL: PASS`、return 0）。
- [x] 1.4 在 `#ifdef PRZ_RLE_TEST` 區塊內移植測試輔助：五大圖樣產生器 `make_portrait`（全黑／全白／隨機／水平漸層／每條帶相異）、`first_diff(a,b)`（首個分歧 offset，含長度差，無分歧回 -1）、`dump_diff(label,lid,K,a,b)`（組出層號／K／offset／兩側 byte 的字串）、以及 `run_suite` 彙總框架（累計 PASS/FAIL、印 `OVERALL`）。
- [x] 1.5 建立獨立沙盒 `src/libslic3r/Format/prz_pure_sandbox/`：自帶 `project()`、不 include 專案根目錄的極簡 `CMakeLists.txt`（編 `../PhrozenPRZRle.cpp` + `-DPRZ_RLE_TEST`，include = deps OpenCV install + repo `src/`，手連 `opencv_world460/ippicvmt/ippiw/libjpeg-turbo/libpng/libtiff`，強制 `MultiThreadedDLL` 全組態套用），及 `CMakePresets.json`（組態 `sandbox-x64`：VS2022 / x64 / RelWithDebInfo）。同時撤除上一輪加在 `tests/sla_print/CMakeLists.txt` 的大專案 micro-target（root config 被 Boost 阻擋而報廢）。
- [x] 1.6 【驗證】操作者依 ★沙盒驗證流程：VS2022「開啟資料夾」沙盒目錄 → 選 `sandbox-x64` → 一鍵執行 `prz_rle_test`；把 IDE 主控台輸出（應為 `scaffold ok` + helper 自檢 + `OVERALL: PASS`）敲回。Claude Code 收到 PASS 後才推進。✅ 已驗證：`scaffold ok` / `checks: 9 passed, 0 failed` / `OVERALL: PASS`。

## 2. RLE 常數與「全幀參考預言」（byte-identity 基準）

- [x] 2.1 在 `PhrozenPRZRle.cpp` 內收 RLE 常數（`RLE_BLACK/WHITE/GRAY`、`RLE_BYTE_NUMBER`、`RLE_CONT_BOUND`、`RLE_BOUND_0`）與 `flush_run` 邏輯（→ `prz_flush_run`，匿名 namespace），逐字搬移自 [SLAPrintSteps.cpp:1512-1699](../../../src/libslic3r/SLAPrintSteps.cpp#L1512-L1699)。
- [x] 2.2 實作 `prz_encode_layer_reference()` = 現行全幀路徑：`cv::rotate(ROTATE_90_CW)` → `cv::flip(code = final_x_mirror?1:0)` → 全幀 row-major 線性 RLE（`0x55` head + runs + checksum，與舊路徑逐 byte 對齊）。
- [x] 2.3 在 `#ifdef` 區塊加測試 `test_reference_vs_oracle()`：對五大圖樣 × 兩家族，斷言 `prz_encode_layer_reference()` 與「區塊內就地複刻的獨立 oracle（改用可移植位移、結構異於生產碼）」逐 byte 相同（鎖定預言本身正確）。
- [x] 2.4 【驗證】操作者重跑 ★沙盒驗證流程；回報終端機 `OVERALL: PASS/FAIL`（FAIL 時附 `dump_diff` offset）。✅ 已驗證：`checks: 19 passed, 0 failed` / `OVERALL: PASS`。

## 3. RLE 狀態機 `PrzRleEncoder`（feed/finish）

- [x] 3.1 實作 `PrzRleEncoder{ begin(); feed(const uchar*, int); finish(); }`（`namespace Slic3r`，使用 `prz_flush_run`），`cur/count/sum/pos/started` 跨 `feed` 連續攜帶；`begin()` 寫 `0x55`、`finish()` flush 末段 run + checksum + 回傳總位元組數；`out` 跨層重用、steady-state 零 realloc。
- [x] 3.2 測試 `test_encoder_one_shot()`：對五大圖樣 × 兩家族，將旋轉+翻轉後的 landscape buffer 一次 `feed`，斷言與 `prz_encode_layer_reference()` 逐 byte 相同。
- [x] 3.3 測試 `test_encoder_split()`：同一 buffer 以「一次餵」vs「切多段（step ∈ {1, 7, 13, 257, 1000}，涵蓋長度 1／質數／跨同色 run 邊界）」編碼，斷言逐 byte 相同（鎖定跨界 run-state 連續）。
- [x] 3.4 【驗證】操作者重跑 ★沙盒驗證流程；回報 `OVERALL`（FAIL 時 offset 指出 run 被切斷處）。✅ 已驗證：`checks: 79 passed, 0 failed` / `OVERALL: PASS`。

## 4. 條帶旋轉 + 融合編碼（先正序 / 水平家族 TRUE）

- [x] 4.1 實作 `prz_encode_layer_banded()`：`M=rows`、`N=cols`，正序迴圈 `c0 += K`、`k=min(K, N-c0)`，取 `slab=portrait(Range::all(), Range(c0,c0+k))`、`cv::rotate(slab, tile, ROTATE_90_CW)`、`cv::flip(tile, tile, 1)`、`enc.feed(tile.data, k*M)`（`PrzRleEncoder` 跨條帶攜帶）；先**僅支援 `final_x_mirror=true`**，其餘以 `assert` 攔截。
- [x] 4.2 測試 `test_banded_true_vs_reference()`：五大圖樣、`final_x_mirror=true`、`K=256`、尺寸 M=300×N=700（N>K → 多條帶 + 殘餘條帶 256/256/188），斷言 banded == reference 逐 byte 相同。已接入 `main()`。
- [x] 4.3 【驗證】操作者重跑 ★沙盒驗證流程；回報 `OVERALL`（FAIL 時 `dump_diff` 印 offset/K）。✅ 已驗證：`checks: 84 passed, 0 failed` / `OVERALL: PASS`。

## 5. 對稱翻轉規則：垂直家族 FALSE（逆序 + 局部 code 0）

- [x] 5.1 引入單一 bool：`descending = !final_x_mirror`、`flip_code = final_x_mirror?1:0`；外層以 `band = descending ? n_bands-1-b : b` 反轉排程指標，每個 tile 一律 `cv::flip(tile, tile, flip_code)`；移除 4.1 的 `assert`，開放 FALSE 家族（逆序下殘餘條帶最先處理，無特例）。
- [x] 5.2 測試 `test_banded_false_vs_reference()`：五大圖樣、`final_x_mirror=false`、`K=256`、M=300×N=700（多條帶 + 殘餘條帶最先），斷言 banded == reference 逐 byte 相同。已接入 `main()`。
- [x] 5.3 測試 `test_banded_false_not_block_permutation()`（負向防呆）：以 Random／HGradient／DistinctPerBand（具條帶內列變化）斷言 FALSE 輸出 == 全圖垂直翻轉參考、**!=** `wrong_block_permutation_encode()`（僅外層逆序、tile 不翻轉），且該錯誤實作 **!=** reference（證明會被攔截）。已接入 `main()`。
- [x] 5.4 【驗證】操作者重跑 ★沙盒驗證流程；回報 `OVERALL`（FAIL 時 offset 立即定位區段置換）。✅ 已驗證：`checks: 98 passed, 0 failed` / `OVERALL: PASS`。

## 6. K-不變性殺手掃描（核心不變式）

- [x] 6.1 測試 `test_k_invariance()`：五大圖樣 × 兩家族，尺寸 M=300×N=700，`K ∈ {1, 7, 256, N, N+1}` 全部與 `K=1` 基準逐 byte 相同；FAIL 時印首個分歧的 K 與 offset。已接入 `main()`。
- [x] 6.2 【驗證】操作者重跑 ★沙盒驗證流程；回報 `OVERALL`（此為偵測 K>1 沉默錯誤的最終關卡）。✅ 已驗證：`checks: 148 passed, 0 failed` / `OVERALL: PASS`。

## 7. TLS tile 緩衝與殘餘條帶 ROI view（零-malloc）

- [x] 7.1 tile 緩衝改為函式內 `thread_local cv::Mat tile_pool`，一次配置 `K×M`（cols 嚴格 == M 以保 ROI 連續；僅在 M 改變或 K 變大時重配）；每條帶以頂部 k 列連續 ROI `tile_pool(cv::Rect(0,0,M,k))` 作 `cv::rotate` dst（尺寸吻合 → transpose 不 realloc），full-band 與殘餘條帶共用同一配置，`feed` 掃 `k*M` 連續位元組。
- [x] 7.2 測試 `test_tls_ragged_continuity()`：(a) 對 `K=256,M=300` 的頂部 k(∈{1,7,128,255,256}) 列 ROI 斷言 `.isContinuous()` 且 rows/cols 正確；(b) 取 `N` 不整除 `K` 的多尺寸（700/701/300/503 × K=256/256/128/64，及 K=1）× Random/DistinctPerBand/HGradient × 兩家族（含逆序殘餘條帶最先處理），斷言 banded == reference。已接入 `main()`。
- [x] 7.3 【驗證】操作者重跑 ★沙盒驗證流程；回報 `OVERALL`。✅ 已驗證：`checks: 183 passed, 0 failed` / `OVERALL: PASS`。

> 至此，融合編碼器的 byte-identity、對稱翻轉、K-不變性、殘餘條帶全部由獨立 `prz_rle_test.exe` 鎖定，**完全未碰 slicer build**。以下整合任務才進入 slicer 源碼。

## 8. 整合主光柵迴圈（進入 slicer 源碼）

- [x] 8.1 將 [SLAPrintSteps.cpp](../../../src/libslic3r/SLAPrintSteps.cpp) 的 `cv::rotate` + `prz_orient_after_rotate` + inline `flush_run` RLE 區塊（含 RLE 常數），替換為單一 `prz_encode_layer_banded(mat, prz_x_mirror, PRZ_RLE_BAND_COLS, rle_buf)`；自 `TLSData` 移除 `mat_rotated`；thumb 擷取（`write_thumb`）已確認在融合編碼之前；`prz_x_mirror` 仍由 `prz_final_x_mirror()` 取得（`PhrozenPRZOrient.hpp` 保留供取 bool）。另：`#include` `PhrozenPRZRle.hpp`、於 `PhrozenPRZRle.hpp` 新增共用常數 `PRZ_RLE_BAND_COLS=256`、將 `Format/PhrozenPRZRle.{hpp,cpp}` 加入 `src/libslic3r/CMakeLists.txt` 來源清單（整合連結所需）。
- [x] 8.2 【驗證】操作者跑 `build_release_vs2022.bat slicer` 確認編譯/連結通過（無 `LNK2005`，RLE 常數已單點化）；回報結果。✅ 已驗證：VS2022 IDE 編譯/連結 100% 通過。

## 9. 整合 PhrozenPRZ cache-miss 路徑

- [x] 9.1 cache-miss 路徑整合：[B] 移除 per-layer `cv::Mat rotated` + `cv::rotate` + `prz_orient_after_rotate`（`batch_mats[i]` 保持 portrait）；[C] 移除 inline `flush_run` 序列 RLE 編碼與 `przByte`、`BLACK/WHITE/GRAY/BYTE_NUMBER/CONTINUOUS_BOUND/BOUND_0` 常數，改呼叫共用 `prz_encode_layer_banded(batch_mats[i], prz_x_mirror, PRZ_RLE_BAND_COLS, rle_scratch)`（`rle_scratch` 跨批次重用）；`#include "PhrozenPRZRle.hpp"`。cache-hit 路徑（串流現成 `.rle`）不動。
- [x] 9.2 【驗證】操作者跑 `build_release_vs2022.bat slicer` 編譯通過；回報結果。✅ 已驗證：VS2022 IDE 編譯通過。
- [x] 9.3 【驗證・cache-hit vs cache-miss】操作者手動切一小模型 → 匯出 PRZ（cache-hit）；刪除該 disk cache 目錄 → 再匯出（cache-miss）；以 `fc /b a.prz b.prz` 比對，回報是否逐 byte 相同。✅ 已驗證：像素 RLE 流 bit-perfect 一致；僅 PRZ header `fileTime` 時間戳不同（正常格式行為，非像素資料）。

## 10. 端對端雙家族與記憶體驗證

- [x] 10.1 【驗證・Mega 垂直 FALSE】操作者以 Mega（normal）preset 切片匯出，於 Chitubox／機台確認方位正確、無分段鏡像；回報。✅ 已驗證：方位正確、無分段鏡像。
- [x] 10.2 【驗證・Revo 水平 TRUE】操作者以 Revo（lcd_mirror）preset 切同模型匯出，確認方位正確、無鏡像；回報。✅ 已驗證：方位正確、無鏡像。
- [x] 10.3 【驗證・跨版本 byte-identity】保留「本變更前」建立的 disk cache，升級後重匯出，確認 `is_valid()` 命中（`CACHE_VERSION` 未變）且與 cache-miss 重算逐 byte 相同；回報。✅ 已驗證：舊快取完美向下相容、命中、像素一致。
- [x] 10.4 【驗證・記憶體峰值】操作者以工作管理員／RAMMap 量測 360 層 13320×5120 切片峰值 RAM，確認較變更前下降約 520MB（朝 ~1.6GB 藍圖），並記錄 wall-clock 未退步；回報。✅ **黃金實測：16K 列印盤切片峰值 RAM ~3.6GB → ~1200MB，正面擊敗 Chitubox 1600MB 基準**；wall-clock 未退步。

## 11. 收尾與參數定值

- [x] 11.1 固化 `PRZ_RLE_BAND_COLS=256`（於 `PhrozenPRZRle.hpp`，兩呼叫站點共用）並寫入「微型沙盒實測 + L2/L3 快取容量權衡 + 16K 峰值 3.6GB→1.2GB」技術依據註解；於 `prz_encode_layer_banded` 上方加 `// CRITICAL CONTRACT` 鋼鐵警語，詳述對稱翻轉規則（K>1 區段置換陷阱）與跨界 run-state 連續性（含 head/checksum 各一次、殘餘條帶無特例）。
- [x] 11.2 保留 `#ifdef PRZ_RLE_TEST` 內聯測試地基（編譯期預設關閉、零執行期成本）與 `prz_pure_sandbox/` 沙盒目錄作為可重跑回歸；`PhrozenPRZRle.cpp` 頂部維持指向沙盒的 ★沙盒驗證流程說明。
- [x] 11.3 【驗證・總回歸】操作者最後再跑一次 ★沙盒驗證流程確認 `OVERALL: PASS`，並於 PR 描述貼上終端機 PASS 摘要 + 8/9/10 的建置/比對/記憶體結果。✅ 已驗證：沙盒 `checks: 183 passed, 0 failed` / `OVERALL: PASS`；8/9/10 建置/比對/記憶體結果全正確。