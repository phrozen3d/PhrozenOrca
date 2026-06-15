## Context

逐層切片預覽（`SLASlice2DCanvas`）有兩條顯示路徑，渲染同一層卻可能得到不同長寬比：

- **向量路徑**（`render_vector_model`）：用 `ortho_2d(min_x..max_x, min_y..max_y)` 投影到 `printable_area` bbox 的 bed mm，比例恆正確。
- **Raster 縮圖路徑**（`render_texture_letterbox`）：把快取縮圖材質 letterbox 進 portal，**目前以材質像素維度決定長寬比**：

```cpp
// 現況 (SLASlice2DCanvas.cpp:598-601)
const bool portrait_lcd = ... display_orientation == sladoPortrait;
const double aspect_img = portrait_lcd ? double(m_tex_h) / double(m_tex_w)
                                       : double(m_tex_w) / double(m_tex_h);
```

對方形像素機（Mega 8K：7680/4320 = 1.778 == 330.24/185.76）此值恰好等於物理比，無異常。但 Revo 16K 為**非方形像素**（15120/6230 = 2.427 ≠ 211.68/118.37 = 1.788），`aspect_img` 取到 2.427，使影像被拉寬成「變扁」。

關鍵幾何（已驗證）：縮圖由 `cv::resize(scale,scale)` 均勻縮放自 panel buffer，未把變形烙進材質；rasterizer 以非方形 `pxdim` 投影，使半徑 r 的圓在 buffer 內成為 `(2r/0.014) rows × (2r/0.019) cols` 的橢圓。要在螢幕還原成正圓，quad 的 `draw_w/draw_h` 必須等於 `(0.014·15120)/(0.019·6230) = 211.68/118.37 = platform_ar`。亦即：**`aspect_img` 應取平台物理比，而非材質像素比。**

`platform_aspect_w_over_h()`（同檔案內既有 helper）已回傳 `printable_area` bbox 的 `bw/bh`（degenerate 時 fallback 至 `display_width/height`），且為 portal 外框與向量路徑共用的同一來源。

## Goals / Non-Goals

**Goals:**
- 讓 raster 縮圖預覽的顯示長寬比鎖定 `platform_aspect_w_over_h()`（`printable_area` 平台物理比），使非方形像素機型不再變扁。
- 與 portal 外框、向量路徑三者同源、視覺一致；影像填滿外框且置中。
- 對方形像素機型維持 no-op、零回歸。
- 提供防禦性 fallback，任何資料缺失/退化情形不致黑屏或除以零。

**Non-Goals:**
- 不更動方位處理（`mirror_x`/`mirror_y`/`flipXY` 的 UV 翻轉與 90° 旋轉）——方位與比例正交，維持原樣。
- 不觸碰 PRZ 匯出、rasterizer（`RasterToCvMat`）、raster 參數快照、縮圖快取（`RasterCache`）、底層切片檔案格式。
- 不改變實機列印輸出（韌體本就按非方形像素貼合，列印正確）。
- 不處理「`printable_area` ≠ 整片 LCD」的假想未來機型差異——使用者已拍板：唯一基準鎖定 `printable_area`，與向量路徑行為一致即可。

## Decisions

### D1：以 `platform_aspect_w_over_h()` 取代材質像素比作為 `aspect_img`

把 `aspect_img` 的來源由 `m_tex_w/m_tex_h` 改為呼叫既有 helper `platform_aspect_w_over_h(m_print)`，回傳值即 `printable_area` bbox 的 `bw/bh`（bed-XY、landscape 方向）。

