# Spec: PRZ 輸出與 Phrozen SLA Profile 對齊 Chitubox

## Purpose

規範 PhrozenOrca 產出 `.prz` 之 header 數值欄位單位（volume = mm³、weight = grams、speed = mm/min）、weight 計算策略（密度公式、樹脂瓶資訊缺失時退化 1.0），以及 Phrozen SLA Resin profile JSON 預設值與 Chitubox 對齊的範圍與項目。本 capability 同時規範 UI 顯示與 PRZ 輸出之「天然解耦」原則 — PRZ writer 自行做單位轉換，不影響任何 UI 顯示路徑或 `SLAPrintStatistics` 結構體本身。

## Requirements

### Requirement: PRZ Header TotalVolume 欄位 SHALL 以 mm³ 量級寫入

PhrozenOrca 產出 `.prz` 檔案時，header 區段中的 `TotalVolume` 欄位 SHALL 以立方公釐（mm³）為單位寫入；數值來源為 `SLAPrintStatistics::objects_used_material + SLAPrintStatistics::support_used_material`（含支撐之總樹脂體積），不再除以 1000。寫入型別 SHALL 維持既有的 4-byte big-endian float（PRZ binary format 規範不可變動）。

#### Scenario: 標準模型切片後 TotalVolume 為整數量級 mm³

- **WHEN** 將任一 STL（例如 10mm 校正立方體）以 PhrozenOrca 切片並匯出 `.prz`，再以 ReadPrz 工具 dump header
- **THEN** `TotalVolume` 欄位數值之量級為整數百至千級（如 `1148.0` 或 `1003.8`），且該數值 ≈ `objects_used_material + support_used_material`（mm³）

#### Scenario: TotalVolume 含支撐體積

- **WHEN** 同一 STL 分別在「無支撐」與「含支撐」兩種設定下匯出 .prz
- **THEN** 含支撐版本之 `TotalVolume` > 無支撐版本之 `TotalVolume`，且差值 ≈ `support_used_material`（mm³）

#### Scenario: TotalVolume 量級對齊 Chitubox

- **WHEN** 將同一 STL 分別以 PhrozenOrca 與 Chitubox 切片，並比對兩個 .prz 之 `TotalVolume` 欄位
- **THEN** 兩者數值之量級一致（皆為整數量級 mm³），不因「ml vs mm³」差距產生 1000 倍量級落差；具體數值之微小差距（因切片演算法不同）視為可接受，不在驗證範圍

### Requirement: PRZ Header TotalWeight 欄位 SHALL 以克（grams）寫入，數值不隨 Volume 量級放大

PRZ header 中的 `TotalWeight` 欄位 SHALL 維持以克（grams）為單位寫入，採密度公式 `weight = volume_ml × density`，其中 `density = (bottle_weight × 1000) / bottle_volume`（kg → g 換算後除以瓶裝容量 ml）。Weight 計算中介所使用的 `volume_ml` SHALL 由 `(objects_used_material + support_used_material) / 1000.0` 推導，與 PRZ `TotalVolume` 欄位之 mm³ 寫入值在程式碼中為**獨立 local 變數**，互不耦合。

#### Scenario: Weight 數值與 UI 顯示量級一致

- **WHEN** 切片後 UI 顯示「Total resin: X.XXX ml」，並匯出對應 .prz
- **THEN** .prz `TotalWeight` 欄位之數值量級為「克」（小數點前 0~4 位數，例如 `1.10418`、`1.148`），不會出現「千」或「萬」量級之數值

#### Scenario: Weight 不隨 Volume 單位變動而放大 1000 倍

- **WHEN** Volume 寫入單位由 ml 改為 mm³（即 `TotalVolume` 數值放大 1000 倍）
- **THEN** `TotalWeight` 欄位之數值 SHALL NOT 同步放大；其量級維持為克，與 Volume 單位變動之前一致

#### Scenario: Weight 量級對齊 Chitubox

- **WHEN** 將同一 STL 分別以 PhrozenOrca 與 Chitubox 切片，並比對兩個 .prz 之 `TotalWeight`
- **THEN** 兩者數值之量級一致（皆為克的小數量級，如 `1.10418` vs `1.148`）；具體數值差距由密度設定與切片差異決定，不在驗證範圍

