## Why

前一變更 `prz-support-binary-rasterization` 落地雙軌光柵化後，為了在 `picture_grayscale` LUT 之後把支撐合成上去，`TLSData` 新增了一個**全幀** `support_mat`（13320×5120 ≈ 65MB/緒），8 緒峰值 +~520MB（已記錄為 design.md「後續技術債：Opt-2」）；且每層支撐合成走「全幀 `setTo(0)` + 全幀 `cv::max`」，吃掉約 4 趟全幀記憶體流量，使「按下切片 → 產生各層預覽」的 wall-clock 居高不下。本變更以 **support ROI 合成** 同時根治記憶體峰值與光柵化吞吐——兩者在此步同向不衝突。

## What Changes

- 新增自由函式 `composite_support_binary(dst, support_polys, res, pxdim, trafo)`：僅在 support bbox 的**局部 ROI** 內把支撐光柵化（contour→255、holes→0）後，以 ROI-local `cv::max` 合成進 `dst`，取代「全幀 support buffer + 全幀 `cv::max`」。
- **整數平移繞開浮點截斷**：ROI 內的像素座標**沿用 fast-path `to_cv_point` 的全幀精度**算出整數座標後，**減去整數 `(x0,y0)`** 平移進局部緩衝；**不**重用 `compute_pixel_roi` 會把 `new_trafo.center` 做 `static_cast<coord_t>` 截斷的路徑（該截斷在 `1/sx` 非整數時會擾動邊緣像素的 `lround`）。ROI 框 `{x0,y0,x1,y1}`（floor/ceil/clamp/mirror/flipXY，無截斷）可沿用既有 box 計算、僅丟棄其 `new_trafo`。
- **Guard Band 防禦圈**：ROI 每邊外擴 1~2px 再 clamp 回 `[0,W)×[0,H)`，從結構上杜絕 `cv::fillPoly` 在縮小畫布邊緣的裁切歧義（不依賴 OpenCV 內部 top-left/subpixel 規則，未來升版不破功）；多出的邊框為背景 0，`cv::max(dst,0)=dst` 不污染任何像素，**保證支撐邊緣絕不縮水**。
- **局部 memset 清零不變式（CRITICAL）**：`cv::fillPoly` 只寫多邊形內部、不清背景，而局部緩衝是 TLS 跨層重用、`std::vector::resize` 不歸零保留容量。故每次合成於 `fillPoly` 前**強制** `std::memset` 本層 guarded ROI 有效區，杜絕「上一層支撐殘留 → 跨層幽靈支撐滲漏」。維持零 malloc 契約（不改用非 TLS buffer）。
- **移除全幀 support 緩衝**：刪除主迴圈 `TLSData.support_mat` 與 PhrozenPRZ `enumerable_thread_specific<cv::Mat> support_tls`；`rasterize_layer_dual` 改委派 `composite_support_binary`。
- **雙路徑共用唯一真相源**：主迴圈與 PhrozenPRZ cache-miss 批次呼叫**同一支** `composite_support_binary`，保證兩路徑 byte-identical。
- **不 bump `CACHE_VERSION`（一致性保證）**：整數平移使 ROI 合成輸出與舊全幀 `cv::max` **逐像素相同**，舊 disk cache 仍有效、不需重建；跨版本 cache-hit(舊bytes) 與 cache-miss(新ROI bytes) 不會分歧。
- **不盲目調大執行緒**：保留 `cv::rotate`（否決會造成跨步 cache miss 與 byte 漂移的「天真逐像素 rotate 融合」）；`RASTERIZE_CONCURRENCY` 維持 8（最多保守至 10，守住 dual-track 回歸峰值 1.88GB 之下）。提速主引擎是 Opt-2 本身砍掉的 ~4 趟全幀流量（後半段頻寬 −40~45%）；**12 緒、P2 band/tiled 融合、消除 `mat_rotated` 一律列為「需 profile 實證後」的後續技術債**（迴圈可能為記憶體頻寬受限，盲加緒無益且推高峰值）。

## Capabilities

### New Capabilities
<!-- 無新增 capability：本變更全部為既有光柵化行為的修改。 -->

### Modified Capabilities
- `sla-roi-aa-raster-pipeline`: 既有 ROI 管線僅涵蓋 model 的 AGG slow-path；新增 **support 二值軌的 ROI-local 合成** 行為（box-only ROI + guard band、顯式 memset 清零生命週期、整數平移與全幀輸出逐像素相同、ROI-local `cv::max`）。
- `prz-support-binary-rasterization`: 支撐與模型的像素合併由「全幀 `cv::max`」改為「ROI-local 合成」；對外語義不變（支撐恆 255 並豁免 `picture_grayscale`、洞內保留 model 像素、相鄰邊界支撐維持銳利、thumbnail 與合成一致）。
- `sla-on-demand-prz-export`: 移除全幀 support 緩衝（`support_tls`），cache-miss 與主迴圈共用 `composite_support_binary`；記憶體峰值需求下調（−~520MB），兩路徑維持 byte 一致。
- `sla-parallel-png-raster-cache`: 明確規範 **不 bump `CACHE_VERSION`**——Opt-2 輸出與全幀版逐像素相同，既有快取持續有效、不觸發全量重建。

## Impact

- [src/libslic3r/SLA/RasterToCvMat.cpp](../../../src/libslic3r/SLA/RasterToCvMat.cpp) / [.hpp](../../../src/libslic3r/SLA/RasterToCvMat.hpp)：新增 `composite_support_binary`；`rasterize_layer_dual`（[L441](../../../src/libslic3r/SLA/RasterToCvMat.cpp#L441)）移除 `support_tmp` 參數與全幀 `cv::max`（L482-489），改委派；ROI box 計算沿用 `compute_pixel_roi`（[L109](../../../src/libslic3r/SLA/RasterToCvMat.cpp#L109)）的 2.1–2.3 部分。
- [src/libslic3r/SLAPrintSteps.cpp](../../../src/libslic3r/SLAPrintSteps.cpp)：移除 `TLSData.support_mat`（[L1535](../../../src/libslic3r/SLAPrintSteps.cpp#L1535)）與呼叫端傳參（L1576）。
- [src/libslic3r/Format/PhrozenPRZ.cpp](../../../src/libslic3r/Format/PhrozenPRZ.cpp)：移除 `support_tls`（[L790](../../../src/libslic3r/Format/PhrozenPRZ.cpp#L790)）與 L817 傳參，改呼叫共用函式。
- 快取：`CACHE_VERSION` **不變**（[RasterCache.cpp](../../../src/libslic3r/SLA/RasterCache.cpp)）；舊 disk cache 仍有效。
- 風險面：跨層殘留污染（以 CRITICAL memset + 專屬回歸測試固化）、邊界裁切等價（以 guard band 結構性消除）、byte-identical（以整數平移數學保證）。
- 範圍外：12 緒、P2 band/tiled rotate 融合、thumbnail resize 優化、dst 全幀 memset 消除、SL1 PNG 路徑。