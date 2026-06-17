# Spec: PRZ 支撐二值化光柵處理

## Purpose

定義 SLA→PRZ 光柵化的「模型／支撐雙軌幾何分流」：支撐區域於 PRZ 輸出時豁免抗鋸齒（AA）、灰階級別、模糊與全域減光（`picture_grayscale`），永遠輸出純白實心（255）；貼床底層整層強制二值以保附著力。目標是讓支撐尖端與接觸點固化面積最大化、不被影像柔化削弱，換取列印可靠度（取捨：光柵化時間約 +30~60%）。

## Requirements

### Requirement: 模型與支撐雙軌幾何分流

`merged_input_to_slices()` SHALL 停止在 `union_ex(trslices)` 將 `model_polygons` 與 `supports_polygons` 合併為單一光柵化來源，改為在 `PrintLayer` 上**僅持久持有兩份獨立的 transformed ExPolygons**：model-track 與 support-track（透過 `transformed_model_slices()` / `transformed_support_slices()` 兩個唯讀存取子供光柵化使用）。

`PrintLayer` SHALL **不再持久儲存 union**（移除 `m_transformed_slices` 成員，記憶體優化 Opt-1，避免為每層保留冗餘 union 副本）。既有 `transformed_slices()` 存取子 SHALL 改為**隨選（on-demand）即時計算**並**以值回傳** `union_ex(model-track ∪ support-track)`，其內容 SHALL 與分軌前版本完全相同，供向量預覽等既有消費者使用。

support-track 的內容 SHALL 與既有一致：`supports_polygons` 已先對 `model_polygons` 做 `diff_ex` 去除與模型重疊區域。

#### Scenario: 僅持久持有雙軌，union 隨選計算

- **WHEN** `merged_input_to_slices()` 處理任一層
- **THEN** `PrintLayer` 僅持久持有 model-track 與 support-track 兩份資料（不儲存 union）；呼叫 `transformed_slices()` 時即時回傳 `union_ex(model-track ∪ support-track)`，其值與分軌前版本完全相同

#### Scenario: 向量預覽取得隨選計算的 union

- **WHEN** `SLASlice2DCanvas` 向量 fallback 呼叫 `transformed_slices()`
- **THEN** 取得即時計算（值回傳）的完整 union 輪廓（含支撐），顯示行為與分軌前一致

---

### Requirement: 一般層差異化光柵處理

對層索引 `lid >= bottom_layer_count` 的一般層，光柵化 SHALL 對兩條 track 施以不同處理：

- **model-track** SHALL 套用既有完整處理：AGG 光柵化（`rp.gamma`）、AA 量化（`rp.aa_steps`）、灰階範圍映射（`rp.gray_lo` / `rp.gray_hi`）、模糊（`rp.blur_pixel`），其後套用 `apply_picture_grayscale_lut(rp.picture_grayscale)`。
- **support-track** SHALL 以 `gamma=0`、`aa_steps=0`、`blur_pixel=0` 光柵化，輸出純二值（像素值僅為 0 或 255），不套用 AA、灰階映射、模糊，亦不套用 `picture_grayscale` LUT。

輸入參數來源 SHALL 為 `SLARasterParams rp` 快照；兩條 track 的幾何皆先平移 `rp.shift`。

#### Scenario: 一般層支撐輸出為純二值

- **WHEN** 一般層（`lid >= bottom_layer_count`）的 support-track 被光柵化
- **THEN** support 覆蓋區域的像素值皆為 `255`，背景為 `0`，不存在中間灰階值，且不受 `anti_aliasing_level`、`gray_scale_level`、`image_blur_enable`、`picture_grayscale` 任何設定影響

#### Scenario: 一般層模型維持既有 AA 處理

- **WHEN** 一般層的 model-track 被光柵化，且 `anti_aliasing` 啟用或 `image_blur_enable` 啟用
- **THEN** model 覆蓋區域呈現 AA／灰階／模糊處理後的中間灰階值，並受 `picture_grayscale` 等比例縮放，與分軌前的模型像素輸出一致

---

### Requirement: 貼床底層整層強制二值化

對層索引 `lid < bottom_layer_count`（貼床底層）的層，光柵化 SHALL 對 **model-track 與 support-track 皆**以 `gamma=0`、`aa_steps=0`、`blur_pixel=0` 輸出純二值，跳過 AA、灰階映射、模糊。`picture_grayscale` LUT SHALL 仍套用於 model-track（support-track 不套用，恆 255）。

