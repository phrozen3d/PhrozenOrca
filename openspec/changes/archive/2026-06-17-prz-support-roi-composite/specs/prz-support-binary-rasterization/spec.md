## MODIFIED Requirements

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