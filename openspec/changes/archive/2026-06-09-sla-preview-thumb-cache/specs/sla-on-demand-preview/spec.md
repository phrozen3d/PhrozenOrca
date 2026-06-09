## MODIFIED Requirements

### Requirement: SLASlice2DCanvas 採同步單層快取預覽（Strategy A）

`SLASlice2DCanvas::render()` SHALL 不再讀取 `m_print->layer_images()`，亦 SHALL 不再於 render thread 上 on-demand 呼叫 `expolygons_to_cvmat()` 即時光柵化，改為：
1. 使用已快取的 cache key（見 `sla-preview-thumb-cache`）與 `m_layer_idx` 組出 `layer_{m_layer_idx:04d}_preview.rle` 路徑
2. 當 `m_layer_idx` 與 `m_cached_layer` 不同時，讀取並解碼該 `_preview.rle` 小圖（thumb 已內含 `picture_grayscale`，故 SHALL NOT 再呼叫 `apply_picture_grayscale_lut`）
3. 上傳 texture 後，局部影像離開 scope 自動釋放（不持有）
4. 以 `m_cached_layer` 快取 layer index，避免同一層重複解碼與上傳
5. 此路徑 MUST NOT 觸發任何 AGG 次像素計算或全解析度解碼

#### Scenario: 切換至新 layer index 時讀取 thumb 快取

- **WHEN** 使用者移動 layer slider 至新的 index
- **THEN** `render()` 讀取並解碼對應 `_preview.rle` 一次、上傳至 GPU texture，且 `expolygons_to_cvmat()` SHALL NOT 被呼叫

#### Scenario: 同一 layer index 不重複解碼

- **WHEN** `render()` 被呼叫且 `m_layer_idx == m_cached_layer`
- **THEN** 使用現有 GPU texture 直接渲染，不重新讀檔、解碼或上傳

#### Scenario: raster_params 為空時回退至向量渲染

- **WHEN** `raster_params().has_value()` 為 `false`（切片尚未完成）
- **THEN** `render_vector_fallback()` 被呼叫，不嘗試讀快取或光柵化

#### Scenario: thumb 缺席時退回向量且不觸發 AGG

- **WHEN** 目標層的 `_preview.rle` 讀取失敗或不存在
- **THEN** `render_vector_fallback()` 被呼叫，且 SHALL NOT 在 UI 執行緒重新觸發任何 AGG 計算

#### Scenario: 記憶體峰值為單層小圖

- **WHEN** GUI 預覽任意切層
- **THEN** 記憶體中最多持有 1 張已解碼的 thumb（遠小於全解析度 `cv::Mat`），不累積

---

### Requirement: render 的 guard 條件更新為 raster_params 存在性檢查

`SLASlice2DCanvas::render()` 內的「切片完成」判斷 SHALL 同時檢查 `is_step_done(slapsRasterize)`、`raster_params().has_value()` 與已快取 cache key 是否存在。

#### Scenario: 三個條件都滿足才進行點陣預覽

- **WHEN** `is_step_done(slapsRasterize)` 為 `true`、`raster_params().has_value()` 為 `true` 且 cache key 已快取
- **THEN** `render()` 進入讀 thumb 快取的點陣預覽分支

#### Scenario: 任一條件不滿足時清除舊 texture

- **WHEN** `is_step_done(slapsRasterize)` 為 `false`、`raster_params().has_value()` 為 `false` 或 cache key 尚未快取
- **THEN** 現有 GPU texture 被銷毀（`destroy_texture()`），`m_cached_layer` 重設為 -1，呼叫 `render_vector_fallback()`