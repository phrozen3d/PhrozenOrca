# CHANGELOG — align-prz-output-with-chitubox

## Phrozen SLA：PRZ 輸出與 Resin Profile 預設值對齊 Chitubox

本次變更使 PhrozenOrca 切片產出的 `.prz` header 單位與 Phrozen SLA profile 預設值對齊 Chitubox 事實標竿。

### 變更摘要

- **Volume 單位改為 mm³**：PRZ header `TotalVolume` 欄位改以立方公釐（mm³）寫入（移除既有 `/ 1000.0`），與 Chitubox 量級一致；寫入型別維持 4-byte big-endian float。來源 `objects_used_material + support_used_material`（含支撐）。UI「Total resin: X.XXX ml」顯示路徑與 `SLAPrintStatistics` 結構體不受影響（天然解耦）。

- **Weight 維持克單位與密度公式**：`TotalWeight` 維持以克（grams）寫入，採 `weight = volume_ml × density`，其中 `density = (bottle_weight × 1000) / bottle_volume`。PRZ 寫入的 mm³ 體積與 weight 計算的 ml 中介值在程式碼中為獨立 local 變數（`volume_mm3` / `volume_ml`），互不耦合。樹脂瓶資訊缺失時 density 退化為 1.0 g/ml（本次 filament profile 維持缺省，weight ≈ volume_ml）。

- **Speed 單位 Audit（mm/min）**：確認 8 個 speed 系列 config key 之 `sidetext` 皆為 `"mm/min"`，PRZ writer 對 speed 數值無 `* 60` / `/ 60` 換算（`motion_s` lambda 內 `* 60` 僅為分→秒之時間換算）。無程式碼補丁需求。

- **Revo 16K 二階速度對齊為 0 之防禦處理**：Mighty Revo 16K 採 Chitubox 範例「二階距離非零、二階速度為 0」設定（`bottom_lift_second_speed` / `lift_second_speed` / `bottom_retract_second_speed` / `retract_second_speed` = 0）。`calculate_prz_print_time()` 的 `motion_s` lambda 既有 guard `if (speed_mm_min <= 0.f) return 0.f;` 已防止除以 0 / inf，無需新增代碼。

- **6 個 PhrozenSLA Process Profile 預設值更新**：對齊 Chitubox diff 之曝光、層數、升降/回拉距離與速度、二階距離/速度、aa_level、blur_level。
  - Speed Plus - Black @ {Mega 8K S, Mega 8K V2, Mighty Revo 16K}
  - Tough ABS-like+ @ {Mega 8K S, Mega 8K V2, Mighty Revo 16K}（僅對齊運動學 + aa + blur，保留樹脂專屬之曝光時間 / 層數 / rest_time）
  - 主要欄位：Revo `liftSpeed=130`/`bottomLiftSpeed=80`/`retractSpeed=160`/`aaLevel=8`/`layerThickness=0.05`；Mega 8K S `bottomExposureTime=35`/`liftSpeed=45`；blurLevel=2（全機型）。

### 範圍與相容性

- 僅修改 `resources/profiles/PhrozenSLA/` 下 6 個 process JSON 與 `src/libslic3r/Format/PhrozenPRZ.cpp`（volume 單位）。
- **未**修改 `PrintConfig.cpp` global default、machine / filament JSON、UI 程式碼、`SLAPrintStatistics` / `SLAPrintSteps.cpp`。
- 既有客製化 user preset 透過 `inherits` 鏈保留覆寫值，無強制 migration。
- 下游讀取 .prz 之工具若硬編碼依賴舊 ml 量級之 volume 需注意（本次不考量印表機韌體顯示行為）。