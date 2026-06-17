## Context

`prz-support-binary-rasterization` 落地雙軌光柵化後，支撐合成走「全幀」路線：`rasterize_layer_dual`（[RasterToCvMat.cpp:441](../../../src/libslic3r/SLA/RasterToCvMat.cpp#L441)）先以 `expolygons_to_cvmat(support_tmp,…)` 把支撐填滿一張**全幀** `cv::Mat`（13320×5120 ≈ 65MB），再以全幀 `cv::max(dst, support_tmp, dst)` 合成。代價：

- **記憶體**：主迴圈 `TLSData.support_mat`（[SLAPrintSteps.cpp:1535](../../../src/libslic3r/SLAPrintSteps.cpp#L1535)）與 PhrozenPRZ `support_tls`（[PhrozenPRZ.cpp:790](../../../src/libslic3r/Format/PhrozenPRZ.cpp#L790)）各為每緒一張全幀 Mat，8 緒 +~520MB（前 change 自記為「Opt-2 技術債」）。
- **速度**：每層多吃約 4 趟全幀流量（support `setTo(0)` 1 趟 + `cv::max` 讀 dst／讀 support／寫 dst 3 趟）。

既有 ROI 機制 `compute_pixel_roi`（[RasterToCvMat.cpp:109](../../../src/libslic3r/SLA/RasterToCvMat.cpp#L109)）目前僅服務 model 的 AGG slow-path，且其 2.4 步對 `new_trafo.center` 做 `static_cast<coord_t>` 截斷——這對「ROI 即唯一路徑」的 model 無害，但若拿來服務「必須與舊全幀逐像素相同」的 support，截斷會在邊緣像素的 `lround` 翻格。

本設計以 **support ROI 合成** 根治上述兩者，且維持與舊全幀輸出 **byte-identical**（故不動快取版本）。

## Goals / Non-Goals

**Goals:**
- 以局部 ROI 取代全幀 support 緩衝，消除 `support_mat` / `support_tls`（−~520MB），並砍掉每層約 4 趟全幀流量。
- ROI 合成輸出與舊全幀 `cv::max` **逐像素相同**，使既有 disk cache 仍有效、`CACHE_VERSION` 不變。
- 主迴圈與 PhrozenPRZ cache-miss 共用同一支 `composite_support_binary`，維持雙路徑 byte 一致。

**Non-Goals:**
- 並行度調整（12 緒）、P2 band/tiled rotate 融合、消除 `mat_rotated`、thumbnail resize 優化、dst 全幀 memset 消除 → 皆為 profile 實證後的後續技術債。
- model slow-path 行為、AA 演算法、SL1 PNG 路徑、UI/GUI。

## Decisions

### D1. 整數平移繞開浮點截斷；ROI box 沿用既有計算、丟棄 new_trafo

新增 `composite_support_binary(cv::Mat& dst, const ExPolygons& support_polys, const Resolution&, const PixelDim&, const RasterBase::Trafo& trafo)`。

- **ROI box**：沿用 `compute_pixel_roi` 的 2.1–2.3（世界 bbox → 像素 bbox、flipXY 軸交換、mirror lo/hi 互換、floor/ceil + clamp 到影像），取得整數框 `{x0,y0,x1,y1}`。這部分**無截斷**（純 floor/ceil/clamp），可安全重用。
- **丟棄 new_trafo**：**不**使用 2.4 算出的 `new_trafo`（其 `static_cast<coord_t>` 截斷會擾動邊緣 `lround`）。
- **整數平移公式**：每個支撐多邊形頂點 `p` 仍以 fast-path 的 `to_cv_point(p, sx,sy,cx,cy, …)` 在**全幀精度**算出整數像素 `(Px,Py) = (lround(px_full), lround(py_full))`（與舊全幀 `cv::fillPoly` 完全同式），再平移為局部索引 `(Px - x0g, Py - y0g)`（`x0g/y0g` 見 D2 guarded 原點）。因 `x0g,y0g` 為整數，`lround(px_full) - x0g` 與全幀像素只差一個整數位移 → **數學上逐像素相同**。

**理由**：唯一能「不 bump `CACHE_VERSION`、舊全幀 disk cache 續用」的作法。
**替代（捨棄）**：重用 `compute_pixel_roi` 的 `new_trafo` 直接餵 AGG/座標 → 截斷在 `1/sx` 非整數（真實機台幾乎皆是）時翻動邊緣像素，破壞 byte-identical。

### D2. Guard Band 範圍與 clamp 邊界

ROI 框每邊外擴 `g`（建議 `g = 2`，下限 1）再 clamp 回影像：

```
x0g = max(0, x0 - g)      y0g = max(0, y0 - g)
x1g = min(W, x1 + g)      y1g = min(H, y1 + g)
roi_wg = x1g - x0g        roi_hg = y1g - y0g
```

**理由**：`cv::fillPoly` 會先把多邊形邊裁切到「目標畫布矩形」再掃描；縮小畫布等於換了裁切框。guard band 確保「全幀版會填到的每一像素」都落在局部畫布內部、不被邊界裁切，把邊界等價性從「需證明 OpenCV top-left/subpixel 規則」變成「結構上不可能出錯」（未來升 OpenCV 版亦不破功）。多出的邊框為背景 0，`cv::max(dst,0)=dst` 不污染。
**替代（捨棄）**：嚴格證明 `lround` 上界不觸及 `x1`/`y1` 欄 → 脆弱、與 OpenCV 實作綁定。

### D3. 局部緩衝清零的生命週期與範圍（CRITICAL）

`composite_support_binary` 內，於 `cv::fillPoly` **之前**，對 TLS 緩衝 `small_buf` 強制清零：

```
small_buf.resize(roi_wg * roi_hg);
std::memset(small_buf.data(), 0, size_t(roi_wg) * size_t(roi_hg));   // 本層 guarded 有效區
```

- **範圍**：清「本層 guarded ROI 有效區」`roi_wg*roi_hg`，**非**整個 TLS 容量、亦**非**僅 `resize` 新增段。
- **為何必要**：`cv::fillPoly` 只寫多邊形內部、不碰背景；`small_buf` 是 TLS 跨層重用，且 `std::vector::resize(n)` 當 `n ≤ 容量`時**不歸零**保留資料 → 上一層支撐殘留的 255 會被 `cv::max` 蓋進 dst，造成稀疏、隨機的「跨層幽靈支撐滲漏」（肉眼預覽抓不到、卻破壞 byte-identical 與列印）。
- **非對稱陷阱記號**：此清零責任**僅** `fillPoly`+ROI 需要；AGG slow-path 由 `RasterGrayscaleAA` 建構時 clear、全幀 fast-path 由 `dst.setTo(0)` 代勞。原始碼須加 `// CRITICAL:` 警語，明示「與 AGG/全幀清零語義不同，刪除將導致跨層幽靈支撐」，避免後人比照「不需顯式清零」結論誤刪。

### D4. TLS 重用安全性與雙路徑共用唯一真相源

- **零 malloc 契約**：`small_buf` 為 `composite_support_binary` 內的 `thread_local std::vector<uint8_t>`（與 `expolygons_to_cvmat` 內部那支函式作用域 thread_local 各自獨立、互不可見）。同一層在同一緒上是 `model → LUT → support` **循序**呼叫，兩緩衝不同時活躍；跨緒各自 thread_local → **無 data race**。風險只在「同緒跨層時間殘留」，由 D3 memset 封殺。
- **不改用非 TLS buffer**：維持零 malloc，拒絕「每層獨立 buffer 換自動歸零」。
- **雙路徑共用**：`rasterize_layer_dual` 移除 `support_tmp` 參數與全幀 `cv::max`，改委派 `composite_support_binary(dst, support_polys, …)`；主迴圈與 PhrozenPRZ cache-miss 皆經此唯一函式 → byte 一致由建構保證。

### D5. 不 bump `CACHE_VERSION` 的算術定性

D1 整數平移使 ROI 合成與舊全幀 `cv::max` 對 `dst` 的最終像素**完全相同**（ROI 外全幀為 0，`cv::max(dst,0)=dst`；ROI 內 `small_buf == support_full(roiRect)`）。故：

- `CACHE_VERSION` **維持不變**；既有 disk cache 不失效、不重建。
- 跨版本 cache-hit（舊 bytes）與 cache-miss（新 ROI bytes）**不分歧**。
- 此一致性由階段二「Opt-2 前後逐 byte 比對」與「cache-hit vs cache-miss 比對」雙重把關。

### D6. 保留 cv::rotate、不調並行度

- **否決天真逐像素 rotate 融合**：ROTATE_90_CW 的 `dst(r,c)=src(H-1-c,r)`，按 rotated row-major 走會變成對 `src` 的**跨步（stride=W=13320B）column 掃描** → 近 100% cache miss + TLB 抖動 + prefetch 失效，且須親手複合 rotate∘mirror（byte 漂移風險）。1 趟跨步很可能比「`cv::rotate` tiled 轉置 + 順序 RLE」那 2 趟還慢數倍。
- **保留 `cv::rotate`**：承認它是「把跨步轉置一次做完、讓 RLE 順序化」的必要優化。
- **並行度**：`RASTERIZE_CONCURRENCY` 維持 8（最多保守至 10）。提速主引擎是 Opt-2 砍掉的 4 趟全幀流量；迴圈每層每緒約 9 趟全幀 65MB，**很可能記憶體頻寬受限**，盲加緒無益且推高峰值（P1 保留 `mat_rotated`，每緒地板仍 ~170MB，8→12 多 680MB > 省下的 520MB）。12 緒、P2 band 融合留待 profile 實證。

## Risks / Trade-offs

- **跨層幽靈支撐滲漏（最毒）** → D3 強制 memset + `// CRITICAL` 警語 + 專屬回歸測試（Layer N-1 右下大支撐、Layer N 左上小支撐，斷言 N 右下角 100% 為 0）作為 CI 門禁。
- **邊界裁切不等價導致支撐縮一格** → D2 guard band 結構性消除。
- **byte 漂移使舊 cache stale** → D1 整數平移數學保證相同；階段二逐 byte 比對把關；不 bump 版本是「主動宣告相同」而非「賭它相同」。
- **重用 `compute_pixel_roi` box 計算與 fast-path 座標式不一致** → 兩者本就同源於 `to_cv_point` 的 sx/sy/cx/cy 與 mirror/flipXY 規則；只取 box（2.1–2.3）、丟 new_trafo（2.4）即對齊。
- **提速幅度不如「炸裂式」期待** → 已誠實定性：Opt-2 後半段頻寬 −40~45% 是實打實提速；並行度提升因頻寬牆很可能收益趨零，故不列硬指標。

## Migration Plan

- 純內部光柵化重構，無檔案格式 / profile / API 變更。
- `CACHE_VERSION` 不變 → 既有 disk cache 直接續用、使用者無感、無需重切。
- Rollback：revert 本 change 即回到全幀 support 合成；因 byte-identical，回滾不影響既有 cache 或既有 PRZ 輸出。

## Open Questions

- 並行度上調（10→12+）與 P2 band/tiled rotate 融合（消 `mat_rotated`）須先 profile 證明「非頻寬受限」且「rotate 佔比可觀」後，另開 change 處理。
- `g`（guard band 寬度）取 1 或 2：1 已足以涵蓋 `lround` 邊界翻格，2 為保險；實作時固定為常數並註記理由。