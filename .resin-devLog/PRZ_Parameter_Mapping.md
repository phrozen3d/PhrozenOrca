# PRZ 參數映射文件

## Metadata

| 項目 | 值 |
|---|---|
| **審查日期** | 2026-05-14 |
| **PRZ 版本基準** | V3.0（`PhrozenPRZ.cpp` `prz_header()` 實作） |
| **UI 版本基準** | branch `phrozen-resin-dev`，commit `a80a4ff6a` |
| **審查範圍** | `SLAPrinterSettingsDialog.cpp` + `Tab.cpp` SLA 所有分頁 × `PhrozenPRZ.cpp prz_header()` + `prz_layer_content()` |

---

## Section 1 — 健康對應欄位

UI 有且 PRZ 有，對應正確無斷層的所有欄位。

### 1-A：SLAPrinterSettingsDialog（機台尺寸與顯示設定）

| UI Config Key | UI 控件位置 | PRZ Header 欄位名稱 | PRZ 資料型別 | 映射方式 | 健康狀態 | 備注 |
|---|---|---|---|---|---|---|
| `display_pixels_y` | SLAPrinterSettingsDialog / Resolution Y | `XResolution` | `int16 BE 2B` | portrait-swap | ✅ 正常 | LCD 長邊像素映射至 PRZ X 軸（列印方向長軸）；portrait 交換為刻意設計，使 cv::Mat width/height 與平台 X/Y 一致 |
| `display_pixels_x` | SLAPrinterSettingsDialog / Resolution X | `YResolution` | `int16 BE 2B` | portrait-swap | ✅ 正常 | 短邊像素映射至 PRZ Y 軸；同上 portrait 設計 |
| `display_height` | SLAPrinterSettingsDialog / Size Y（mm） | `PlatformXLength` | `float BE 4B` | portrait-swap | ✅ 正常 | 曾有同步斷層（`sync_local_to_tab()` 未寫回），**已於 fix-prz-platform-xy-mapping 修復** |
| `display_width` | SLAPrinterSettingsDialog / Size X（mm） | `PlatformYLength` | `float BE 4B` | portrait-swap | ✅ 正常 | 同上，**已於 fix-prz-platform-xy-mapping 修復** |
| `printable_height` | SLAPrinterSettingsDialog / Size Z（mm） | `PlatformZLength` | `float BE 4B` | direct | ✅ 正常 | |
| `display_mirror_x` | SLAPrinterSettingsDialog / Mirror X | `Xmirror` | `uint8 1B` | inverted（`false→1, true→0`） | ✅ 正常 | PRZ bit 語意與 config 相反（0 = 已鏡射，1 = 未鏡射）；與 Ymirror 極性不對稱，屬已知韌體慣例（見 Section 2） |
| `display_mirror_y` | SLAPrinterSettingsDialog / Mirror Y | `Ymirror` | `uint8 1B` | direct（`false→0, true→1`） | ✅ 正常 | PRZ bit 語意與 config 相同（0 = 未鏡射，1 = 已鏡射）；SLA 上拉式列印物理特性導致 X/Y 軸使用不同慣例，見 Section 2 |

### 1-B：Layer & Exposure 分頁（SLA 材料設定）

| UI Config Key | UI 控件位置 | PRZ Header 欄位名稱 | PRZ 資料型別 | 映射方式 | 健康狀態 | 備注 |
|---|---|---|---|---|---|---|
| `layer_height` | Layer & Exposure / 層厚 | `LayerThickness` | `float BE 4B` | direct | ✅ 正常 | |
| `exposure_time` | Layer & Exposure / 曝光時間 | `ExposureTime` | `float BE 4B` | direct | ✅ 正常 | 同時寫入 per-layer `LayerExposureTime`（含過渡層線性內插） |
| `bottom_exposure_time` | Layer & Exposure / 底層曝光時間 | `BottomExposureTime` | `float BE 4B` | direct | ✅ 正常 | |
| `bottom_layer_count` | Layer & Exposure / 底層數 | `BottomLayers` | `int32 BE 4B` | direct | ✅ 正常 | |

### 1-C：Transition 分頁

| UI Config Key | UI 控件位置 | PRZ Header 欄位名稱 | PRZ 資料型別 | 映射方式 | 健康狀態 | 備注 |
|---|---|---|---|---|---|---|
| `transition_layer_count` | Transition / 過渡層數 | `TransitionLayers` | `int16 BE 2B` | direct | ✅ 正常 | 同時作為 per-layer 曝光時間內插的層數基準 |

### 1-D：Wait & Rest 分頁

