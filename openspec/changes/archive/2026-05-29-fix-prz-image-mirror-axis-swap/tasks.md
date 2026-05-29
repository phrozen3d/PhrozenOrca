# Tasks

> 實作鐵則（不可違反）：
> 1. **先實作共用 Helper，再分別套用於兩個旋轉路徑。** 嚴禁繞過 Helper 各自寫一份。
> 2. **每完成一個路徑就立即驗證**（編譯 + 局部邏輯確認），通過後才能進入下一個路徑。
> 3. **絕對禁止「兩路徑全改完才驗證」**——錯誤必須在第一時間被攔截。

## 1. 共用 Helper（先行，唯一事實來源）

- [x] 1.1 新增共用 inline helper `prz_orient_after_rotate(cv::Mat& mat, bool final_x_mirror)`，語意：在 `ROTATE_90_CLOCKWISE` **之後**呼叫；`final_x_mirror==false` → `cv::flip(mat, mat, 0)`（垂直）；`final_x_mirror==true` → `cv::flip(mat, mat, 1)`（水平）。落點：新建專用輕量標頭 [PhrozenPRZOrient.hpp](src/libslic3r/Format/PhrozenPRZOrient.hpp)（僅由兩個旋轉點所在 .cpp include，避免把 opencv 拉進被 GUI 引用的 PhrozenPRZ.hpp）。
- [x] 1.2 新增共用 helper `prz_final_x_mirror(const SLAPrinterConfig&)` 計算「最終 X 鏡像 bool」，與 header Xmirror 同源：由 `display_mirror_mode` 推導（`lcd_mirror→true`、`normal`/`dlp_normal→false`），缺值時回退 `display_mirror_x`。header 寫入端（[PhrozenPRZ.cpp](src/libslic3r/Format/PhrozenPRZ.cpp) Xmirror 區塊）已改呼叫此 helper，消除雙事實來源。
- [x] 1.3 helper 加註解，引用 [design.md](design.md) 的 D4 推導，說明「為何 false→垂直、true→水平」。
- [x] **1.4 驗證（Helper 完成）**：使用者手動編譯通過；邏輯勾稽：`cv::flip(code=0)`＝垂直、`code=1`＝水平，對應 `false→垂直`、`true→水平`；`cv::flip` 為元素重排不改 `rows/cols/type`。

## 2. 路徑 A — 主路徑（寫入 cache）

- [x] 2.1 在 [SLAPrintSteps.cpp:1566](src/libslic3r/SLAPrintSteps.cpp#L1566) 的 `cv::rotate(..., ROTATE_90_CLOCKWISE)` 之後，呼叫 §1.1 helper，傳入由 §1.2 helper 算出的 `final_x_mirror`（於平行迴圈前以 `m_print->printer_config()` 算一次 `prz_x_mirror`，逐層共用）。
- [x] **2.2 驗證（路徑 A 完成）**：使用者手動編譯通過；Mega 8K S / 8K V2 / Revo 16K 圖檔結果正確，與 Chitubox 一致。

## 3. 路徑 B — cache-miss 備援

- [x] 3.1 在 [PhrozenPRZ.cpp](src/libslic3r/Format/PhrozenPRZ.cpp) cache-miss 路徑的 `cv::rotate(..., ROTATE_90_CLOCKWISE)`（`batch_mats[i] = std::move(rotated)` 之後）呼叫**同一個** §1.1 helper `prz_orient_after_rotate(batch_mats[i], prz_x_mirror)`；`prz_x_mirror` 由 §1.2 同源 helper `prz_final_x_mirror(print.printer_config())` 於迴圈前算一次。include 已於 §1 加入，無需重複。
- [x] **3.2 驗證（路徑 B 完成）**：使用者手動編譯通過。位元組一致性由結構保證：兩路徑使用同一 helper、同一條件來源（`prz_final_x_mirror`）、同一套用時機（rotate 後、RLE 前）、同一作用對象（旋轉後 landscape buffer）；本輪三機實測（走 cache 路徑）圖檔正確亦佐證。顯式 cache-hit/cache-miss byte-diff 為可選加強項，留待需要時執行。

## 4. 快取失效

- [x] 4.1 提升 [RasterCache.hpp:62](src/libslic3r/SLA/RasterCache.hpp#L62) 的 `CACHE_VERSION`（4 → 5），註解更新為「post-rotate orientation flip (Chitubox alignment) — old direction bytes invalidated」。
- [x] **4.2 驗證**：`CACHE_VERSION` 參與 cache key 的 CRC32 雜湊（[RasterCache.cpp:51-54](src/libslic3r/SLA/RasterCache.cpp#L51)），版本 4→5 必然產生不同 hash → 舊快取項一律 cache-miss，更新後首次切片重新光柵化並以修正後方向重建快取。

## 5. 端到端驗收與回歸

- [x] 5.1 端到端：使用者已對三台機型（Mega 8K S、Mega 8K V2、Mighty Revo 16K）實測，逐機台 `.prz` 圖檔方向正確、與 Chitubox 一致。
- [x] 5.2 維度回歸：補償用 `cv::flip` 為元素重排，不改變 `rows/cols/type`；旋轉後緩衝維持橫向（Mega 7680×4320），與 header `XResolution`/`YResolution` 一致。三台圖檔正確即佐證維度未亂。
- [x] 5.3 不波及確認：2D 預覽（[SLASlice2DCanvas.cpp:803](src/slic3r/GUI/SLASlice2DCanvas.cpp#L803)）自行 on-demand 光柵化、不讀 raster cache、不呼叫 `generate_prz`，故不受影響；補償翻轉僅存在於兩個 PRZ 旋轉點；未改 `RasterBase::Trafo`/`RasterToCvMat`/`AGGRaster`，SL1 等其他輸出格式不受影響。`generate_prz`/cache 的唯一額外 consumer 為 [ExportPRZJob.cpp](src/slic3r/GUI/Jobs/ExportPRZJob.cpp)。
- [x] 5.4 確認未更動任何 profile 參數、`display_mirror_*`、`display_orientation`、解析度；header Xmirror byte 推導改呼叫 `prz_final_x_mirror` 為等價重構，輸出位元組值不變。

## 6. 收尾

- [x] 6.1 以 `openspec validate fix-prz-image-mirror-axis-swap` 驗證規格一致 → valid。
- [x] 6.2 新增開發紀錄 [.resin-devLog/SLA_PRZ_Image_Orientation_Fix.md](../../../.resin-devLog/SLA_PRZ_Image_Orientation_Fix.md)，記錄根因、D4 推導、旋轉後補償翻轉與雙路徑修正。