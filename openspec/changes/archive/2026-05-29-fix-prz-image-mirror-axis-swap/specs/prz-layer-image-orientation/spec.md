## ADDED Requirements

### Requirement: 逐層曝光影像方向 SHALL 逐機台對齊 Chitubox

`.prz` 每一層的曝光影像方向 SHALL 與 Chitubox 對「同一台機型」輸出的影像逐機台一致。系統 SHALL 在後置 `ROTATE_90_CLOCKWISE` 之後，依該機型「最終 X 鏡像狀態」施加一次補償翻轉：當最終 X 鏡像為 false 時施加垂直翻轉（`cv::flip` code 0），為 true 時施加水平翻轉（`cv::flip` code 1）。此「最終 X 鏡像狀態」SHALL 與 PRZ header 寫入的 Xmirror 位元組同源（由 `display_mirror_mode` 推導，`lcd_mirror→true`、`normal`/`dlp_normal→false`，缺值時回退 `display_mirror_x`），不得另立第二事實來源。

#### Scenario: Mega 8K S / V2（normal）影像正立

- **WHEN** 以 `display_mirror_mode = normal` 的 Phrozen Sonic Mega 8K S 或 Mega 8K V2 機型切片並產出 `.prz`
- **THEN** 每層曝光影像方向與 3D 物件預覽一致（正立）：字序由左至右不變、字體不上下顛倒
- **AND** 與 Chitubox 對同機型、同 STL 輸出的層影像方向一致

#### Scenario: Revo 16K（lcd_mirror）影像 X 鏡像

- **WHEN** 以 `display_mirror_mode = lcd_mirror` 的 Phrozen Sonic Mighty Revo 16K 機型切片並產出 `.prz`
- **THEN** 每層曝光影像呈 X 鏡像（左右反轉、字序反向），但字體不上下顛倒
- **AND** 與 Chitubox 對同機型、同 STL 輸出的層影像方向一致

#### Scenario: 修正前的錯誤方向不得再出現

- **WHEN** 任一目標機型切片產出 `.prz`
- **THEN** 不得出現修正前的症狀：Mega 不得呈純垂直鏡像（字體上下顛倒）、Revo 不得呈完全正立而缺少應有的 X 鏡像

### Requirement: 輸出緩衝維度 SHALL 維持橫向佈局

補償翻轉 SHALL NOT 改變輸出影像的寬高維度。最終寫入 `.prz` 的緩衝 SHALL 維持橫向佈局，每列像素數等於 `display_pixels_x`，與 PRZ header 的 `XResolution` 一致。

#### Scenario: Mega 8K 維度不變

- **WHEN** Mega 8K S 機型產出層影像
- **THEN** 最終影像維度為 7680（每列像素）× 4320（列數），與 header `XResolution=7680`、`YResolution=4320` 一致

#### Scenario: 翻轉不改變維度

- **WHEN** 補償翻轉施加於旋轉後的橫向 Mat
- **THEN** 該 Mat 的 `rows` 與 `cols` 在翻轉前後保持相同，且仍為 `CV_8UC1` row-major 佈局

### Requirement: cache-hit 與 cache-miss 路徑 SHALL 位元組一致

主路徑（切片時寫入 raster cache）與 cache-miss 備援路徑（匯出時即時光柵化）SHALL 對相同輸入產生位元組一致的層影像與 RLE 串流。補償翻轉 SHALL 同時、等效地施加於兩個旋轉點，不得僅施加於其中之一。

#### Scenario: 兩路徑輸出相同

- **WHEN** 同一層分別經由 cache-hit（串流預編碼位元組）與 cache-miss（即時光柵化）產出
- **THEN** 兩者產生的 PRZ 層位元組完全相同

#### Scenario: 僅修正單一旋轉點視為不符規範

- **WHEN** 補償翻轉只施加於匯出端而未施加於寫入 cache 的主路徑（或反之）
- **THEN** 此實作不符本規範，因 cache-hit 路徑仍會輸出錯誤方向

### Requirement: 版本更新 SHALL 強制失效舊切片快取

當層影像方向邏輯改變時，系統 SHALL 透過提升 raster cache 的 `CACHE_VERSION` 強制失效既有快取，確保使用者更新版本後，舊版以錯誤方向編碼的快取不會被誤用而串流至新的 `.prz`。

#### Scenario: 提升 CACHE_VERSION 使舊快取失效

- **WHEN** 本次方向修正合入，且 `CACHE_VERSION` 已較前一版提升
- **THEN** 由舊版本產生的快取項其 cache key（含 `CACHE_VERSION` 雜湊）與新版不符，新版視為 cache-miss

#### Scenario: 更新後首次匯出採用修正後方向

- **WHEN** 使用者更新版本後，對先前曾切片（且舊快取仍存在）的模型重新匯出 `.prz`
- **THEN** 系統不採用舊快取位元組，改以修正後的方向重新光柵化或重建快取
- **AND** 輸出層影像方向符合「逐機台對齊 Chitubox」之規範