| UI Config Key | UI 控件位置 | PRZ Header 欄位名稱 | PRZ 資料型別 | 映射方式 | 健康狀態 | 備注 |
|---|---|---|---|---|---|---|
| `light_off_day` | Wait & Rest / 關燈等待時間 | `TurnOffTime`（header）+ `LayerOffTime`（per-layer） | `float BE 4B` | direct | ✅ 正常 | 同一 key 寫入 header 及每層 layer content |
| `rest_time_before_lift` | Wait & Rest / 上抬前靜止時間 | `Bottom_Before_lift_static_time` + `Before_lift_static_time` | `float BE 4B` | direct | ✅ 正常 | 同一 key 寫入 header 兩個欄位；底層與一般層共用相同值 |
| `rest_time_after_lift` | Wait & Rest / 上抬後靜止時間 | `Bottom_After_lift_static_time` + `After_lift_static_time` | `float BE 4B` | direct | ✅ 正常 | 同上 |
| `rest_time_after_retract` | Wait & Rest / 回縮後靜止時間 | `Bottom_After_retract_static_time` + `After_retract_static_time`（header × 2）+ per-layer | `float BE 4B` | direct | ✅ 正常 | 同一 key 寫入 header 兩欄及每層 layer content |

### 1-E：Motion 分頁（Lift / Retract）

| UI Config Key | UI 控件位置 | PRZ Header 欄位名稱 | PRZ 資料型別 | 映射方式 | 健康狀態 | 備注 |
|---|---|---|---|---|---|---|
| `bottom_lift_distance` | Motion / Bottom Lift Distance（第一段） | `BottomLiftDist` | `float BE 4B` | direct | ✅ 正常 | 同時用於 `BottomRetractDist` derived 計算 |
| `bottom_lift_second_distance` | Motion / Bottom Lift Distance（第二段） | `BottomLift_second_Dist` | `float BE 4B` | direct | ✅ 正常 | 同時用於 `BottomRetractDist` derived 計算 |
| `lifting_distance` | Motion / Lifting Distance（第一段） | `LiftDist` | `float BE 4B` | direct | ✅ 正常 | 同時用於 `RetractDist` derived 計算 |
| `lift_second_distance` | Motion / Lifting Distance（第二段） | `Lift_second_Dist` | `float BE 4B` | direct | ✅ 正常 | 同時用於 `RetractDist` derived 計算 |
| `bottom_lift_speed` | Motion / Bottom Lift Speed（第一段） | `BottomLiftSpeed` | `float BE 4B` | direct | ✅ 正常 | |
| `bottom_lift_second_speed` | Motion / Bottom Lift Speed（第二段） | `BottomLift_second_Speed` | `float BE 4B` | direct | ✅ 正常 | |
| `lifting_speed` | Motion / Lifting Speed（第一段） | `LiftSpeed` | `float BE 4B` | direct | ✅ 正常 | |
| `lift_second_speed` | Motion / Lifting Speed（第二段） | `Lift_second_Speed` | `float BE 4B` | direct | ✅ 正常 | |
| `bottom_retract_speed` | Motion / Bottom Retract Speed（第一段） | `BottomRetractSpeed` | `float BE 4B` | direct | ✅ 正常 | |
| `bottom_retract_second_distance` | Motion / Bottom Retract Distance（第二段） | `BottomRetract_second_Dist` | `float BE 4B` | direct | ✅ 正常 | 同時用於 `BottomRetractDist` derived 計算 |
| `bottom_retract_second_speed` | Motion / Bottom Retract Speed（第二段） | `BottomRetract_second_Speed` | `float BE 4B` | direct | ✅ 正常 | |
| `retract_speed` | Motion / Retract Speed（第一段） | `RetractSpeed` | `float BE 4B` | direct | ✅ 正常 | |
| `retract_second_distance` | Motion / Retract Distance（第二段） | `Retract_second_Dist` | `float BE 4B` | direct | ✅ 正常 | 同時用於 `RetractDist` derived 計算 |
| `retract_second_speed` | Motion / Retract Speed（第二段） | `Retract_second_Speed` | `float BE 4B` | direct | ✅ 正常 | |
| `bottom_lift_distance + bottom_lift_second_distance − bottom_retract_second_distance` | Motion / Bottom Retract Distance（UI 顯示衍生值） | `BottomRetractDist` | `float BE 4B` | derived | ✅ 正常 | 若計算結果 ≤ 0 則 fallback 至 `lh + lh2`；UI 的 `bottom_retract_distance` 欄位顯示此衍生值，與 PRZ 寫法一致 |
| `lifting_distance + lift_second_distance − retract_second_distance` | Motion / Retract Distance（UI 顯示衍生值） | `RetractDist` | `float BE 4B` | derived | ✅ 正常 | 同上 fallback 邏輯 |