#### Scenario: 樹脂瓶資訊缺失時 weight 退化為 volume_ml

- **WHEN** active filament profile 之 `bottle_weight` 或 `bottle_volume` 為 0（或未填寫）
- **THEN** 計算公式中 `density` SHALL 退化為 `1.0` g/ml，使得 `TotalWeight ≈ volume_ml × 1.0`；既有預設行為保留

### Requirement: PRZ Header 所有 Speed 系列欄位 SHALL 以 mm/min 寫入

PRZ header 與 per-layer content 中所有與「速度」相關的欄位 SHALL 以毫米/分鐘（mm/min）為單位寫入，數值直接來自對應 config key 經 `cfg_f()` 讀取所得；writer 端 SHALL NOT 對 speed 數值做任何 `* 60` 或 `/ 60` 等單位轉換。涵蓋之 config key 清單 SHALL 至少包含以下 8 個：

- `bottom_lift_speed`
- `bottom_lift_second_speed`
- `lifting_speed`
- `lift_second_speed`
- `bottom_retract_speed`
- `bottom_retract_second_speed`
- `retract_speed`
- `retract_second_speed`

#### Scenario: PRZ Header 速度欄位數值等同 active config 值

- **WHEN** 將任一 STL 以 PhrozenOrca 切片並匯出 .prz，且 active process profile 之 `lifting_speed = 130`、`bottom_lift_speed = 80`
- **THEN** .prz header `LiftSpeed` 欄位讀回為 `130.0`、`BottomLiftSpeed` 欄位讀回為 `80.0`（單位 mm/min，無單位換算）

#### Scenario: PRZ Per-layer 速度欄位數值同步

- **WHEN** 任意 per-layer content 寫入完成
- **THEN** 該 layer 之 `LiftSpeed` / `Retract_Speed` 等欄位數值與對應 config（依 `is_bottom` 條件選擇 bottom 或 normal 變體）一致，且單位皆為 mm/min

#### Scenario: PrintConfig 與 PRZ writer 單位一致性

- **WHEN** 檢視 PrintConfig.cpp 內 8 個 speed 系列 config key 之 `def->sidetext`
- **THEN** 所有 8 個 config key 之 sidetext SHALL 標記為 `"mm/min"`；PhrozenPRZ.cpp 對應寫入點 SHALL NOT 含 `* 60` 或 `/ 60` 之單位換算（`motion_s` lambda 內部之 `* 60.f` 為「分鐘 → 秒」之時間單位轉換，不屬於本要求所禁止之 speed 單位轉換）

#### Scenario: Speed 為 0 時 distance 為 0 之 guard 維持

- **WHEN** 任一 `*_second_distance` 為 0
- **THEN** 對應之 `*_second_speed` 欄位寫入值 SHALL 為 0（既有 guard 邏輯保留，不因本變更而失效）

### Requirement: Phrozen SLA Resin Profile JSON 預設值 SHALL 對齊 Chitubox 並反映於新建 Profile UI 與 PRZ 輸出

[resources/profiles/PhrozenSLA/](resources/profiles/PhrozenSLA/) 目錄下之 process / filament / machine JSON profile，其相關曝光、升降距離、升降速度、回拉距離、回拉速度、第二段升降/回拉、抗鋸齒（aa_level）、模糊（blur_level）等預設欄位 SHALL 對齊 Chitubox 同機型輸出之數值範例。修改 SHALL 限定於 PhrozenSLA profile JSON；不修改 [PrintConfig.cpp](src/libslic3r/PrintConfig.cpp) 之 hardcoded global default。

#### Scenario: 新建 Phrozen Profile 之 UI 預設值對齊 Chitubox

- **WHEN** 使用者於 PhrozenOrca 新建 process profile 並選用 Phrozen 系列機型（如 `Phrozen Sonic Mighty Revo 16K`），不做任何客製化
- **THEN** UI 中曝光、升降、回拉系列欄位之預設顯示值 SHALL 與本變更於 [resources/profiles/PhrozenSLA/process/](resources/profiles/PhrozenSLA/process/) 修改後之預設值一致；該預設值來源於對齊 Chitubox 範例之 diff 表

