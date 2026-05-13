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

