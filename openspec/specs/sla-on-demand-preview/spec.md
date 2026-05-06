# Spec: SLA On-Demand GUI 切層預覽（Strategy A）

## Requirement: SLASlice2DCanvas 採同步單層快取預覽（Strategy A）

`SLASlice2DCanvas::render()` SHALL 不再讀取 `m_print->layer_images()`，改為：
1. 當 `m_layer_idx` 與 `m_cached_layer` 不同時，呼叫 `expolygons_to_cvmat()` 即時光柵化當前切層
2. 套用 `picture_grayscale` LUT
3. 上傳 texture 後，局部 `cv::Mat` 離開 scope 自動釋放（不持有）
4. 以 `m_cached_layer` 快取 layer index，避免同一層重複計算

### Scenario: 切換至新 layer index 時即時光柵化
- **WHEN** 使用者移動 layer slider 至新的 index
- **THEN** `expolygons_to_cvmat()` 被呼叫一次，產生該層影像並上傳至 GPU texture，`cv::Mat` 不持有超過 `render()` 呼叫範圍

### Scenario: 同一 layer index 不重複光柵化
- **WHEN** `render()` 被呼叫且 `m_layer_idx == m_cached_layer`
- **THEN** `expolygons_to_cvmat()` 不被呼叫，使用現有 GPU texture 直接渲染

### Scenario: raster_params 為空時回退至向量渲染
- **WHEN** `raster_params().has_value()` 為 `false`（切片尚未完成）
- **THEN** `render_vector_fallback()` 被呼叫，不嘗試光柵化

### Scenario: 記憶體峰值為單層
- **WHEN** GUI 預覽任意切層
- **THEN** 記憶體中最多持有 1 張 `cv::Mat`（約 23 MB），不累積

---

## Requirement: render 的 guard 條件更新為 raster_params 存在性檢查

`SLASlice2DCanvas::render()` 內的「切片完成」判斷 SHALL 改為同時檢查 `is_step_done(slapsRasterize)` 與 `raster_params().has_value()`。

### Scenario: 兩個條件都滿足才進行光柵化預覽
- **WHEN** `is_step_done(slapsRasterize)` 為 `true` 且 `raster_params().has_value()` 為 `true`
- **THEN** `render()` 進入光柵化預覽分支

### Scenario: 任一條件不滿足時清除舊 texture
- **WHEN** `is_step_done(slapsRasterize)` 為 `false` 或 `raster_params().has_value()` 為 `false`
- **THEN** 現有 GPU texture 被銷毀（`destroy_texture()`），`m_cached_layer` 重設為 -1，呼叫 `render_vector_fallback()`
