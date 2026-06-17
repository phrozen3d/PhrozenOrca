# sla-roi-aa-raster-pipeline Specification

## Purpose
TBD - created by archiving change optimize-slice-performance. Update Purpose after archive.
## Requirements
### Requirement: Full-Image Zero-Fill Before ROI Processing

In the slow path (gamma > 0 or blur_pixel > 0), before any rendering occurs, the destination TLS matrix SHALL be zero-filled across its entire extent to prevent inter-layer pixel residuals.

#### Scenario: Non-empty layer zero-fill

- **WHEN** `expolygons_to_cvmat` is called with a non-empty polygon set and the destination matrix already contains data from a previous layer
- **THEN** all pixels in the destination matrix SHALL be set to 0 before any AGG rendering or blit operations begin

#### Scenario: Zero-fill is independent of ROI size

- **WHEN** the computed ROI covers only a fraction of the full image
- **THEN** pixels outside the ROI SHALL still be zero after the operation completes

---

### Requirement: Fast Exit for Empty Layers

When the input polygon set is empty, the pipeline SHALL skip all rendering work and return immediately after the zero-fill step.

#### Scenario: Empty polys fast exit

- **WHEN** the `polys` argument contains zero `ExPolygon` elements
- **THEN** the pipeline SHALL zero-fill the destination matrix and return without constructing any AGGRaster or allocating any small buffer

#### Scenario: Empty layer output is pure black

- **WHEN** an empty layer is processed
- **THEN** every pixel of the destination matrix SHALL be 0 (pure black)

---

### Requirement: Pixel-Space ROI Computation

The pipeline SHALL compute a bounding rectangle (ROI) from the layer's polygon geometry. The ROI SHALL be computed entirely in the pixel coordinate system after applying the full transformation chain (flipXY, scale, center offset, mirror_x, mirror_y). The ROI SHALL be expanded by `blur_pixel` in all four directions and clipped to the image boundaries `[0, W) × [0, H)`.

#### Scenario: ROI contains all rendered geometry

- **WHEN** the transformed pixel bounding box of all polygons in a layer is computed and expanded by `blur_pixel`
- **THEN** every pixel that could receive a non-zero value after AGG rendering and stack blur SHALL fall within the resulting ROI

#### Scenario: ROI is clipped to image bounds

- **WHEN** the expanded bounding box exceeds the image dimensions
- **THEN** the ROI SHALL be clamped so that `x0 ≥ 0`, `y0 ≥ 0`, `x1 ≤ W`, `y1 ≤ H`

#### Scenario: flipXY transformation is applied before ROI

- **WHEN** the printer configuration has `flipXY = true` (portrait mode)
- **THEN** the ROI SHALL be computed in the swapped pixel coordinate system, such that the object BBox is correctly located within the rotated image grid

#### Scenario: mirror_x transformation is applied before ROI

- **WHEN** `mirror_x = true`
- **THEN** the pixel-space BBox SHALL be horizontally mirrored (`px = W − px`) before expansion, so the ROI tracks the true on-screen position of the geometry

---

### Requirement: Thread-Local Small Buffer Lifecycle

The pipeline SHALL maintain a thread-local buffer (`small_buf`) sized to the current ROI dimensions (`roi_w × roi_h` bytes). The buffer SHALL be reused across layers without re-allocation when existing capacity is sufficient.

#### Scenario: Initial allocation

- **WHEN** a thread processes its first layer with a non-empty ROI
- **THEN** `small_buf` SHALL be allocated to at least `roi_w × roi_h` bytes

#### Scenario: Capacity-based reuse

- **WHEN** a subsequent layer's ROI is no larger than the previously allocated `small_buf` capacity
- **THEN** the buffer SHALL be resized (with no heap allocation) rather than reallocated, keeping the same underlying memory

#### Scenario: Growth re-allocation

- **WHEN** a layer's ROI is larger than the current `small_buf` capacity
- **THEN** the buffer SHALL grow to accommodate the new ROI size; this re-allocation is permitted and expected to be rare

---

### Requirement: ROI-Local AGG Rendering

AGG rasterization SHALL be performed using a raster object constructed with the ROI dimensions (`roi_w × roi_h`) and an adjusted transform offset that maps world coordinates into the ROI-local coordinate system. The `small_buf` buffer SHALL serve as the backing pixel store for this raster object.

