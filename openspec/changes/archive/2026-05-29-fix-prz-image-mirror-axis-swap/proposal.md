## Why

web_slicer_core（DS-online）切片輸出的 `.prz` 逐層影像，與 Chitubox 對「同一台機型」的輸出在方向上不一致：Phrozen Sonic Mega 8K S / V2 呈現**純垂直鏡像（字體上下顛倒、字序不變）**，而 Mighty Revo 16K **缺少應有的 X 鏡像（完全正立）**。以 Chitubox 為對外事實標竿時，這會讓同一 STL 在不同切片器上產生方向不同的曝光影像，造成 QA 比對困難，且在實機上可能列印出鏡像/翻轉錯誤的成品。

根因已釐清：`RasterBase::Trafo` 先套用 `mirror_x` / `mirror_y`，但 [PhrozenPRZ.cpp:809](src/libslic3r/Format/PhrozenPRZ.cpp#L809) 之後又對整張 `cv::Mat` 做**無條件 `ROTATE_90_CLOCKWISE`**。這 90° 旋轉把兩個鏡像軸整體轉了 90°，導致鏡像意圖被投影到錯誤的軸上：

```
            應控制的最終軸         旋轉後實際控制的軸
config        ──────────         ────────────────────
mirror_x  →   左右 / 字序     →   ❌ 變成控制 上下 / 字體倒立
mirror_y  →   上下 / 字體     →   ❌ 變成控制 左右 / 字序
```

- Mega（trafo `mirror_x = !1 = false`）→ 字體上下顛倒、字序保留 = 純垂直鏡像。
- Revo（trafo `mirror_x = !0 = true`）→ 字體正立、字序正常 = 完全正立、缺 X 鏡像。

此座標推導能同時、精確重現兩台機型被觀察到的現象。

## What Changes

- **修正鏡像與旋轉的軸向投影偏誤**：調整渲染管線中 `mirror_x` / `mirror_y` 與 `ROTATE_90_CLOCKWISE` 的套用座標系／組合順序，使 `mirror_x` 對應到最終影像的「左右 / 字序」軸、`mirror_y` 對應到「上下 / 字體」軸——讓每台機型 `.prz` 逐層影像方向逐機台對齊 Chitubox。
  - 目標最終狀態：Mega 8K S / V2 = 正立（與物件一致）；Revo 16K = X 鏡像（左右反、字體不倒立）。
- **核心約束（不可違反）**：最終寫入 `.prz` 的緩衝區必須維持**橫向 7680×4320 佈局**（與 header `XResolution` / `YResolution` 一致）。旋轉的「轉置維度」成分承載輸出尺寸，**不得移除**；本次只調整鏡像／翻轉的組合，不改變輸出影像的寬高維度。
- **不更動任何參數**：machine / process / filament profile 的 `display_mirror_x/y`、`display_mirror_mode`、`display_orientation`、解析度等數值維持不變；header 的 Xmirror/Ymirror 位元組推導維持不變（已確認正確）。
- **不更動 UI 顯示與統計**：不影響 `SLAPrinterSettingsDialog`、`SLASlice2DCanvas` 等任何 UI 路徑。
- **兩條渲染路徑一致**：cache-hit 與 cache-miss（on-demand rasterization）兩條路徑必須產生位元組一致的影像方向。

## Capabilities

### New Capabilities

- `prz-layer-image-orientation`：規範 `.prz` 逐層曝光影像的方向必須逐機型對齊 Chitubox 事實標竿——明確定義 `mirror_x` / `mirror_y` 與最終 90° 旋轉在像素座標系中的組合，使 `mirror_x` 控制最終影像的水平（字序）軸、`mirror_y` 控制垂直（字體）軸；並規範輸出緩衝維持橫向 7680×4320 佈局、cache-hit 與 cache-miss 路徑方向一致。

### Modified Capabilities

（無）`sla-printer-preset-mirror-output` 規範的是 header Xmirror/Ymirror **位元組**與 UI 模式 round-trip，行為不變；`sla-roi-aa-raster-pipeline` 規範的是 ROI / zero-fill / AA 像素管線，其 transformation chain 的鏡像「套用步驟」存在但「最終軸向組合」未被規範，屬本次新增 capability 涵蓋範圍，故不修改既有 spec 的 requirement。

## Impact

**程式碼**
- [src/libslic3r/Format/PhrozenPRZ.cpp](src/libslic3r/Format/PhrozenPRZ.cpp) — layer 影像生成端的 `ROTATE_90_CLOCKWISE` 與鏡像套用順序（cache-miss 路徑 line ~806-810，及 cache-hit 路徑對應處）。
- 可能涉及 [src/libslic3r/SLA/RasterToCvMat.cpp](src/libslic3r/SLA/RasterToCvMat.cpp) / [src/libslic3r/SLA/RasterBase.hpp](src/libslic3r/SLA/RasterBase.hpp) 的 `Trafo` 鏡像套用座標系（具體落點由 design.md 決定）。

**不受影響**
- 所有 machine / process / filament profile 數值。
- header 數值欄位（含 Xmirror/Ymirror byte、Resolution、display_width/height）。
- 任何 UI 程式碼與 `SLAPrintStatistics`。
- SL1 / AnycubicSLA 等其他輸出格式。

**下游影響**
- 既有解讀 `.prz` 的工具／韌體會看到方向修正後的曝光影像；此為對齊 Chitubox 的預期結果。

**驗收方式**
- 同一 STL 經 web_slicer_core 切片產出 `.prz`，逐層影像方向與 Chitubox 同機型輸出逐機台一致：Mega 8K S / V2 正立、Revo 16K X 鏡像。
- 輸出影像維度仍為各機型對應的橫向解析度（Mega 7680×4320）。