#### Scenario: 預設值變更同步反映於 PRZ 輸出

- **WHEN** 使用者以未客製化之 Phrozen 預設 profile 切片任一 STL 並匯出 .prz
- **THEN** .prz header 內對應之 `BottomExposureTime`、`BottomLayerCount`、`LiftDist`、`LiftSpeed`、`BottomLiftSpeed`、`RetractDist`、`RetractSpeed`、`BlurLevel`、`AntiAliasing` 等欄位數值 SHALL 等於 JSON 預設值（透過 `cfg_i()` / `cfg_f()` 動態讀取，無需修改 writer 讀取邏輯）

#### Scenario: 既有 user preset 不受預設值變動影響

- **WHEN** 使用者於本變更套用前已建立 user preset（帶 `inherits: "<system preset>"` 鏈結，並覆寫過如 `bottom_exposure_time = 20`）；本變更套用後 system preset 預設值變更為 `bottom_exposure_time = 35`
- **THEN** 該 user preset 仍維持 `bottom_exposure_time = 20`，不被「無聲 migration」覆蓋；使用者主動切換至 system preset 時才會看到新預設值

#### Scenario: 機型差異化預設值正確套用

- **WHEN** 切換 process profile 至不同 Phrozen 機型（例如由 `Speed Plus - Black@Phrozen Sonic Mega 8K S` 切至 `Speed Plus - Black@Phrozen Sonic Mighty Revo 16K`）
- **THEN** UI 顯示之 `lifting_speed`、`bottom_lift_speed` 等機型相依欄位 SHALL 變更為新機型對應之 Chitubox 對齊值（如 Mega 8K S = 45、Revo 16K = 130/80）

#### Scenario: 修改範圍限定於 PhrozenSLA

- **WHEN** 檢視 [src/libslic3r/PrintConfig.cpp](src/libslic3r/PrintConfig.cpp) 與其他廠牌之 profile JSON（如 `resources/profiles/Anycubic*`、`resources/profiles/Elegoo*` 若存在）
- **THEN** 該等檔案之 default_value SHALL NOT 因本變更而修改；本變更影響面限定於 `resources/profiles/PhrozenSLA/`

### Requirement: UI 顯示路徑 SHALL 不受 PRZ Volume 單位變動影響

切片完成後 SLA 預覽面板（[SLASlice2DCanvas](src/slic3r/GUI/SLASlice2DCanvas.cpp)）之「Total resin」顯示行為 SHALL 與本變更前完全一致；該路徑自行從 `SLAPrintStatistics` 讀取 mm³ 並做 `/ 1000.0` 換算為 ml 顯示，與 PRZ writer 互不耦合。`SLAPrintStatistics` 結構體本身 SHALL NOT 被修改。

#### Scenario: 切片後 UI 顯示維持 ml 單位與精度

- **WHEN** 切片完成且 `slapsMergeSlicesAndEval` step 為 done
- **THEN** UI 顯示為「Total resin: X.XXX ml」格式（小數點 3 位、單位後綴 `" ml"`），與本變更前一致；數值為 `(objects_used_material + support_used_material) / 1000.0`

#### Scenario: SLAPrintStatistics 結構體未被本變更修改

- **WHEN** 檢視 [src/libslic3r/SLAPrintStatistics](src/libslic3r/) 結構體定義與 [src/libslic3r/SLAPrintSteps.cpp](src/libslic3r/SLAPrintSteps.cpp) 中 `merge_slices_and_eval_stats()` 之計算邏輯
- **THEN** 該結構體之欄位、單位、計算邏輯 SHALL NOT 因本變更而改動；單位維持 mm³（scaled coordinates × SCALING2 還原）

#### Scenario: UI 路徑與 PRZ writer 之單位轉換各自獨立

- **WHEN** 後續若有任一方需修改單位（例如 UI 改顯示 cm³）
- **THEN** 該修改 SHALL 僅影響其自身路徑，不會傳染至另一方；本變更建立並維持此解耦原則