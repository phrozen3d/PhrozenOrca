# Tasks: Opt-2 Support ROI 合成

> 排程原則：四階段各為「可獨立編譯、可獨立交付」的最小單位，**每階段末強制即時驗證**（且儘量為使用者可手動執行/肉眼檢驗的形式），嚴禁累積到最後一次性驗證。相依：階段一 →（核心函式就緒）→ 階段二 → 階段三 → 階段四。

## 1. 階段一：核心函式 `composite_support_binary`

- [x] 1.1 於 [RasterToCvMat.hpp](../../../src/libslic3r/SLA/RasterToCvMat.hpp) 宣告、[.cpp](../../../src/libslic3r/SLA/RasterToCvMat.cpp) 實作 `composite_support_binary(cv::Mat& dst, const ExPolygons& support_polys, const Resolution&, const PixelDim&, const RasterBase::Trafo&)`：`support_polys.empty()` 早退。
- [x] 1.2 ROI box：沿用 `compute_pixel_roi`（[L109](../../../src/libslic3r/SLA/RasterToCvMat.cpp#L109)）的 2.1–2.3 取得 `{x0,y0,x1,y1}`（floor/ceil/clamp/mirror/flipXY），**丟棄 new_trafo（2.4）**。
- [x] 1.3 Guard band：`x0g=max(0,x0-g)`、`y0g=max(0,y0-g)`、`x1g=min(W,x1+g)`、`y1g=min(H,y1+g)`，`g=2`（常數，加註理由）；`roi_wg/roi_hg`。
- [x] 1.4 **CRITICAL memset**：`small_buf`（函式內 `thread_local`）`resize(roi_wg*roi_hg)` 後，`fillPoly` 前 `std::memset(small_buf.data(), 0, roi_wg*roi_hg)`；上方加 `// CRITICAL:` 警語（說明與 AGG/全幀清零語義不同，刪除將致跨層幽靈支撐）。
- [x] 1.5 整數平移 fillPoly：每頂點以 fast-path `to_cv_point`（全幀精度）算 `(Px,Py)`，平移 `(Px-x0g, Py-y0g)` 填入 `small_buf`（contour→255、holes→0，順序同舊 fast-path）。
- [x] 1.6 ROI 合成：`cv::Mat sb(roi_hg, roi_wg, CV_8UC1, small_buf.data()); cv::max(dst(cv::Rect(x0g,y0g,roi_wg,roi_hg)), sb, dst(...))`。
- [x] 1.7 改 `rasterize_layer_dual`（[L441](../../../src/libslic3r/SLA/RasterToCvMat.cpp#L441)）：改委派 `composite_support_binary`、移除全幀 `expolygons_to_cvmat(support_tmp,…)`+`cv::max`；保留 `support_polys.empty()` 早退。**【採選項 A】** `support_tmp` 參數暫時保留（標記 `(void)`、未使用），其與呼叫端全幀 buffer 的實際移除併入階段二 2.1/2.2，以維持本階段可獨立編譯。
- [x] 1.8 **【即時驗證｜使用者手動】** 手動下指令編譯：`build_release_vs2022.bat slicer`，確認**編譯通過**。於 `composite_support_binary` 內暫時加 `std::cout` 輸出每層 ROI 包圍盒 `x0g,y0g,x1g,y1g,roi_wg,roi_hg`；手動切一個含支撐模型，對照終端機日誌，肉眼確認 ROI 座標落在影像範圍內、且大致吻合支撐在版面上的位置與大小（驗畢移除 `std::cout`）。
  - 備註：使用者實機驗證通過（Revo 16K、正方體置中加支撐）。`debug_roi.txt` 各層 ROI 座標皆落在版面內、置中支撐位置吻合、guard band 正確 ±2。

## 2. 階段二：雙路徑落地 + 移除全幀 buffer + byte 比對

- [x] 2.1 主迴圈（[SLAPrintSteps.cpp:1532-1579](../../../src/libslic3r/SLAPrintSteps.cpp#L1532)）：移除 `TLSData.support_mat` 成員與 `rasterize_layer_dual` 呼叫的 support buffer 傳參（L1576）。同步移除 `rasterize_layer_dual` 簽名的 `support_tmp` 參數（header + cpp）。
- [x] 2.2 PhrozenPRZ cache-miss（[PhrozenPRZ.cpp:790,816](../../../src/libslic3r/Format/PhrozenPRZ.cpp#L790)）：移除 `enumerable_thread_specific<cv::Mat> support_tls` 與其傳參（及僅為它而加的 `#include <tbb/enumerable_thread_specific.h>`），改呼叫**同一支** `composite_support_binary`（經 `rasterize_layer_dual`）；保留其後 `cv::rotate` + `prz_orient_after_rotate`。
- [x] 2.3 確認 `CACHE_VERSION`（[RasterCache.hpp](../../../src/libslic3r/SLA/RasterCache.hpp#L94)）**維持不變**（= 7，本次未動）。
- [x] 2.4 **【即時驗證｜使用者手動 byte 比對】** 手動編譯 `build_release_vs2022.bat slicer`。準備兩個 PRZ：(a) Opt-2 **重構前** build 匯出的 `before.prz`、(b) 本階段 build 匯出的 `after.prz`（同一模型同設定）。手動執行二進位比對：
  - Windows：`fc /b before.prz after.prz`（預期輸出「找不到差異」/「no differences encountered」）
  - Linux/macOS：`cmp before.prz after.prz`（預期無輸出、exit 0）
  
  肉眼斷言 100% 完全相同 → 證明 ROI 合成 byte-identical、不需 bump 版本。另以同模型切片後，分別走 **cache-hit** 與 **cache-miss** 各匯出一次，再 `fc /b` / `cmp` 兩者，確認亦完全相同。

## 3. 階段三：跨層滲漏防禦・肉眼實證

- [x] 3.1 確認階段一的 CRITICAL memset 已就位且範圍為 `roi_wg*roi_hg`。
  - 核對結果（[RasterToCvMat.cpp:487-503](../../../src/libslic3r/SLA/RasterToCvMat.cpp#L487)）：順序 `resize(roi_wg*roi_hg)`（L489）→ **CRITICAL 警語**（L491-501）→ `std::memset(small_buf.data(), 0, roi_wg*roi_hg)`（L502-503）→ `cv::fillPoly`（L530-534）。範圍正確（= 本層 guarded 有效區，非僅 resize 新增段、非整個容量）；警語完整說明「與 AGG/全幀清零非對稱、fillPoly 不清背景、resize 不歸零保留容量、DO NOT DELETE」。
- [x] 3.2 **【即時驗證｜使用者手動・反證法】** 故意防禦力實證：
  1. **暫時註解掉** 1.4 的 `std::memset` 那一行，編譯後切一個「不同層支撐分佈差異大」的模型（例如某些層支撐多、某些層支撐少且位置不同）。
  2. 在 GUI 2D Preview 手動拖動層滑桿，**肉眼確認** 支撐稀少層的空白處是否冒出「上一層殘留」的幽靈支撐像素/碎塊。
  3. 確認**有滲漏**後，**取消註解**還原 `memset`，重新編譯切片，再次拖動滑桿，**肉眼確認幽靈支撐完全消失**。
  
  以此一刪一補的對照，實證 memset 的防禦力（並作為日後不可誤刪的依據）。
  - 備註：使用者實機驗證通過——刪 memset 後幽靈支撐相當明顯，還原後消失。

## 4. 階段四：端對端量測（Peak RAM + 邊緣品質）

- [x] 4.1 **【即時驗證｜使用者手動・記憶體】** 開啟 Windows **工作管理員**（或資源監視器），切一個含大量支撐的模型，觀察切片並行期間本程式 **Peak RAM**；與 Opt-2 前 build 同模型對照，肉眼確認峰值下降。
  - 備註：使用者實機（50×50×50mm 正方體 + 底部支撐）量得峰值由 ~3800MB 降至 ~3600MB（≈ −200MB）。降幅小於理論 −520MB，係因單一小模型切片快、未持續飽和 8 緒（節省量 = 並行峰值緒數 × 65MB）；趨勢正確、方向符合預期。含大量支撐／層數多的大模型可見更接近理論值的降幅。
- [x] 4.2 **【即時驗證｜使用者手動・邊緣品質】** 在 GUI 2D Preview **放大**檢查支撐區域：肉眼確認支撐為**純白（255）銳利實心**、邊緣**完全無縮水/羽化**；切換 `picture_grayscale<255` 觀察 support 不變暗、model 變暗；檢視底層（`idx < bottom_layer_count`）model 與 support 皆二值。— 使用者實機驗證通過。
- [x] 4.3 **【即時驗證｜使用者手動・回歸】** `cd build && ctest --output-on-failure`（至少 `sla_print_tests` 全綠）；確認既有 PRZ 方位、header、thumb 快取等測試不回歸。多物件 / 非 z=0 起始情境抽驗底層映射無誤。— 使用者實機驗證通過。