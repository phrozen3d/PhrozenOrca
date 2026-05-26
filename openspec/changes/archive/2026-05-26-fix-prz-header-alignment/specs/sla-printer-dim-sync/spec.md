## MODIFIED Requirements

### Requirement: 修復範圍不涉及 portrait swap 邏輯

`prz_header()` SHALL 以下列規則寫入 PRZ header 的 X/Y 軸欄位，**不得交換** X 與 Y 軸對應：

- `XResolution` MUST 取自 `display_pixels_x`
- `YResolution` MUST 取自 `display_pixels_y`
- `PlatformXLength` MUST 取自 `display_width`
- `PlatformYLength` MUST 取自 `display_height`

且 `display_mirror_x` 與 `display_mirror_y` 的寫入語意 MUST 對稱——皆為「`mirror = false → 0; true → 1`」。

本需求由先前「修復 SLAPrinterSettingsDialog 同步」變更引入，當時刻意將 `prz_header()` 的 X/Y 軸交換保留為「目前行為」。實機驗證顯示該交換是錯誤補償——光柵化端輸出的 `cv::Mat` 維度已是物理正確（`cols = display_pixels_x`、`rows = display_pixels_y`），交換寫入導致檔頭與 byte 流不一致、韌體解 RLE 時誤判尺寸；本變更將該需求由「保留交換」改為「禁止交換」。

#### Scenario: PRZ header 像素解析度與物理光柵維度一致

- **WHEN** `prz_header()` 在修正後對任意 Phrozen 機種匯出 PRZ
- **THEN** header 寫入的 `XResolution` 數值等於同一張 layer `cv::Mat` 的 `cols`（亦即 `display_pixels_x`），`YResolution` 數值等於 `rows`（亦即 `display_pixels_y`）

#### Scenario: PRZ header 平台尺寸與機種設定值方向一致

- **WHEN** `prz_header()` 在修正後對任意 Phrozen 機種匯出 PRZ
- **THEN** header 寫入的 `PlatformXLength` 數值等於機種設定的 `display_width`，`PlatformYLength` 數值等於 `display_height`，不做交換

#### Scenario: X mirror 與 Y mirror 寫入規則對稱

- **WHEN** 機種設定 `display_mirror_x = true` 且 `display_mirror_y = true`
- **THEN** PRZ header 的 `xm` 欄位寫入 `1`、`ym` 欄位寫入 `1`

#### Scenario: 雙 mirror 設為 false 時皆輸出 0

- **WHEN** 機種設定 `display_mirror_x = false` 且 `display_mirror_y = false`
- **THEN** PRZ header 的 `xm` 欄位寫入 `0`、`ym` 欄位寫入 `0`

#### Scenario: Mighty Revo 16K 修正後 xm 對齊 Chitubox 輸出

- **WHEN** 在 Mighty Revo 16K 機種（其 profile 內 `display_mirror_x = true`）匯出 PRZ
- **THEN** PRZ header 的 `xm` 欄位寫入 `1`，與 Chitubox 同機種輸出一致

#### Scenario: Sonic Mega 8K S / V2 Normal 預設輸出 xm=0 ym=0

- **WHEN** 在 Sonic Mega 8K S 或 Sonic Mega 8K V2 機種（其 profile 內明文設定 `display_mirror_x = false` 且 `display_mirror_y = false`，代表 Phrozen 韌體 `Normal` 狀態）匯出 PRZ
- **THEN** PRZ header 的 `xm` 欄位寫入 `0`、`ym` 欄位寫入 `0`

#### Scenario: Mighty Revo 16K LCD_mirror 預設輸出 xm=1 ym=0

- **WHEN** 在 Mighty Revo 16K 機種（其 profile 內明文設定 `display_mirror_x = true` 且 `display_mirror_y = false`，代表 Phrozen 韌體 `LCD_mirror` 狀態）匯出 PRZ
- **THEN** PRZ header 的 `xm` 欄位寫入 `1`、`ym` 欄位寫入 `0`

---

### Requirement: sync_local_to_tab() 必須同步寫回三個平台尺寸 config key

`SLAPrinterSettingsDialog::sync_local_to_tab()` 在將 Size X/Y 寫入 `printable_area` 的同時，SHALL 同步呼叫 `set_key_value` 寫入 `display_width`（等於 size_x）與 `display_height`（等於 size_y），確保三個 config key 始終保持一致：

- `printable_area`：四角頂點多邊形，bounding box 的 width = size_x，height = size_y
- `display_width`：`ConfigOptionFloat`，值 = size_x（mm）
- `display_height`：`ConfigOptionFloat`，值 = size_y（mm）

#### Scenario: 使用者修改 Size X/Y 後三個 key 一致

- **WHEN** 使用者在 `SLAPrinterSettingsDialog` 修改 Size X 與 Size Y 並觸發 `sync_local_to_tab()`
- **THEN** `display_width` 的值等於使用者輸入的 Size X（mm），`display_height` 的值等於 Size Y（mm），且 `printable_area` 的 bounding box 與兩者相符

#### Scenario: PRZ 匯出反映使用者修改的平台尺寸

- **WHEN** 使用者在對話框修改平台尺寸後匯出 PRZ 檔案
- **THEN** PRZ header 中的 `PlatformXLength` 等於使用者輸入的 Size X（= `display_width`，無交換），`PlatformYLength` 等於 Size Y（= `display_height`，無交換）

#### Scenario: Rasterization 中心點計算使用修改後的尺寸

- **WHEN** 使用者修改平台尺寸後執行切片（slapsRasterize 步驟）
- **THEN** `SLAPrintSteps.cpp` 中讀取 `display_width`/`display_height` 的 raster shift 計算結果與使用者設定的 `printable_area` 中心點一致