- **Rationale**：portal 外框與向量路徑本就用此來源；三者同源可由建構面保證一致，並讓影像剛好填滿外框（`aspect_img == aspect_vp`），消除「變扁」與多餘 letterbox 黑邊。
- **portrait/landscape 不需再分支**：`platform_ar` 已是 bed-XY（長軸水平）比；portrait 機型的 90° UV 旋轉由既有方位邏輯處理，旋轉後螢幕水平軸對應長軸（display_width），故顯示比例直接等於 `platform_ar`，無論 portrait 或 landscape 皆成立。
- **Alternatives considered**：
  - (a) 改用 `display_width / display_height`：對現有三台機型與 `printable_area` 相等，但與 portal／向量路徑來源不同步，未來若 `printable_area ≠ LCD` 會與外框打架。**否決**（使用者鐵律：唯一基準鎖 `printable_area`）。
  - (b) 在材質像素比上乘像素縱橫補償係數 `pxdim.h_mm/pxdim.w_mm`：數學等價但需引入 raster_params、邏輯較繞，且仍非與外框同源。**否決**（D1 更簡潔且同源）。
  - (c) 修正 Revo 16K profile 的 `display_pixels_y` 使像素方形：已於質詢確認硬體確為非方形像素，且會牽動 PRZ／實機輸出。**否決**（違反鐵律、且實機本來正確）。

### D2：防禦性 fallback 退避機制

`aspect_img` 計算前後加守衛，異常時退回現行「材質像素比」舊行為，確保不致黑屏／除零：

退避判斷（任一成立即 fallback）：
- `m_print == nullptr`；或
- `platform_aspect_w_over_h()` 回傳非有限值或 ≤ 0（degenerate）。

fallback 值即現行公式 `portrait_lcd ? m_tex_h/m_tex_w : m_tex_w/m_tex_h`（保留既有分支），等同「行為不退步」的安全底線。

- **Rationale**：helper 內部對 `printable_area` 空、寬高 < 1e-9 已回傳 1.0；但 1.0（正方）對非方形機仍是錯誤比例，故僅在「資料缺失/退化」時退回舊行為而非硬套 1.0；正常情形一律採平台物理比。
- **既有的下游保護維持**：函式開頭 `m_tex_w <= 0 || m_tex_h <= 0 || portal_w/h <= 0` 早退與 letterbox 的 `aspect_vp` 比較邏輯不動，避免除零。

### D3：零回歸保證（方形像素機）

方形像素機 `platform_ar == 材質像素比`，故 D1 對 Mega 8K 等為數值 no-op；UV／方位邏輯未動 → 既有正常機型像素級不變。

## Risks / Trade-offs

- **[未來 `printable_area < 整片 LCD` 的假想機型]** → 預覽會把整片 LCD 的切片內容等比塞進「可列印平台」框（與向量路徑同行為）。使用者已明確接受此語意（唯一基準鎖 `printable_area`）；非本變更回歸風險。
- **[landscape 非方形像素機型未經實機驗證]** → 目前無此機型；D1 在 landscape 同樣取 `platform_ar`，邏輯上正確，但缺實機樣本。Mitigation：fallback 守衛保底，且方位 UV 邏輯維持原 design D7 限制範圍，不擴大風險面。
- **[helper degenerate 回傳 1.0 與 fallback 條件重疊]** → 設計上「`printable_area` 空」會走 helper 內 `display_width/height` 分支而非 1.0；只有寬高皆退化才回 1.0，此時 fallback 條件 `≤0` 不觸發、會採 1.0。Mitigation：可在 D2 守衛額外排除「helper 等於 1.0 但材質明顯非方形」的情形——列為實作時的細節判斷，不影響現有三台機型。

## Migration Plan

- 單檔單點修改，無資料遷移、無設定變更、無格式版本變動。
- Rollback：回退 `render_texture_letterbox()` 內 `aspect_img` 一處運算式即可，無其他相依。
- 既有 raster 快取無需失效（縮圖內容不變，僅顯示比例改變）。

## Open Questions

- D2 是否需要對「helper 回傳 1.0 但材質縱橫明顯非 1」的邊界額外加判斷？對現有三台機型無影響，留待實作評估後決定，不阻擋本設計。