### 1-F：Advanced 分頁

| UI Config Key | UI 控件位置 | PRZ Header 欄位名稱 | PRZ 資料型別 | 映射方式 | 健康狀態 | 備注 |
|---|---|---|---|---|---|---|
| `anti_aliasing_level` | Advanced / Anti-aliasing Level | `AntiAliasLevel` | `int16 BE 2B` | direct | ✅ 正常 | 僅當 `anti_aliasing = spAntiAliasingLevel` 時 UI 顯示；PRZ 無論模式均寫入此欄（見 Section 2 風險說明） |
| `gray_scale_level[0]` | Advanced / Grey Scale Level | `GreyLevel` | `int16 BE 2B` | scaled `[0,255]→[0,8]` | ✅ 正常 | 僅當 `anti_aliasing = spGrayScaleLevel` 時 UI 顯示；PRZ 均寫入（含縮放） |
| `image_blur_pixel` | Advanced / Image Blur Pixel | `BlurLevel` | `int16 BE 2B` | direct（enum → index） | ✅ 正常 | 僅當 `anti_aliasing = spGrayScaleLevel` 且 `image_blur_enable = true` 時 UI 顯示 |
| `bottom_light_pwm` | Advanced / Bottom Light PWM | `BottomLightPwm` | `int16 BE 2B` | direct | ✅ 正常 | |
| `light_pwm` | Advanced / Light PWM | `LightPwm` | `int16 BE 2B` | direct | ✅ 正常 | 同時寫入 per-layer `LightPwm`（底層使用 `bottom_light_pwm`） |

---

## Section 2 — 風險 / 斷層欄位

| UI Config Key | UI 控件位置 | PRZ 欄位名稱 | PRZ 資料型別 | 映射方式 | 狀態 | 風險說明 |
|---|---|---|---|---|---|---|
| `display_width` / `display_height` | SLAPrinterSettingsDialog / Size X, Y | `PlatformYLength` / `PlatformXLength` | `float BE 4B` | portrait-swap | ✅ 已修復 | **修復前斷層**：`sync_local_to_tab()` 只更新 `printable_area`，未寫回 `display_width` / `display_height`，導致 PRZ 匯出平台尺寸永遠等於 JSON profile 初始值（如 Mighty Revo 16K 永遠輸出 211×118 mm，無視使用者修改）。已於 `fix-prz-platform-xy-mapping` 補上兩行 `set_key_value` 修復，現已升入 Section 1。 |
| `display_mirror_x` vs `display_mirror_y` | SLAPrinterSettingsDialog / Mirror X、Mirror Y | `Xmirror` vs `Ymirror` | `uint8 1B each` | asymmetric polarity | ⚠️ 已知設計 | **Mirror 極性不對稱**：`Xmirror = !mirror_x`（反轉慣例），`Ymirror = mirror_y`（直接慣例）。兩軸的 bit 語意相反，但此為**刻意設計**：SLA 上拉式列印中 X 軸預設需鏡射（`display_mirror_x` default = `true` → Xmirror = 0），Y 軸預設不鏡射（`display_mirror_y` default = `false` → Ymirror = 0），兩者均對應韌體「不鏡射 = 0」的期望值。未來修改鏡射設定時應注意此極性差異，避免邏輯錯誤。 |
| `anti_aliasing_level` / `gray_scale_level` / `image_blur_pixel` | Advanced 分頁（條件顯示） | `AntiAliasLevel` / `GreyLevel` / `BlurLevel` | `int16 BE 2B` | conditional UI, always written | ⚠️ 輕微 | **AA 子選項無條件寫入**：PRZ 無論 `anti_aliasing` 選擇何種模式，三個子欄位均寫入 header。GrayScale 模式下 `anti_aliasing_level` 仍被寫入（但韌體應忽略）；AntiAliasing 模式下 `gray_scale_level` / `image_blur_pixel` 同樣被寫入。目前韌體依 PRZ 欄位自行解讀，風險低，但需確認韌體對未啟用模式欄位的容錯行為。 |
| `rest_time_before_lift` / `rest_time_after_lift` | Wait & Rest 分頁 | `Bottom_Before/After_lift_static_time` + `Before/After_lift_static_time` | `float BE 4B` | same key → two header slots | ⚠️ 輕微 | **底層 / 一般層共用 Rest Time**：PRZ header 有獨立的 bottom 與 normal 靜止時間欄位，但 PhrozenOrca 使用同一組 config key 填入兩者，無法個別設定底層與一般層的靜止時間差異。若未來韌體利用此差異，需新增對應 config key。 |

