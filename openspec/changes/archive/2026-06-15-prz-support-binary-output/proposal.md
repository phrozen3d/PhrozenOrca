## Why

目前 SLA 光柵化在 `rasterize()` 階段，模型與支撐的多邊形早在 `merged_input_to_slices()`（[SLAPrintSteps.cpp:1329](../../../src/libslic3r/SLAPrintSteps.cpp#L1329)）就被 `union_ex` 合併成單一 ExPolygons，導致 AA（`anti_aliasing_level`）、灰階（`gray_scale_level`）、模糊（`image_blur_enable`）以及全域減光（`picture_grayscale`）一律套用在「模型＋支撐」整張影像上。支撐被這些柔化處理削弱後，尖端與接觸點固化面積縮小、貼床底層附著力下降，造成支撐脫落、翹曲與列印失敗。

支撐的唯一目的是穩固承載，需要的是**實心、銳利、滿曝光**，而非外觀平滑。本變更讓支撐區域在 PRZ 輸出時豁免所有影像柔化處理，換取列印可靠度。

## What Changes

- **雙軌幾何**：`PrintLayer` 不再於 `union_ex(trslices)` 合併模型與支撐，改為保留兩條獨立 track（model-track / support-track）；origin（`soModel` / `soSupport`）本就存活至此切點，無需重新分類。
- **一般層差異化光柵**：model-track 維持既有 AA／`gray_scale_level`／`image_blur` 處理；support-track 改走 `gamma=0` threshold，輸出純二值（0/255），不套 AA／灰階／模糊。
- **底層整層強制二值**：層索引 `idx < m_tolerance_bottom_layer_count` 時，model-track **與** support-track 皆以 threshold 二值輸出，確保貼床底層滿曝光附著（與一般層只二值化支撐的規則不同）。
- **合成順序（永遠 255）**：合成發生在 `picture_grayscale` LUT **之後**，support-track 像素恆為純白 255、**豁免全域減光**；model-track 仍套 LUT。最終 `composite = max(model_after_LUT, support_255)`。
- **RasterCache key 失效正確性**：cache key 須納入 `m_tolerance_bottom_layer_count`、走 model 與 support **雙 track** 的幾何雜湊、並 bump `CACHE_VERSION`；新增 `SLARasterParams` 欄位須遵守零初始化／無未定義 padding 紀律。
- **功能定性**：Always-on，無使用者開關。
- **範圍**：本次完整涵蓋 **PRZ 路徑**（主光柵迴圈 + PhrozenPRZ cache-miss 批次，兩者 byte 必須一致）；**SL1 PNG 匯出路徑列為後續技術債**，不在本變更範圍。

### 技術取捨（誠實定性）

本變更**並非效能優化**。雙軌幾何使每層多一次 AGG fill（support）＋一次合成 pass，光柵化時間預估增加 **約 +30~60%**，峰值記憶體最多 +520MB（可以「同 buffer 覆寫 255」壓低）。唯一回沖是支撐純二值使 RLE run 變長，PRZ 略小、編碼略快。**此成本的正當性來自「支撐實心化與貼床附著力」，而非速度**——這是一項以光柵化時間換取列印可靠度的刻意取捨。

## Capabilities

### New Capabilities
- `prz-support-binary-rasterization`: 模型／支撐雙軌幾何分流，支撐區域於 PRZ 輸出豁免 AA／灰階／模糊與全域減光（永遠純白 255），底層整層強制二值；定義一般層與底層的差異化光柵規則與合成順序。

### Modified Capabilities
- `sla-parallel-png-raster-cache`: cache key 須加入底層數參數、雜湊 model 與 support 雙 track 幾何，並隨格式變更 bump `CACHE_VERSION`，避免改底層數或僅變動支撐幾何時的 stale cache hit。
- `sla-on-demand-prz-export`: PRZ 主光柵迴圈與 cache-miss 批次路徑須等價落地雙軌合成邏輯，維持兩路徑 byte 一致；thumbnail 與 2D Preview 於合成後擷取自動繼承。

## Impact

- [src/libslic3r/SLAPrint.hpp](../../../src/libslic3r/SLAPrint.hpp)：`PrintLayer` 新增 model/support 雙 track；`SLARasterParams` 新增 `bottom_layer_count`（注意 padding 紀律）。
- [src/libslic3r/SLAPrintSteps.cpp](../../../src/libslic3r/SLAPrintSteps.cpp)：取消 1329 單軌合併改存雙軌；`rasterize()` 主迴圈雙軌差異化光柵 + 底層 predicate + LUT 後合成。
- [src/libslic3r/Format/PhrozenPRZ.cpp](../../../src/libslic3r/Format/PhrozenPRZ.cpp)：cache-miss 批次路徑等價落地。
- [src/libslic3r/SLA/RasterCache.cpp](../../../src/libslic3r/SLA/RasterCache.cpp)：`compute_key` 走雙 track、納入底層數、bump `CACHE_VERSION`。
- `expolygons_to_cvmat` 介面（[src/libslic3r/SLA/RasterToCvMat.*](../../../src/libslic3r/SLA/RasterToCvMat.cpp)）：需支援雙軌輸入／support mask 與合成點。
- **正交不衝突**：faded 層（大象腳補償）為幾何操作，與底層二值（像素模式）為獨立 predicate（`has_efc = idx < faded_layers` vs `is_binary = idx < bottom_layer_count`），各自判斷、互不引用。
- **範圍外**：SL1 PNG 匯出（`sla::Raster` 路徑，不經 `expolygons_to_cvmat`）本次不處理。