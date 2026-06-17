## ADDED Requirements

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