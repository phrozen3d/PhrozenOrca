# Spec: SLA 切層預覽依平台物理比顯示

## Purpose

定義 SLA 逐層切片 raster 縮圖預覽（`SLASlice2DCanvas::render_texture_letterbox`）的顯示長寬比基準：必須鎖定**平台物理比例**（`printable_area` bounding box，與 portal 外框及向量預覽路徑同源），使非方形像素機型（如 Revo 16K）不再被壓扁，並對方形像素機型維持零回歸；另定義資料缺失/退化時的防禦性 fallback，確保預覽永不黑屏或除以零。相關：見 `sla-on-demand-preview`、`sla-preview-thumb-cache`、`sla-printer-dim-sync`。

## Requirements

### Requirement: 逐層 raster 預覽依平台物理比顯示

逐層切片 raster 縮圖預覽（`SLASlice2DCanvas::render_texture_letterbox`）的顯示長寬比 SHALL 取自平台物理比例 `platform_aspect_w_over_h()`（來源為 `printable_area` bounding box 的 bed mm 比，degenerate 時其內部 fallback 至 `display_width / display_height`），而非材質像素維度比（`m_tex_w / m_tex_h`）。

此規則 SHALL 對 portrait 與 landscape 機型一致適用：方位（旋轉與鏡像，`mirror_x` / `mirror_y` / `flipXY` 的 UV 處理）與比例正交，MUST NOT 因本規則而改變。本規則 MUST NOT 影響 PRZ 匯出、rasterizer、raster 參數快照、縮圖快取或底層切片檔案格式。

#### Scenario: 非方形像素機種（如 Revo 16K）不再變扁

- **WHEN** 使用者在非方形像素機型（例如 Revo 16K：像素 15120×6230、平台 211.68×118.37 mm，像素比 2.427 ≠ 物理比 1.788）下檢視逐層 raster 預覽
- **THEN** 影像 SHALL 以平台物理比 1.788（211.68 / 118.37）顯示
- **AND** 圓形截面 SHALL 呈現為正圓而非被拉寬的橢圓
- **AND** 影像 SHALL 填滿 portal 外框並置中，與向量預覽路徑及外框比例一致

#### Scenario: 方形像素機種（如 Mega 8K）維持零回歸

- **WHEN** 使用者在方形像素機型（例如 Mega 8K：像素 7680×4320、平台 330.24×185.76 mm，像素比 1.778 == 物理比 1.778）下檢視逐層 raster 預覽
- **THEN** 顯示比例 SHALL 維持 1.778，與本變更前像素級一致（數值 no-op）
- **AND** 方位（旋轉/鏡像）與既有顯示結果 SHALL 不變

#### Scenario: 物理比與材質方位同源一致

- **WHEN** 同一層同時可由向量路徑與 raster 縮圖路徑呈現
- **THEN** 兩條路徑 SHALL 使用同一物理比來源（`printable_area` bbox），顯示比例 SHALL 相等

### Requirement: 預覽比例的防禦性 fallback

當平台物理比無法取得或為退化值時，系統 SHALL 退回既有的材質像素比行為，且 MUST NOT 發生黑屏、除以零或例外。

#### Scenario: `m_print` 為空時退避

- **WHEN** `render_texture_letterbox` 執行時 `m_print == nullptr`
- **THEN** `aspect_img` SHALL 退回現行材質像素比公式（`portrait_lcd ? m_tex_h / m_tex_w : m_tex_w / m_tex_h`）
- **AND** 渲染 SHALL 正常完成，不丟出例外

#### Scenario: 平台物理比退化時退避

- **WHEN** `platform_aspect_w_over_h()` 回傳非有限值或 ≤ 0
- **THEN** `aspect_img` SHALL 退回現行材質像素比公式
- **AND** 渲染 SHALL 正常完成

#### Scenario: 材質或 portal 尺寸無效時早退

- **WHEN** `m_tex_id == 0` 或 `m_tex_w <= 0` 或 `m_tex_h <= 0` 或 `portal_w <= 0` 或 `portal_h <= 0`
- **THEN** 函式 SHALL 維持既有早退行為（不繪製），不進行任何除法運算