#### Scenario: AGGRaster is sized to ROI, not full image

- **WHEN** an AGGRaster is created for a layer
- **THEN** it SHALL be constructed via the external-buffer overload, using the thread-local `small_buf` (sized to `roi_w × roi_h` bytes) as its backing store; the internal `m_buf` SHALL have capacity 0 (no heap allocation), and the rendered pixel data SHALL reside in `small_buf`, NOT in an internal `W × H` buffer

#### Scenario: Geometry is rendered at the correct ROI-local position

- **WHEN** a polygon vertex maps to full-image pixel `(px_global, py_global)`
- **THEN** the same vertex SHALL render at `(px_global − x0, py_global − y0)` within `small_buf`, where `(x0, y0)` is the ROI origin

#### Scenario: aa_steps quantization is applied only within ROI

- **WHEN** the aa_steps post-processing loop runs
- **THEN** it SHALL iterate over only the `roi_w × roi_h` pixels in `small_buf`, not the full `W × H` image

---

### Requirement: ROI-Local Stack Blur

When `blur_pixel ≥ 2`, the `stack_blur_gray8` operation SHALL be applied exclusively within `small_buf`, using `roi_w` as the rendering buffer stride.

#### Scenario: Stack blur stride equals ROI width

- **WHEN** `stack_blur_gray8` is invoked
- **THEN** the `agg::rendering_buffer` wrapping `small_buf` SHALL have stride = `roi_w`, NOT `W`

#### Scenario: Blur does not read or write outside small_buf

- **WHEN** stack blur completes
- **THEN** no memory outside `small_buf` SHALL have been accessed by the blur operation

---

### Requirement: ROI Blit to Destination Matrix

After rendering and blur, the pipeline SHALL copy `small_buf` row-by-row into the destination TLS matrix at position `(x0, y0)`.

#### Scenario: Blit writes correct region

- **WHEN** `small_buf` is blitted to the destination matrix
- **THEN** destination pixel `(x0 + dx, y0 + dy)` for `0 ≤ dx < roi_w`, `0 ≤ dy < roi_h` SHALL equal `small_buf[dy * roi_w + dx]`

#### Scenario: Non-ROI pixels are unchanged

- **WHEN** the blit completes
- **THEN** all pixels outside `[x0, x1) × [y0, y1)` in the destination matrix SHALL retain the value set during the zero-fill step (i.e., 0)

---

### Requirement: Graceful Fallback for Full-Image ROI

When the expanded ROI equals or exceeds the full image dimensions (`roi_w = W`, `roi_h = H`), the pipeline SHALL continue to operate correctly using the same ROI code path without special-casing. The `small_buf` and adjusted trafo are equivalent to the original full-image behavior.

#### Scenario: Full-image ROI degrades gracefully

- **WHEN** the expanded BBox covers the entire image (e.g., geometry fills most of the print bed)
- **THEN** `small_buf` grows to `W × H` bytes, and the pipeline produces output identical to the pre-optimization slow path

#### Scenario: No special branch for full-image case

- **WHEN** `roi_w == W AND roi_h == H`
- **THEN** the code SHALL NOT take any alternative branch; the standard ROI path SHALL handle this case correctly

---

### Requirement: Support 二值軌 ROI-Local 合成

`composite_support_binary(dst, support_polys, res, pxdim, trafo)` SHALL 僅在 support 幾何的局部 ROI 內把 support-track 光柵化為純二值（contour→255、holes→0），再以 ROI-local `cv::max` 合成進全幀 `dst`，取代「全幀 support 緩衝 + 全幀 `cv::max`」。`rasterize_layer_dual` SHALL 移除 `support_tmp` 全幀緩衝參數，改委派此函式。

ROI 框 `{x0,y0,x1,y1}` SHALL 沿用既有「Pixel-Space ROI Computation」的世界 bbox→像素 bbox、flipXY 軸交換、mirror lo/hi 互換、floor/ceil + clamp 到影像（皆無浮點截斷）。`composite_support_binary` SHALL **不**使用該計算產生的 `new_trafo`（其 `static_cast<coord_t>` 中心截斷會擾動邊緣像素）。

`support_polys` 為空時 SHALL 早退，不觸碰 `dst`。

