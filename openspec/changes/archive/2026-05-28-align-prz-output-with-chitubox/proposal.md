## Why

QA 主動對齊：目前 PhrozenOrca 輸出的 `.prz` 與 Chitubox 輸出的 `.prz` 在 header 數值欄位的 **單位** 與 **預設 profile 參數** 上存在差異（見附檔三組 diff：Mega 8K S / Mega 8K V2 / Mighty Revo 16K）。雖然檔案皆可成功列印，但對外比對 Chitubox 為事實標竿時，數值單位與量級不一致會造成 QA 與後續驗證困難。本次變更聚焦於「**輸出對齊**」與「**Phrozen SLA Resin profile 預設值對齊**」，使同一 STL 在 PhrozenOrca 切片後產生的 .prz header 在單位與預設值上與 Chitubox 一致；體積數值上的微小差距（因模型本體切片差異所致）不在本次處理範圍。

## What Changes

本次變更包含四項明確技術承諾：

1. **Volume 單位由 ml 改為 mm³（PRZ Header 寫入端）**
   - 僅修改 [src/libslic3r/Format/PhrozenPRZ.cpp](src/libslic3r/Format/PhrozenPRZ.cpp) 內 `prz_header()` 對 `objects_used_material + support_used_material` 的單位轉換邏輯，移除既有的 `/ 1000.0`，直接以 mm³ 寫入 PRZ `TotalVolume` 欄位。
   - **支撐 (`support_used_material`) 仍然納入** volume 計算（使用者場景：樹脂用量預估）。
   - **不更動全域 `SLAPrintStatistics` 變數**：該結構體本身儲存的就是 mm³，UI 顯示路徑 [src/slic3r/GUI/SLASlice2DCanvas.cpp:317](src/slic3r/GUI/SLASlice2DCanvas.cpp#L317) 自行 `/ 1000.0` 換算成 ml 顯示，與 PRZ writer 天然解耦，無須任何分流邏輯或 override 設計。

2. **速度（Speed）欄位單位 Audit — 確保 PRZ 寫入皆為 mm/min**
   - 對 [PhrozenPRZ.cpp](src/libslic3r/Format/PhrozenPRZ.cpp) 內所有與「速度」相關的寫入欄位（如 `liftSpeed`、`bottomLiftSpeed`、`retractSpeed`、`bottomRetractSpeed`、`lift_second_Speed`、`retract_second_Speed`、`bottomRetract_second_Speed` 等）進行單位 audit。
   - 確認 OrcaSlicer 內部 config 該欄位是 mm/s 還是 mm/min；UI 顯示已確認為 mm/min，預期 config 內亦為 mm/min 因此 writer 不需 `× 60` 換算。實作時逐欄位核對；若發現任何 mm/s → mm/min 漏轉換的情況，於 PhrozenPRZ.cpp 補上換算。
   - **不修改任何 UI 顯示行為**。

3. **Weight 採用選項 X — 維持克（grams）單位、使用密度公式**
   - 維持 [PhrozenPRZ.cpp:531-534](src/libslic3r/Format/PhrozenPRZ.cpp#L531-L534) 既有的 `weight = volume × density` 公式，其中 density 由樹脂瓶的 `bottle_weight` (kg) ÷ `bottle_volume` (ml) 推算。
   - 輸出單位為克（grams），與 Chitubox 一致（Chitubox 範例：`weight: 1.10418` 為克）。
   - **注意**：Volume 寫入 PRZ 雖改為 mm³，但 weight 公式內部仍需以 ml 為基準參與 density 乘法（避免破壞既有正確邏輯）。實作上將「PRZ 輸出的 volume 值」與「weight 計算的中介 volume 值」做變數命名與作用域分離。
   - 若樹脂 profile 之 `bottle_weight` / `bottle_volume` 已填寫具體值，weight 自動反映該樹脂密度；若無，預設密度退化為 1.0 g/ml（既有行為）。

4. **resources/profiles 內 Phrozen SLA Resin Profile 預設值對齊 Chitubox**
   - 修改 [resources/profiles/PhrozenSLA/](resources/profiles/PhrozenSLA/) 下 process / filament / machine JSON 之預設值，以對應 diff 檔內 Chitubox 範例所示的數值（如 `bottomExposureTime`、`bottomLayers`、`liftDist`、`liftSpeed`、`bottomLiftSpeed`、`retractDist`、`retractSpeed`、`blurLevel`、`aaLevel` 等可直接對應的欄位）。
   - **同時影響 UI 預設值與 PRZ 輸出值**：因為 PRZ writer 全部欄位皆透過 `cfg_i(cfg, "key")` / `cfg_f(cfg, "key")` 動態讀取 active config，修改 JSON 預設後 UI 與 PRZ 輸出會同步反映新預設值，**不需要修改 PRZ writer 的讀取邏輯**。
   - **不修改 PrintConfig.cpp 的 hardcoded global default**（避免影響非 Phrozen 機型的 SLA 使用者）。
   - 已客製化（建立過 user preset、`inherits` 鏈結到 system preset）的使用者保留其覆寫值，行為符合既有 Preset 系統規範（無強制 migration）。

## Capabilities

### New Capabilities

- `prz-chitubox-output-alignment`: 規範 PhrozenOrca 產出 `.prz` 之 header 數值欄位單位（volume = mm³、weight = grams、speed = mm/min、time = seconds）、weight 計算策略（密度公式、選項 X），以及 Phrozen SLA Resin profile JSON 預設值與 Chitubox 對齊的範圍與項目。本 capability 同時規範 UI 顯示與 PRZ 輸出之「天然解耦」原則 — PRZ writer 自行做單位轉換，不影響任何 UI 顯示路徑或 `SLAPrintStatistics` 結構體本身。

### Modified Capabilities

（無）本次變更不修改既有 spec 的需求行為。`prz-ui-parameter-mapping` 與 `sla-on-demand-prz-export` 等既有 spec 規範的對應為映射與管線架構，與本次「輸出單位與預設值對齊」屬正交議題。新 capability 完成後，將由 `prz-ui-parameter-mapping` 在下次更新時自然納入記載（單位欄位新增說明），但**該動作不在本變更範圍內**。

## Impact

**程式碼**
- [src/libslic3r/Format/PhrozenPRZ.cpp](src/libslic3r/Format/PhrozenPRZ.cpp) — `prz_header()` 內 volume 寫入單位、speed 欄位單位 audit、weight 公式變數作用域整理。
- 不修改 [src/slic3r/GUI/SLASlice2DCanvas.cpp](src/slic3r/GUI/SLASlice2DCanvas.cpp) 與任何其他 UI 程式碼。
- 不修改 [src/libslic3r/PrintConfig.cpp](src/libslic3r/PrintConfig.cpp) 的 hardcoded default。
- 不修改 `SLAPrintStatistics` 結構體與 [src/libslic3r/SLAPrintSteps.cpp](src/libslic3r/SLAPrintSteps.cpp)。

**Resource / 設定檔**
- [resources/profiles/PhrozenSLA/process/](resources/profiles/PhrozenSLA/process/) — 列印程序 profile 預設值（如 `bottom_exposure_time`、`bottom_lift_distance`、`lift_speed` 等）。
- [resources/profiles/PhrozenSLA/filament/](resources/profiles/PhrozenSLA/filament/) — 樹脂 profile 預設值。
- [resources/profiles/PhrozenSLA/machine/](resources/profiles/PhrozenSLA/machine/) — 機型相關預設值（如 `blur_level`、`aa_level` 等若以機型為單位）。

**使用者影響**
- 既有客製化 profile（user preset）保留其數值，不受影響。
- 全新建立或尚未客製化的 Phrozen SLA profile 之預設值將更新為對齊 Chitubox 之值。
- UI 顯示（如「Total resin: 1.148 ml」）的單位與行為**不變**。
- .prz 檔案內 `volume` 數值量級由小數（ml）改為整數量級（mm³），下游讀取 .prz 的工具若硬編碼依賴 ml 量級需注意 — 但根據既有討論，本次不考量 Phrozen 印表機韌體顯示行為，視為使用者可接受的對齊代價。

**驗收方式**
- 同一 STL 透過 PhrozenOrca 切片產出 .prz，並以 ReadPrz 工具 dump header，volume / weight / speed 欄位之 **單位** 與 **量級** 與 Chitubox 同模型輸出一致（數值上的微小差距因切片差異所致，不在驗收範圍）。
- Phrozen SLA profile 預設值（diff 檔列出之可對應欄位）數值與 Chitubox 範例對齊。
- UI 顯示路徑回歸測試：「Total resin」仍顯示 ml 單位且數值正確。