`bottom_layer_count` SHALL 取自 `m_tolerance_bottom_layer_count`，並隨 `SLARasterParams` 快照傳遞。底層判定 SHALL 與大象腳補償（faded layers）為獨立 predicate，不得互相引用或假設兩範圍相等。

#### Scenario: 底層模型不含 AA 中間值

- **WHEN** 某層 `lid < bottom_layer_count`，且 `anti_aliasing` 或 `image_blur_enable` 啟用
- **THEN** 該層 model 覆蓋區域的像素僅為二值（threshold 後 0 或 255，再經 picture_grayscale LUT 縮放），不出現 AA 羽化或模糊邊緣

#### Scenario: 底層支撐維持二值且豁免減光

- **WHEN** 某層 `lid < bottom_layer_count`
- **THEN** 該層 support 覆蓋區域像素恆為 `255`，不受 `picture_grayscale` 縮放

#### Scenario: 底層與大象腳範圍不一致時各自獨立

- **WHEN** `faded_layers = 8` 且 `bottom_layer_count = 4`
- **THEN** 層 0–3 同時套用大象腳幾何補償並強制二值；層 4–7 套用大象腳幾何補償但 model 仍走一般層 AA 處理；兩判定互不干擾

---

### Requirement: 像素合併邊界與 picture_grayscale 豁免

最終每層影像 SHALL 由 model-track 影像與 support-track 以逐像素取最大值合成：`output[i] = max(model_after_LUT[i], support_255[i])`。合成 SHALL 發生在 `picture_grayscale` LUT **之後**，使 support 的 `255` 不被全域減光削弱。

合成 SHALL 以 **support 局部 ROI** 進行（`composite_support_binary`）：僅在 support bbox（加 guard band、clamp 至影像）的局部緩衝內把 support-track 光柵化為純二值，再以 ROI-local `cv::max` 合成進全幀 model 影像，取代既有「全幀 support 緩衝 + 全幀 `cv::max`」。此 ROI 合成輸出 SHALL 與全幀合成**逐像素相同（byte-identical）**，故不變動 `CACHE_VERSION`。

合成順序 SHALL 確保：support 覆蓋處最終值恆為 `255`；support 覆蓋率低於門檻（threshold < 50%）的次像素區域輸出 `0`，不得使該處 model 像素變暗；support 洞（`diff_ex` 去除與 model 重疊處）內的 model 像素 SHALL 原值保留。

合成後影像 SHALL 為 thumbnail 擷取與 PRZ-RLE 編碼的唯一來源，使 Thumbnail 與 2D Preview 自動繼承合成結果。

#### Scenario: 支撐豁免全域減光

- **WHEN** `picture_grayscale = 200`，某一般層同時含 model 與 support
- **THEN** model 覆蓋區域最大值約為 `200`（受 LUT 縮放），support 覆蓋區域恆為 `255`（不受 LUT 影響）

#### Scenario: 模型與支撐相鄰邊界支撐維持銳利

- **WHEN** model 啟用模糊（`image_blur_enable`），且 support 與 model 邊界相鄰
- **THEN** model 側呈現模糊漸層、support 側維持銳利 `255` 邊緣；model 的模糊光暈不會軟化 support 邊緣（因 support 於 LUT 後以 max 蓋上）

#### Scenario: thumbnail 與合成輸出一致

- **WHEN** 某層完成雙軌合成
- **THEN** 該層 thumbnail 擷取自合成後（`picture_grayscale` LUT 與 support 合成之後、PRZ 方位旋轉之前）的影像，反映支撐二值化結果

#### Scenario: ROI 合成與全幀合成 byte-identical

- **WHEN** 同一層分別以全幀 `cv::max` 與 `composite_support_binary` 的 ROI 合成產生最終影像
- **THEN** 兩者每一像素值完全相同，PRZ-RLE 編碼結果一致，既有 disk cache 不失效

## Known Limitations

- **記憶體峰值（已根治）**：雙軌合成早期於主迴圈每執行緒新增一張全幀 `support_mat`（~65MB，8 緒約 +520MB）。已由 Opt-2（support ROI 合成，`composite_support_binary`）消除：support 改在局部 ROI 內合成，不再持有全幀 support 緩衝。
- **多物件 / 非 z=0 起始**：`is_binary = lid < bottom_layer_count` 假設全域前 `bottom_layer_count` 層為貼床底層；多物件不同起始高度時此映射可能需改 per-object 底層集合。