#### Scenario: ROI 合成輸出與全幀逐像素相同

- **WHEN** 同一層的 support-track 分別經「全幀 `cv::fillPoly` + 全幀 `cv::max`」與 `composite_support_binary` 的 ROI 合成
- **THEN** 合成後 `dst` 的每一像素值完全相同（byte-identical）

#### Scenario: 洞內保留 model 像素

- **WHEN** support ExPolygon 含洞（`diff_ex` 去除與 model 重疊處），洞內存在 model 像素
- **THEN** ROI 內洞區域於局部緩衝為 `0`，`cv::max` 後該處 model 像素原值保留，不被覆寫為 255

#### Scenario: 空支撐層早退

- **WHEN** 某層 `support_polys.empty()`
- **THEN** `composite_support_binary` 直接返回，`dst` 維持 model-only 結果，不配置或清零任何緩衝

---

### Requirement: 整數平移座標映射

`composite_support_binary` SHALL 以 fast-path `to_cv_point` 在**全幀精度**算出每個 support 頂點的整數像素座標 `(Px, Py) = (lround(px_full), lround(py_full))`（與舊全幀 `cv::fillPoly` 同式），再以整數平移 `(Px - x0g, Py - y0g)` 取得局部緩衝索引（`x0g/y0g` 為 guarded ROI 左上原點）。因平移量為整數，局部像素與全幀像素 SHALL 僅差一個整數位移，逐像素等價。

#### Scenario: 1/sx 非整數時仍 byte-identical

- **WHEN** `pxdim.w_mm / SCALING_FACTOR` 非整數（如 218.88mm / 13320px）
- **THEN** ROI 合成輸出與全幀輸出仍逐像素相同，不因任何中心截斷在 `.5` 捨入邊界翻動邊緣像素

---

### Requirement: Guard Band 邊界等價

ROI 框 SHALL 每邊外擴固定 guard 寬度 `g`（`g ≥ 1`）再 clamp 回 `[0,W)×[0,H)`：`x0g=max(0,x0-g)`、`y0g=max(0,y0-g)`、`x1g=min(W,x1+g)`、`y1g=min(H,y1+g)`，局部緩衝尺寸為 `roi_wg×roi_hg`。guard band 範圍 SHALL 確保「全幀版會填到的每一像素」皆落在局部畫布內、不被 `cv::fillPoly` 的畫布邊界裁切。多出的邊框像素為背景 `0`，合成時 `cv::max(dst,0)=dst`，不污染。

#### Scenario: 支撐緊貼影像邊緣不縮格

- **WHEN** support 多邊形最外緣像素恰落在 ROI 緊邊界（`lround` 觸及 `x1`/`y1` 欄）
- **THEN** 因 guard band 外擴，該邊緣像素仍在局部畫布內被正確填色，合成結果與全幀版相同，支撐邊緣不縮減一格

#### Scenario: guard 邊框不污染 model

- **WHEN** guarded ROI 涵蓋了 support bbox 以外、含 model 像素的邊框區
- **THEN** 該邊框於局部緩衝為 `0`，`cv::max` 後 model 像素原值保留

---

### Requirement: 局部緩衝清零生命週期

`composite_support_binary` SHALL 在 `cv::fillPoly` 之前，對 thread-local 局部緩衝以 `std::memset` 清零**本層 guarded ROI 有效區**（`roi_wg × roi_hg` bytes），不得僅清 `resize` 新增段、亦不依賴 `cv::fillPoly` 或 `std::vector::resize` 自動歸零。緩衝 SHALL 為 thread-local 跨層重用（維持零 malloc 契約），不得改用每層新配置的非 TLS buffer。

#### Scenario: 跨層殘留不滲漏

- **WHEN** Layer N-1 在某區域有大面積 support、Layer N 在不同區域僅有小面積 support，且兩層共用同一 thread-local 緩衝
- **THEN** Layer N 合成後，Layer N-1 support 曾覆蓋而 Layer N 未覆蓋的區域像素為 `0`（無上一層殘留 255 滲漏）

#### Scenario: 清零範圍涵蓋 guard 邊框

- **WHEN** 某層 guarded ROI 大於該層 support 實際覆蓋範圍
- **THEN** guarded ROI 內所有未被 `fillPoly` 寫入的像素（含 guard 邊框）皆為 `0`