---

## Section 3 — PRZ 支援但 UI 未開放的欄位

> **盤點說明**：PhrozenOrca UI 已相當完整地覆蓋 PRZ 格式中所有使用者可配置的列印參數（機台尺寸、解析度、鏡射、曝光、層數、運動、PWM、灰階、模糊等）。本 Section 列出 PRZ binary 中有定義但無對應 config key 亦無 UI 入口的欄位，主要為硬寫常數或執行期衍生值。

| PRZ Header 欄位名稱 | PRZ 資料型別 | 語意說明 | 建議 UI Config Key | 擴充優先級 | 備注 |
|---|---|---|---|---|---|
| `Exposure_delay_mode` | `uint8 1B` | 曝光延遲模式旗標；`0x01` = 靜態等待時間模式（使用 rest_time_* 控制）。目前 PRZ 硬寫 `0x01`，無 config key | `exposure_delay_mode` (coEnum) | 低 | 韌體未來可能新增動態模式（VSYNC 同步等）；當前全機型均為靜態模式，擴充前需確認韌體支援 |
| `Advance_Mode` | `uint8 1B` | 列印進階模式旗標；`0x00` = 一般模式。目前 PRZ 硬寫 `0x00`，無 config key | `advance_print_mode` (coEnum) | 低 | 韌體保留欄位，目前無已知進階模式定義；擴充前需韌體規格確認 |
| `Grayscale_level` | `uint8 1B` | 圖層灰階輸出位元深度；`0x01` = 8-bit 灰階。目前 PRZ 硬寫 `0x01`，無 config key | `grayscale_bit_depth` (coInt) | 低 | 與 `gray_scale_level`（AA 強度設定）為不同欄位；此為 PRZ format 的輸出格式宣告，目前全機型固定 8-bit，無需 UI 選項 |
| `PrintTimes` | `int32 BE 4B` | 預估列印時間（秒），由 `adjusted_prz_print_time_seconds()` 依層數計算，寫入 PRZ header 供韌體顯示 | — | 低 | 為計算輸出值（非使用者輸入），無需 UI config key；計算邏輯封裝於 `prz_header()` 中，可在切片預覽中顯示但不需設定介面 |
| `TotalVolume / TotalWeight / TotalPrice` | `float BE 4B × 3` | 材料使用量（mm³）、重量（g）、費用（貨幣）；來自 `SLAPrintStatistics`，寫入 PRZ header 供韌體顯示 | — | 低 | 計算輸出值；費用與重量計算需材料密度與單價；PRZ 中僅供韌體顯示，不影響列印行為，無需 UI config key |
| `PriceUnit` | `8B（null bytes）` | 費用貨幣單位字串；目前全部為 null bytes，韌體未使用 | `price_unit` (coString) | 低 | 目前留空；若韌體未來解讀此欄位可在材料設定中新增貨幣單位選項，但優先級極低 |

---

## Section 4 — UI 開放但 PRZ 無對應的欄位

| UI Config Key | UI 控件位置 | 說明 | 現狀 |
|---|---|---|---|
| `printable_area` | SLAPrinterSettingsDialog / Size X, Y（內部多邊形） | 機台可列印範圍的四角頂點多邊形（`ConfigOptionPoints`）；PRZ 格式使用純量 `display_width` / `display_height` 表示平台尺寸，無 polygon 欄位 | `printable_area` 於 `fix-prz-platform-xy-mapping` 修復後與 `display_width` / `display_height` 保持同步；PRZ 讀取 display_width/height，polygon 為 UI slicing 內部座標使用，不需對應 PRZ 欄位 |
| `bottom_light_off_day` | Wait & Rest / 底層關燈等待時間 | 底層專用的關燈等待時間；PRZ header 與 per-layer 均只使用 `light_off_day`（一般層值），無獨立底層欄位 | PRZ 底層使用與一般層相同的 `TurnOffTime` / `LayerOffTime` 值；若需底層差異化需同步擴充韌體與 PRZ format 規格 |
| `anti_aliasing` | Advanced / Anti-aliasing 模式枚舉 | 父模式選擇器（`spGrayScaleLevel` / `spAntiAliasingLevel` 等），控制哪組 AA 子選項出現在 UI | PRZ 直接寫入子選項數值（`AntiAliasLevel` / `GreyLevel` / `BlurLevel`），不記錄父模式枚舉；韌體依子欄位值自行判斷模式，無需父枚舉 |
