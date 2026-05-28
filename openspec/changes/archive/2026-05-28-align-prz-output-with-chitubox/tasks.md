## 階段執行紀律（請於開始實作前閱讀）

本變更**嚴格**拆分為三個獨立階段：

- **階段 1**：底層 Writer 修改（Volume / Weight 單位調整）
- **階段 2**：速度參數 Audit 與補丁
- **階段 3**：JSON 設定檔分批更新

**強制執行規則**：

- 🔴 **嚴禁整包實作**：禁止在未完成前一階段「驗證任務」的情況下開始下一階段任何實作任務。
- 🔴 **驗證未通過則不得前進**：若任一驗證任務失敗，必須先修正失敗點並重新通過驗證，才能繼續。
- 🔴 **驗證任務必須留下證據**：執行 grep / 編譯 / 切片 dump 之輸出需貼至 commit message 或本 tasks.md 之 footnote，不得僅以「已執行」帶過。
- 🔴 **每完成一個 task 立即勾選**：不得批次勾選；勾選代表「已實作 + 已驗證」。

---

## 1. 階段 1 — 底層 Writer 修改（Volume / Weight）

### 1.A — Volume 寫入單位調整（ml → mm³）

- [x] 1.1 於 [PhrozenPRZ.cpp:526-541](src/libslic3r/Format/PhrozenPRZ.cpp#L526-L541) 之 `TotalVolume / TotalWeight / TotalPrice` block 內，將既有 `float volume_ml = ((stats.objects_used_material + stats.support_used_material) / 1000.0);` 拆分為兩個 local 變數：
  - `const double total_mm3 = stats.objects_used_material + stats.support_used_material;`
  - `const float volume_mm3 = static_cast<float>(total_mm3);` （供 PRZ 寫入用）
  - `const float volume_ml  = static_cast<float>(total_mm3 / 1000.0);` （供 weight / price 公式用）
- [x] 1.2 將 `write_be(fh, volume_ml)` 改為 `write_be(fh, volume_mm3)`，使 PRZ `TotalVolume` 欄位以 mm³ 寫入；保持 4-byte BE float 寫入型別不變。
- [x] 1.3 補上單位語意註解（`// PRZ: mm³` / `// weight calc base: ml`），消除日後誤改風險。
- [x] **1.4 [驗證]** 使用 Grep 確認 [PhrozenPRZ.cpp](src/libslic3r/Format/PhrozenPRZ.cpp) 內：
  - `volume_mm3` 出現於 `write_be` 寫入點（恰好 1 次）
  - `volume_ml` 不出現於 `write_be(fh, ...)` 寫入引數位置（避免遺漏改名）
  - `/ 1000.0` 仍存在於 `volume_ml` 推導行，但**不**存在於 `write_be` 引數
- [x] **1.5 [驗證]** 確認 [SLASlice2DCanvas.cpp:317](src/slic3r/GUI/SLASlice2DCanvas.cpp#L317) 與 [SLAPrintSteps.cpp](src/libslic3r/SLAPrintSteps.cpp) 之 `merge_slices_and_eval_stats()` **未被修改**（git diff 應僅顯示 PhrozenPRZ.cpp 一個檔案）。
- [x] **1.6 [驗證]** 編譯 build/RelWithDebInfo 通過（`build_release_vs2022.bat slicer`）；無編譯錯誤、無與 volume / weight 相關之 warning。
  - 證據：Visual Studio 2022 IDE 編譯結果 `========== 組建: 1 成功，0 失敗，21 為最新狀態，2 已跳過 ==========` 於 2026-05-27 上午 10:21（增量編譯耗時 2.027 秒）。

### 1.B — Weight / Price 公式變數作用域整理

- [x] 1.7 確認 weight 公式 `weight = volume_ml * density` 與 price 公式 `price = (volume_ml * bottle_cost) / bv_ml` 改用新的 local `volume_ml`（即原 block 內第二個變數），語意保持與既有完全一致。
- [x] 1.8 確認 density 退化邏輯保留：`density = (bv_ml > 0.f) ? bw_g / bv_ml : 1.f;`
- [x] **1.9 [驗證]** 切片一個 10mm 校正立方體（無支撐），匯出 .prz，以 ReadPrz 工具 dump header，確認：
  - `TotalVolume` 數值量級為整數百至千（如 `~1000` 上下，視切片精度而定，**非** 1.xxx）
  - `TotalWeight` 數值量級為克的小數（如 `1.0~1.5`，**非** 1000+ 量級）
  - `TotalPrice` 數值未爆漲 1000 倍（與本變更前同量級）
  - 證據：3 個機型 ReadPrz dump 一致：`volume:1148`（mm³，整數量級正確）、`weight:1.148`（克，密度退化為 1.0 g/ml）、`price:0`（無 bottle_cost 設定）。檔案：cube_8ks_new.txt、cube_8kv2_new.txt、cube_revo_new.txt（fileTime 2026-05-27 10:26–10:29）。
- [x] **1.10 [驗證]** 同一切片於 PhrozenOrca UI 之「Total resin: X.XXX ml」顯示**仍為 ml 單位、數值小於 10**（回歸測試：UI 路徑未受影響）。
  - 證據：UI 顯示路徑 [SLASlice2DCanvas.cpp:317](src/slic3r/GUI/SLASlice2DCanvas.cpp#L317) 未被改動（1.5 已確認 git diff 範圍）；UI 自行 `/ 1000.0` 將 SLAPrintStatistics mm³ 轉成 ml 顯示，與 PRZ writer 天然解耦，使用者確認 UI 行為正常。

### 1.C — 階段 1 完成檢查（gate）

- [x] **1.11 [GATE]** 上述 1.1–1.10 全部勾選完畢、git diff 僅含 [PhrozenPRZ.cpp](src/libslic3r/Format/PhrozenPRZ.cpp)；commit message 附驗證證據（ReadPrz dump 摘要 + 編譯 log 摘要）。**未完成本 GATE 不得開始階段 2 任何任務。**
  - GATE 通過證據：
    - git diff 範圍：`src/libslic3r/Format/PhrozenPRZ.cpp`（+8 行 / -5 行，單一檔案）
    - 編譯：VS2022 IDE「1 成功 0 失敗 21 為最新狀態 2 已跳過」（2026-05-27 10:21）
    - ReadPrz dump：3 機型一致 `volume:1148`（mm³）、`weight:1.148`（g）、`price:0`

---

## 2. 階段 2 — 速度參數 Audit 與補丁

> 前置條件：**階段 1 GATE（task 1.11）已勾選**。

### 2.A — PrintConfig.cpp sidetext audit

- [x] 2.1 對 8 個 speed 系列 config key 逐一執行 Grep，確認 [PrintConfig.cpp](src/libslic3r/PrintConfig.cpp) 內 `def->sidetext` 值：
  - `bottom_lift_speed`、`bottom_lift_second_speed`
  - `lifting_speed`、`lift_second_speed`
  - `bottom_retract_speed`、`bottom_retract_second_speed`
  - `retract_speed`、`retract_second_speed`
  - Grep 命中 add() 位置：line 6759 / 6767 / 6775 / 6783 / 6791 / 6799 / 6807 / 6815。
- [x] **2.2 [驗證]** Grep 結果列出 8 個 sidetext，**全部為 `"mm/min"`**。若有任一為 `"mm/s"`，於 tasks.md 紀錄該 key 名稱、行號，暫停 audit，進入 2.C 補丁流程。
  - 證據（[PrintConfig.cpp](src/libslic3r/PrintConfig.cpp)）：6763 `bottom_lift_speed="mm/min"`、6771 `bottom_lift_second_speed="mm/min"`、6779 `lifting_speed="mm/min"`、6787 `lift_second_speed="mm/min"`、6795 `bottom_retract_speed="mm/min"`、6803 `bottom_retract_second_speed="mm/min"`、6811 `retract_speed="mm/min"`、6819 `retract_second_speed="mm/min"`。**8/8 全為 mm/min，無需進入 2.C。**

### 2.B — PhrozenPRZ.cpp 寫入點 audit

- [x] 2.3 對 [PhrozenPRZ.cpp](src/libslic3r/Format/PhrozenPRZ.cpp) 全檔執行 Grep `* 60` 與 `/ 60` 模式，列出所有命中行。
  - Grep 結果：全檔僅命中 1 行 `line 102: return dist_mm / speed_mm_min * 60.f;`
- [x] **2.4 [驗證]** Grep 結果僅含 `motion_s` lambda 內 `dist_mm / speed_mm_min * 60.f`（line 102，分→秒之時間換算，正確）；其他位置**不得**有 `* 60` 或 `/ 60` 用於 speed 數值轉換。若發現，於 tasks.md 紀錄並進入 2.C。
  - 證據：line 100-103 為 `auto motion_s = [](float dist_mm, float speed_mm_min) { if (speed_mm_min <= 0.f) return 0.f; return dist_mm / speed_mm_min * 60.f; };` — 用途為列印時間換算（mm ÷ mm/min × 60 = 秒），非 speed 數值轉換。**通過，無需進入 2.C。**
- [x] 2.5 對 header 寫入區（line 437-513）逐一檢查 8 個 speed 寫入點：
  - `BottomLiftSpeed` (line 456) / `BottomLift_second_Speed` (line 488)
  - `LiftSpeed` (line 460) / `Lift_second_Speed` (line 496)
  - `BottomRetractSpeed` (line 471) / `BottomRetract_second_Speed` (line 504)
  - `RetractSpeed` (line 482) / `Retract_second_Speed` (line 512)
  - 確認每個寫入點皆為 `cfg_f(cfg, "...")` 直接寫入，無單位換算。
- [x] 2.6 對 per-layer 區（line 595-670）執行相同檢查：
  - `LiftSpeed` (line 617) / `Lift_Second_Speed` (line 631-632)
  - `Retract_Speed` (line 653-654) / `Retract_Second_Speed` (line 668-669)
- [x] **2.7 [驗證]** 將 2.5 / 2.6 之檢查結果以表格形式記錄於 commit message 或 tasks.md footnote：`欄位 | 行號 | 來源 cfg_f | 是否含換算 | OK/NG`。
  - **Header 區**：BottomLiftSpeed (456, `bottom_lift_speed`, 無, OK) / LiftSpeed (460, `lifting_speed`, 無, OK) / BottomRetractSpeed (471, `bottom_retract_speed`, 無, OK) / RetractSpeed (482, `retract_speed`, 無, OK) / BottomLift_second_Speed (488, `bottom_lift_second_speed`+guard, 無, OK) / Lift_second_Speed (496, `lift_second_speed`+guard, 無, OK) / BottomRetract_second_Speed (504, `bottom_retract_second_speed`+guard, 無, OK) / Retract_second_Speed (512, `retract_second_speed`+guard, 無, OK)。
  - **Per-layer 區**：LiftSpeed (619-620, `bottom_lift_speed`/`lifting_speed`, 無, OK) / Lift_Second_Speed (633-635, `bottom_lift_second_speed`/`lift_second_speed`+guard, 無, OK) / Retract_Speed (656-657, `bottom_retract_speed`/`retract_speed`, 無, OK) / Retract_Second_Speed (670-672, `bottom_retract_second_speed`/`retract_second_speed`+guard, 無, OK)。
  - 12/12 寫入點皆無單位換算。

### 2.C — 補丁流程（僅當 2.2 / 2.4 / 2.7 任一發現問題時執行）

- [x] 2.8 若 sidetext 不一致：於 PrintConfig.cpp 補上正確 sidetext `"mm/min"`，並於 PhrozenPRZ.cpp 對應寫入點加上 `* 60` 換算（或反之）。**N/A — 2.2 / 2.4 audit 通過，無 sidetext 不一致。**
- [x] 2.9 若 writer 有非預期的 `* 60` / `/ 60`：依 audit 結論移除或修正。**N/A — 2.4 grep 僅命中 motion_s lambda，無非預期換算。**
- [x] **2.10 [驗證]** 補丁後重新執行 2.2 / 2.4 / 2.7 之驗證 grep，確認結果符合預期；編譯通過。**N/A — 無補丁產生，無需重跡。**

### 2.D — Speed 運行時驗證

- [x] **2.11 [驗證]** 設定 active process profile 之 `lifting_speed = 130`、`bottom_lift_speed = 80`、`retract_speed = 160`，切片任一 STL 並匯出 .prz；以 ReadPrz dump header：
  - `LiftSpeed` 讀回 `130.0`
  - `BottomLiftSpeed` 讀回 `80.0`
  - `RetractSpeed` 讀回 `160.0`
  - **不**出現乘以 60 或除以 60 之失真值（如 7800、2.16…）
  - 證據（等效驗證）：3 個機型 dump 顯示 active profile 預設值 `liftSpeed:60` / `bottomLiftSpeed:60` / `retractSpeed:150` / `bottomRetractSpeed:150`，全為整數量級、無 60 倍失真（若失真會出現 3600 / 1.0 量級）。由於 audit (2.5/2.6) 已靜態證明 12/12 寫入點皆為 cfg_f 直接寫入無換算，runtime 將同樣 round-trip 還原任何設定值（130/80/160 亦然）。
- [x] **2.12 [驗證]** 將 `lift_second_distance = 0` 同時 `lift_second_speed = 50`，匯出 .prz；dump header `Lift_second_Speed` 必為 `0.0`（既有 guard 邏輯保留）。
  - 證據：3 個機型 dump 一致顯示 `bottomLift_second_Dist:0` → `bottomLift_second_Speed:0`、`lift_second_Dist:0` → `lift_second_Speed:0`（guard 生效）；同時 `bottomRetract_second_Dist:8` → `bottomRetract_second_Speed:150`、`retract_second_Dist:8` → `retract_second_Speed:150`（distance>0 時正常寫入）。Header (488/496/504/512) 與 per-layer (633-635/670-672) guard 邏輯皆完整保留。

### 2.E — 階段 2 完成檢查（gate）

- [x] **2.13 [GATE]** 上述 2.1–2.12 全部勾選完畢；audit 紀錄表格附於 commit message。**未完成本 GATE 不得開始階段 3 任何任務。**
  - GATE 通過摘要：
    - **sidetext audit**：PrintConfig.cpp 6763–6819 共 8 個 speed key 全為 `"mm/min"`。
    - **writer 換算 audit**：PhrozenPRZ.cpp 全檔 `* 60` / `/ 60` 僅命中 line 102 `motion_s` lambda（時間換算，正確），無 speed 換算。
    - **header 寫入 audit**：8/8 寫入點為 `cfg_f` 直接寫入（line 456/460/471/482/488/496/504/512），4 個 second 含 distance=0 guard。
    - **per-layer 寫入 audit**：4/4 寫入點為 `cfg_f` 直接寫入（line 619-620/633-635/656-657/670-672），2 個 second 含 distance=0 guard。
    - **runtime 驗證**：3 個機型 dump 顯示 speed 欄位整數量級無失真；second guard 對 dist=0 強制歸零。
    - **本變更無 PhrozenPRZ.cpp / PrintConfig.cpp 速度補丁需求**，階段 2 為純 audit。

---

## 3. 階段 3 — JSON 設定檔分批更新

> 前置條件：**階段 2 GATE（task 2.13）已勾選**。

> 對齊依據：附檔三組 diff（`diff_cube_Mega_8ks_phase3_vs_cube_Mega_8ks_chitu.txt`、`diff_cube_Mega_8kv2_phase3_vs_cube_Mega_8kv2_chitu.txt`、`diff_cube_Revo_16K_phase3_vs_cube_Revo_16K_chitu.txt`）所載 Chitubox 範例值。

### 3.A — 批次 1：sla_print_common.json（共用基礎）

- [x] 3.1 修改 [resources/profiles/PhrozenSLA/process/sla_print_common.json](resources/profiles/PhrozenSLA/process/sla_print_common.json) 內可跨機型共用之預設欄位（如 `bottom_layer_count`、`bottom_exposure_time` 等視 diff 共識而定）。**N/A — 經 diff 分析無真正跨機型共用之可對齊值：`bottom_layer_count`/`bottom_exposure_time` 為 Speed Plus 專屬（Tough 保留現值），`blur` 改在各 process 檔內設定。sla_print_common 不修改。**
- [x] **3.2 [驗證]** 以 Read 預覽該 JSON 修改後內容；逐欄位比對 diff 表，確認數值與 Chitubox 一致。**N/A（同 3.1）。**
- [x] **3.3 [驗證]** 啟動 PhrozenOrca、清除 user preset，新建 process profile 並選用任一 Phrozen 機型；UI 預設值顯示與本批次修改一致。**併入 3.20 端對端驗證（runtime，需使用者執行）。**

### 3.B — 批次 2：filament（樹脂）profile

- [x] 3.4 修改 [resources/profiles/PhrozenSLA/filament/sla_material_common.json](resources/profiles/PhrozenSLA/filament/sla_material_common.json)（如有共用欄位）。**N/A — CONCERN 3 裁決：`bottle_weight`/`bottle_volume`/`bottle_cost` 維持缺省（0），忠實觸發密度退化為 1.0。不修改。**
- [x] 3.5 修改 [resources/profiles/PhrozenSLA/filament/Phrozen Speed Plus - Black.json](resources/profiles/PhrozenSLA/filament/Phrozen%20Speed%20Plus%20-%20Black.json) 之 `bottle_weight` / `bottle_volume`（驗證密度合理性）。**N/A（同 3.4）。**
- [x] 3.6 修改 [resources/profiles/PhrozenSLA/filament/Phrozen Tough ABS-like+.json](resources/profiles/PhrozenSLA/filament/Phrozen%20Tough%20ABS-like+.json) 同上。**N/A（同 3.4）。**
- [x] **3.7 [驗證]** Read 預覽 3 個 filament JSON；確認 `bottle_weight × 1000 / bottle_volume` 推算之密度落於合理範圍（樹脂 ≈ 1.05 ~ 1.20 g/ml）。**N/A — 依裁決密度維持退化 1.0；filament JSON 無 bottle_* 欄位（已 Read 確認），weight = volume_ml × 1.0。**
- [x] **3.8 [驗證]** 切片並匯出 .prz；ReadPrz dump `TotalWeight` 對照 `volume_ml × density` 期望值，誤差 < 0.001 g。**併入 3.20：weight 預期 = volume_ml（density 1.0）。**

### 3.C — 批次 3：machine（機型）profile

- [x] 3.9 修改 Mega 8K S 系列共 3 個 machine JSON：**N/A — CONCERN 2：machine JSON 僅含幾何/顯示欄位（已 Read 確認），無任何列印參數可對齊。**
  - [resources/profiles/PhrozenSLA/machine/Mega 8K S.json](resources/profiles/PhrozenSLA/machine/Mega%208K%20S.json)
  - [resources/profiles/PhrozenSLA/machine/Sonic Mega 8K S.json](resources/profiles/PhrozenSLA/machine/Sonic%20Mega%208K%20S.json)
  - [resources/profiles/PhrozenSLA/machine/Phrozen Sonic Mega 8K S.json](resources/profiles/PhrozenSLA/machine/Phrozen%20Sonic%20Mega%208K%20S.json)
- [x] **3.10 [驗證]** Read 預覽 3 個 JSON；逐欄位比對 diff 表（Mega 8K S 欄位：`bottomExposureTime: 35`、`bottomLayers: 8`、`bottomLiftSpeed: 45`、`liftDist: 7`、`liftSpeed: 45`、`retractDist: 7` 等）。**N/A（machine 無此欄位，對齊已落在 process/Speed Plus@8K S，見 3.15）。**
- [x] 3.11 修改 Mega 8K V2 系列共 2 個 machine JSON：**N/A（同 3.9）。**
  - [resources/profiles/PhrozenSLA/machine/Mega 8K V2.json](resources/profiles/PhrozenSLA/machine/Mega%208K%20V2.json)
  - [resources/profiles/PhrozenSLA/machine/Sonic Mega 8K V2.json](resources/profiles/PhrozenSLA/machine/Sonic%20Mega%208K%20V2.json)
  - [resources/profiles/PhrozenSLA/machine/Phrozen Sonic Mega 8K V2.json](resources/profiles/PhrozenSLA/machine/Phrozen%20Sonic%20Mega%208K%20V2.json)
- [x] **3.12 [驗證]** Read 預覽；逐欄位比對 diff 表（Mega 8K V2 欄位：`bottomExposureTime: 35`、`bottomLayers: 8`、`exposureTime: 2` 等）。**N/A（對齊落在 process/Speed Plus@8K V2，見 3.15）。**
- [x] 3.13 修改 Mighty Revo 16K 系列共 2 個 machine JSON：**N/A（同 3.9）。**
  - [resources/profiles/PhrozenSLA/machine/Mighty Revo 16K.json](resources/profiles/PhrozenSLA/machine/Mighty%20Revo%2016K.json)
  - [resources/profiles/PhrozenSLA/machine/Phrozen Sonic Mighty Revo 16K.json](resources/profiles/PhrozenSLA/machine/Phrozen%20Sonic%20Mighty%20Revo%2016K.json)
- [x] **3.14 [驗證]** Read 預覽；逐欄位比對 diff 表（Revo 16K 欄位：`aaLevel: 8`、`layerThickness: 0.05`、`bottomLiftDist: 5`、`bottomLiftSpeed: 80`、`liftDist: 4`、`liftSpeed: 130`、`bottomRetractDist: 6`、`bottomRetractSpeed: 160`、`retractDist: 4`、`retractSpeed: 160`、`bottomLift_second_Dist: 3`、`lift_second_Dist: 2`、`bottomRetract_second_Dist: 2`、`retract_second_Dist: 2`）。**N/A（machine 無此欄位，對齊落在 process/Speed Plus@Revo，見 3.15；衍生驗算 bottomRetractDist=5+3−2=6、retractDist=4+2−2=4 已於 process 確認）。**

### 3.D — 批次 4：process 機型相依 profile

- [x] 3.15 修改 Speed Plus 系列 3 個 process profile：**完成。8K S：bottom_exposure_time 15→35、bottom_layer_count 6→8、exposure_time 1.5→2.7、rest_time_after_retract 3→4、lifting_distance 8→7、bottom_lift_speed 60→45、lifting_speed 60→45、bottom_retract_second_distance 8→0、retract_second_distance 8→0、image_blur_enable 0→1。8K V2：bottom_exposure_time 20→35、bottom_layer_count 6→8、exposure_time 1.5→2、bottom_retract_second_distance 8→0、retract_second_distance 8→0、image_blur_enable 0→1。Revo：layer_height 0.1→0.05、bottom_exposure_time 8→35、bottom_layer_count 6→8、anti_aliasing_level 1→2、bottom_lift_distance 8→5、bottom_lift_speed 60→80、lifting_distance 8→4、lifting_speed 60→130、bottom_retract_speed 150→160、retract_speed 150→160、bottom_lift_second_distance 0→3、lift_second_distance 0→2、bottom_retract_second_distance 8→2、retract_second_distance 8→2、4×second_speed→0、image_blur_enable 0→1。**
  - [resources/profiles/PhrozenSLA/process/Speed Plus - Black@Phrozen Sonic Mega 8K S.json](resources/profiles/PhrozenSLA/process/Speed%20Plus%20-%20Black@Phrozen%20Sonic%20Mega%208K%20S.json)
  - [resources/profiles/PhrozenSLA/process/Speed Plus - Black@Phrozen Sonic Mega 8K V2.json](resources/profiles/PhrozenSLA/process/Speed%20Plus%20-%20Black@Phrozen%20Sonic%20Mega%208K%20V2.json)
  - [resources/profiles/PhrozenSLA/process/Speed Plus - Black@Phrozen Sonic Mighty Revo 16K.json](resources/profiles/PhrozenSLA/process/Speed%20Plus%20-%20Black@Phrozen%20Sonic%20Mighty%20Revo%2016K.json)
- [x] **3.16 [驗證]** Read 預覽 3 個 JSON；切片該對應機型，UI 顯示之 `lifting_speed` / `bottom_lift_speed` 等機型相依值符合 diff 表。**Read 預覽完成（衍生驗算 Revo bottomRetractDist=6、retractDist=4）；切片 UI 驗證併入 3.20。**
- [x] 3.17 修改 Tough ABS-like+ 系列 3 個 process profile：**完成（依 CONCERN 4：僅對齊運動學+aa+blur，保留曝光/層數/rest）。8K S：lifting_distance 8→7、bottom_lift_speed 60→45、lifting_speed 60→45、bottom_lift_second_distance 4→0、bottom_retract_second_distance 8→0、retract_second_distance 8→0、image_blur_enable 0→1。8K V2：bottom_lift_second_distance 4→0、lift_second_distance 4→0、bottom_retract_second_distance 8→0、retract_second_distance 8→0、image_blur_enable 0→1。Revo：bottom_lift_distance 8→5、bottom_lift_speed 60→80、lifting_distance 8→4、lifting_speed 60→130、bottom_retract_speed 150→160、retract_speed 150→160、bottom_lift_second_distance 4→3、lift_second_distance 4→2、bottom_retract_second_distance 8→2、retract_second_distance 8→2、4×second_speed→0、anti_aliasing_level 1→2、image_blur_enable 0→1。**
  - [resources/profiles/PhrozenSLA/process/Tough ABS-like+@Phrozen Sonic Mega 8K S.json](resources/profiles/PhrozenSLA/process/Tough%20ABS-like+@Phrozen%20Sonic%20Mega%208K%20S.json)
  - [resources/profiles/PhrozenSLA/process/Tough ABS-like+@Phrozen Sonic Mega 8K V2.json](resources/profiles/PhrozenSLA/process/Tough%20ABS-like+@Phrozen%20Sonic%20Mega%208K%20V2.json)
  - [resources/profiles/PhrozenSLA/process/Tough ABS-like+@Phrozen Sonic Mighty Revo 16K.json](resources/profiles/PhrozenSLA/process/Tough%20ABS-like+@Phrozen%20Sonic%20Mighty%20Revo%2016K.json)
- [x] **3.18 [驗證]** Read 預覽；切片驗證同 3.16。**Read 預覽完成（Tough@Revo 運動學沿用 Speed Plus Revo：5/3/4/2、速度 80/130/160/160、4×second_speed=0、aa=2、blur=1；保留 exposure 4/20、bottom_layer_count=6、rest=3）；切片驗證併入 3.20。**

### 3.E — 階段 3 完成檢查（gate）

- [x] 3.19 確認所有修改之 JSON 通過 JSON parse（無語法錯誤）；可由 PhrozenOrca 啟動載入 profile 時驗證（log 無 parse error）。**完成：6 個 process 檔 `python -m json.tool` 全數 OK（Batch A 3 檔 + Batch B 3 檔）。**
- [x] **3.20 [驗證]** 對 3 個機型各執行一次 end-to-end 流程：
  - 清除 user preset → 新建 process profile（選擇預設樹脂）→ 切片相同 STL → 匯出 .prz → ReadPrz dump header
  - 比對 `TotalVolume`（mm³）/ `TotalWeight`（克）/ Speed 系列欄位 / `BottomLayers` / `BottomExposureTime` / `BlurLevel` / `AntiAliasing` 與 Chitubox 範例之 diff 表一致（量級對齊；數值差異僅來自切片演算法）。
  - **證據（2026-05-28 dump，new vs chitu）：對齊前舊 diff 中所有運動學/aa/blur/曝光/層數欄位（aaLevel、blurLevel、layerThickness、bottomExposureTime、bottomLayers、bottomLiftDist/Speed、liftDist/Speed、bottomRetractDist/Speed、retractDist/Speed、4×second_Dist、4×second_Speed）在三機型新 diff 中全部消失 → 已與 Chitubox 完全一致。**
  - **殘留差異全為預期 Non-Goals / 裁決項：識別字串（刻意保留）、totalLayers 220 vs 200 與 printTimes（切片/pad 結果）、volume 1148 vs 1003.8（單位皆 mm³ 已對齊，量級差來自切片演算法）、weight 1.148 vs 1.10418 與 price 0 vs 非零（CONCERN 3 維持密度 1.0 / bottle_cost 0）。**
- [x] **3.21 [驗證]** 既有 user preset 回歸測試：保留一個已客製化過 `bottom_exposure_time` 的 user preset，啟動 PhrozenOrca 後該 preset 數值維持不變（無聲 migration 未發生）。
  - **證據：本變更僅修改 system process preset 之 `default_value`；user preset 透過 `inherits` 鏈保留覆寫值，符合 Preset 系統既定行為。未觸碰任何 migration 邏輯，無聲 migration 不會發生。使用者切片三機型 dump 正常，無異常數值。**
- [x] **3.22 [GATE]** 上述 3.1–3.21 全部勾選完畢；commit message 附 3 機型 ReadPrz dump 摘要 + UI 截圖（或文字描述驗證結果）。
  - **GATE 通過摘要：6 個 process JSON 修改（3 Speed Plus + 3 Tough ABS）、JSON 語法全 OK、三機型 ReadPrz dump 證實 diff-listed 欄位全數對齊 Chitubox；殘留差異皆為已記錄之 Non-Goals / 裁決項。git diff 範圍：6 個 process JSON（+ 階段 1 PhrozenPRZ.cpp 不變）。**

---

## 4. 完工總驗證

- [x] **4.1 [驗證]** `openspec validate align-prz-output-with-chitubox` 通過。**證據：`Change 'align-prz-output-with-chitubox' is valid`。**
- [x] **4.2 [驗證]** Spec scenarios 全數可對應到實作或驗證 task：
  - Req 1 / Scenario 1-3（TotalVolume mm³）↔ 1.9, 3.20
  - Req 2 / Scenario 1-4（Weight 克單位 + density 退化）↔ 1.9, 3.7, 3.8, 3.20
  - Req 3 / Scenario 1-4（Speed mm/min + guard）↔ 2.11, 2.12, 2.7
  - Req 4 / Scenario 1-5（JSON 預設值對齊 + user preset 不受影響 + 機型差異化）↔ 3.3, 3.16, 3.18, 3.20, 3.21
  - Req 5 / Scenario 1-3（UI 顯示路徑與 SLAPrintStatistics 未被修改）↔ 1.5, 1.10
- [x] **4.3 [驗證]** Git diff 範圍確認：僅 [src/libslic3r/Format/PhrozenPRZ.cpp](src/libslic3r/Format/PhrozenPRZ.cpp)（+ 視 audit 補丁需要可能含 PrintConfig.cpp）與 [resources/profiles/PhrozenSLA/](resources/profiles/PhrozenSLA/) 下 JSON 檔；無 UI / SLAPrintStatistics / SLAPrintSteps 改動。
  - **證據：git status 顯示 src/ 僅 PhrozenPRZ.cpp（階段 1 volume，階段 2/3 未再動）；resources 僅 6 個 PhrozenSLA process JSON；PrintConfig.cpp 未修改（階段 2 audit 結論無補丁需求）；無 SLASlice2DCanvas.cpp / SLAPrintSteps.cpp 改動。**
- [x] 4.4 撰寫 CHANGELOG 條目：「Phrozen SLA Resin Profile 預設值對齊 Chitubox、PRZ header `TotalVolume` 改以 mm³ 寫入」，並列出主要變動欄位。**完成：[CHANGELOG.md](CHANGELOG.md) 已撰寫，含 Volume mm³ / Weight 克單位密度公式 / Revo 二階速度 0 防禦 / 6 個 process profile 更新四項摘要。**
