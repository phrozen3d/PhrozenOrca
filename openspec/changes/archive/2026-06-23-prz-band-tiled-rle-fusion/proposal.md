## Why

前一變更 `prz-support-roi-composite`（Opt-2）已砍掉全幀 support 緩衝，但 PRZ 層影像的 RLE 編碼前仍保留一個**全幀旋轉副本** `mat_rotated`（13320×5120 ≈ 65MB/緒，[SLAPrintSteps.cpp:1534](../../../src/libslic3r/SLAPrintSteps.cpp#L1534)），8 緒常駐 +~520MB；PhrozenPRZ cache-miss 批次路徑亦每層 malloc 一個同尺寸 `rotated`（[PhrozenPRZ.cpp:822-824](../../../src/libslic3r/Format/PhrozenPRZ.cpp#L822-L824)）。此副本是 Opt-2 design.md 明列、待 profile 實證後處理的 **P2 技術債**，亦是把整體 peak RAM 由 ~3.6GB 壓向 ~1.6GB 藍圖的最後一塊大頭。本變更以 **Band/Tiled 旋轉與 RLE 融合** 在「不犧牲、甚至優化吞吐」的前提下消滅此 65MB 副本——關鍵在於 13320×5120 下「天真全幀跨步掃描」會造成嚴重 cache-line 錯失，故必須走「L3-friendly 小容量條帶 + 跨界攜帶 RLE run-state」的精細路線，而非單純的 direct-scan。

## What Changes

- **新增融合自由函式** `prz_encode_layer_banded(portrait_mat, final_x_mirror, band_cols, out_rle)`：以**直向 column-slab 條帶**（src `M×K`）逐塊 `cv::rotate(ROTATE_90_CW)` 旋轉成 `K×M` 連續 tile（TLS 重用、約 3MB、落在 L2/L3），就地完成局部翻轉與 RLE 掃描，**永不**物化 65MB 全幀旋轉副本。
- **對稱翻轉規則（grill 收斂・CRITICAL）**：全圖垂直翻轉在 K>1 時無法只靠外層條帶逆序達成（會造成「區段置換 Block Permutation」沉默錯誤——可正常解碼卻在硬體上印出分段鏡像）。正解收斂為「**雙家族皆呼叫 `prz_orient_after_rotate(tile, code)` 做局部翻轉**，唯一分歧只在外層條帶排程方向」：
  - `final_x_mirror = FALSE`（垂直，Mega 系列）= 條帶**逆序 Descending** + 局部 `code 0`
  - `final_x_mirror = TRUE`（水平，Revo 系列）= 條帶**正序 Ascending** + 局部 `code 1`
- **殘餘條帶無特例（grill 收斂）**：`N`（= `display_pixels_y` = dst 列數）不被 `K` 整除時，以 `k = min(K, N - offset)` 動態界定條帶邊界，殘餘 tile 走同一路徑，**不開特例分支**，確保 RLE 狀態機（`cur`/`count`/`sum`/`pos`）跨條帶與跨層邊界絕對連續；`0x55` head 與 checksum 各於全幀首尾僅一次。
- **雙路徑共用唯一真相源**：主光柵迴圈（[SLAPrintSteps.cpp](../../../src/libslic3r/SLAPrintSteps.cpp)）與 PhrozenPRZ cache-miss 批次（[PhrozenPRZ.cpp](../../../src/libslic3r/Format/PhrozenPRZ.cpp)）改呼叫**同一支** `prz_encode_layer_banded`；目前兩處重複的 rotate+flip+RLE 邏輯合而為一，二進位一致由「人工維持」升級為「結構保證」。
- **移除全幀旋轉緩衝**：刪除 `TLSData.mat_rotated`；移除 PhrozenPRZ 批次的 per-layer `rotated`。`mat`、`tile`、`rle_buf` 維持 TLS 零-malloc 契約。
- **不 bump `CACHE_VERSION`**：融合輸出與舊 `cv::rotate`→`flip`→全幀掃描路徑**逐位元組相同**，既有 disk cache 持續有效、不需重建。

## Capabilities

### New Capabilities
- `prz-band-tiled-rle-encoding`: 條帶化融合旋轉+RLE 編碼器。涵蓋（1）byte-identity：輸出與舊全幀路徑逐位元組相同；（2）K-不變性：輸出與 `band_cols` 取值完全無關（含 `K=1`、質數、`K>N`）；（3）對稱翻轉規則的幾何等價（垂直=逆序排程+局部 code 0；水平=正序+局部 code 1）；（4）殘餘條帶 `min(K, N-offset)` 與跨界 run-state 連續性；（5）tile 緩衝記憶體上界（≈`K×M`，TLS 重用）。

### Modified Capabilities
- `sla-on-demand-prz-export`: 移除全幀 `mat_rotated`／per-layer `rotated`，cache-miss 與主迴圈共用 `prz_encode_layer_banded`；峰值記憶體需求下調（−~520MB，朝 ~1.6GB 藍圖收斂），兩路徑維持 byte 一致。
- `sla-parallel-png-raster-cache`: 明確規範**不 bump `CACHE_VERSION`**——融合輸出與全幀版逐像素相同，既有快取持續有效、不觸發全量重建；確認光柵並行維持 per-layer 粒度（條帶為層內序列、絕不下放為層內並行，以免破壞 run-state 序列攜帶）。

## Impact

- [src/libslic3r/Format/PhrozenPRZOrient.hpp](../../../src/libslic3r/Format/PhrozenPRZOrient.hpp)：`prz_orient_after_rotate` / `prz_final_x_mirror` 維持不動，作為融合函式內局部翻轉的唯一真相源。
- 新增融合函式所在 TU（建議 `PhrozenPRZRle.{hpp,cpp}`，與 `PhrozenPRZOrient.hpp` 同層的 OpenCV 依賴邊界，避免大函式 inline 進 header 造成重複 codegen）。
- [src/libslic3r/SLAPrintSteps.cpp](../../../src/libslic3r/SLAPrintSteps.cpp)：移除 `TLSData.mat_rotated`（[L1534](../../../src/libslic3r/SLAPrintSteps.cpp#L1534)）、[L1617](../../../src/libslic3r/SLAPrintSteps.cpp#L1617) `cv::rotate` 與 inline `flush_run` RLE 區塊（L1626-1699），改委派融合函式。
- [src/libslic3r/Format/PhrozenPRZ.cpp](../../../src/libslic3r/Format/PhrozenPRZ.cpp)：移除批次 `rotated`（[L822-824](../../../src/libslic3r/Format/PhrozenPRZ.cpp#L822-L824)）與後續序列 RLE 編碼，改呼叫共用函式。
- 快取：`CACHE_VERSION` **不變**（[RasterCache.cpp](../../../src/libslic3r/SLA/RasterCache.cpp)）。
- 風險面：區段置換鏡像（以對稱翻轉規則 + K-不變性測試固化）、byte-identical（以黃金對拍 + K-不變性測試固化）、巨型單色 run 的 count 溢位（`N*M≈68M < 2^31`，落於 4-byte 連續編碼 `1<<28` 範圍）。
- 範圍外：本身的 cache 鍵格式、12 緒以上的並行調參、thumbnail 路徑、SL1 PNG 路徑、`band_cols`(K) 的最終調優值（留待 design/profile）。