## ADDED Requirements

### Requirement: Phrozen 機種設定檔的平台尺寸 SHALL 採兩位小數精度

[resources/profiles/](resources/profiles/) 下所有 Phrozen 機種的設定檔（`Phrozen.json` 及其包含的子 profile），其 `display_width` 與 `display_height` 欄位 SHALL 採兩位小數精度，與 LCD 機械標稱規格一致；不得使用整數量化值。

對應關係（依各機種 LCD 規格）：

| 機種 | `display_width` (mm) | `display_height` (mm) |
|---|---|---|
| Sonic Mega 8K S | 330.24 | 185.76 |
| Sonic Mega 8K V2 | 330.24 | 185.76 |
| Mighty Revo 16K | 211.68 | 118.37 |
| 其他 Phrozen 機種 | 依各自 LCD 機械規格 | 依各自 LCD 機械規格 |

`display_pixels_x / display_pixels_y` 維持整數（像素本來就是整數）。

#### Scenario: Sonic Mega 8K S 設定檔內尺寸為兩位小數

- **WHEN** 讀取 Sonic Mega 8K S 的機種設定檔
- **THEN** `display_width` 為 `330.24`，`display_height` 為 `185.76`

#### Scenario: PRZ 輸出的 PlatformXLength 自然帶兩位小數

- **WHEN** 使用 Sonic Mega 8K S 預設 profile 切片並匯出 PRZ
- **THEN** PRZ header 的 `PlatformXLength` 浮點值序列化為 `330.24`，`PlatformYLength` 為 `185.76`

#### Scenario: Mighty Revo 16K 設定檔內尺寸為兩位小數

- **WHEN** 讀取 Mighty Revo 16K 的機種設定檔
- **THEN** `display_width` 為 `211.68`，`display_height` 為 `118.37`

---

### Requirement: Phrozen 機種設定檔 SHALL 明文寫入 `display_mirror_x / y` 預設值

[resources/profiles/PhrozenSLA/machine/](resources/profiles/PhrozenSLA/machine/) 下所有受影響 Phrozen 機種的設定檔，**SHALL 明文寫入** `display_mirror_x` 與 `display_mirror_y` 的預設值；不得依賴 `PrintConfig.cpp` 全域預設（`mirror_x=true, mirror_y=false`）而省略不寫，避免 Phrozen 機種的 PRZ `xm / ym` 輸出隨全域預設值改動而漂移。

明文預設值對應表（依 Phrozen 韌體狀態定義）：

| 機種 | Phrozen 韌體狀態 | `display_mirror_x` JSON 值 | `display_mirror_y` JSON 值 |
|---|---|---|---|
| Sonic Mega 8K S | Normal | `"0"` | `"0"` |
| Sonic Mega 8K V2 | Normal | `"0"` | `"0"` |
| Mighty Revo 16K | LCD_mirror | `"1"` | `"0"` |

該需求與 [`sla-on-demand-prz-export`](../sla-on-demand-prz-export/spec.md) 的 `xm / ym` 對稱寫入規則組合後，保證 Phrozen 三款主力機種匯出之 PRZ header `xm / ym` 與 Phrozen 韌體期望硬體狀態完全一致；任何 Revo 16K 印件方向錯誤的回饋 SHALL 透過調整本檔 JSON `display_mirror_x / y` 修正，**禁止**在 [PhrozenPRZ.cpp](src/libslic3r/Format/PhrozenPRZ.cpp) 或 [SLAPrintSteps.cpp](src/libslic3r/SLAPrintSteps.cpp) 引入機種特化分支。

#### Scenario: Mega 8K S/V2 JSON 內 mirror_x/y 皆為 "0"

- **WHEN** 讀取 [Phrozen Sonic Mega 8K S.json](resources/profiles/PhrozenSLA/machine/Phrozen Sonic Mega 8K S.json) 或 [Phrozen Sonic Mega 8K V2.json](resources/profiles/PhrozenSLA/machine/Phrozen Sonic Mega 8K V2.json)
- **THEN** 檔案內含有明文鍵值 `"display_mirror_x": "0"` 與 `"display_mirror_y": "0"`，不依賴全域預設繼承

#### Scenario: Revo 16K JSON 內 mirror_x="1" 且 mirror_y="0"

- **WHEN** 讀取 [Phrozen Sonic Mighty Revo 16K.json](resources/profiles/PhrozenSLA/machine/Phrozen Sonic Mighty Revo 16K.json)
- **THEN** 檔案內含有明文鍵值 `"display_mirror_x": "1"` 與 `"display_mirror_y": "0"`，不依賴全域預設繼承

#### Scenario: Mega 機種匯出 PRZ 的 xm/ym 與明文 JSON 一致

- **WHEN** 以 Sonic Mega 8K S 或 V2 預設 profile 切片並匯出 PRZ
- **THEN** PRZ header 的 `xm = 0` 與 `ym = 0`，且整條 Phase 1 + Phase 1.5 + Phase 2 rasterize → R₉₀cw → 寫檔流程後，最終 LCD 顯示影像方向與 Chitubox 同機種輸出一致

#### Scenario: Revo 機種匯出 PRZ 的 xm/ym 與明文 JSON 一致

- **WHEN** 以 Mighty Revo 16K 預設 profile 切片並匯出 PRZ
- **THEN** PRZ header 的 `xm = 1` 與 `ym = 0`，且整條 Phase 1 + Phase 1.5 + Phase 2 rasterize → R₉₀cw → 寫檔流程後，最終 LCD 顯示影像方向與 Chitubox 同機種（LCD_mirror 模式）輸出一致
