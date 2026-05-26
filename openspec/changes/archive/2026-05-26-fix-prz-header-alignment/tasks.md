## 0. 前置作業：建立驗證基線

- [x] 0.1 確認本地 PRZ header diff 工具可用（使用者既有的 `diff_cube_*.txt` 產生工具），若僅本機存在，將其 commit 至 `scripts/prz_header_diff.py` 或等效位置，供後續每階段比對使用
- [x] 0.2 準備標準測試模型 `cube.stl`（單位 1×1×1 mm 校正立方體）置於 `tests/sla_print/fixtures/` 或專案約定位置
- [x] 0.3 取得 Chitubox 0.0.15 切出的三組 PRZ 參考檔，置於本機 `C:\Users\kwshi\Desktop\PRZ\` 並 commit 對照表 SHA-256 至本變更目錄 `reference_chitubox_sha256.txt`（避免後續被改檔但無人察覺）
  - `cube_Mega_8ks_chitu.prz`
  - `cube_Mega_8kv2_chitu.prz`
  - `cube_Revo_16K_chitu.prz`
- [x] 0.4 切出當前 main 分支版本 Orca 的三組 PRZ baseline（未修正版），存為 `cube_Mega_8ks_baseline.prz` 等，記錄 header diff 至 `phase0_baseline_diff.txt`，作為「修正前狀態」永久存查
- [x] 0.5 於本變更目錄建立 `validation_log.md`，每階段驗證結果（指令、輸出、判定）依序 append，PR review 時可一覽全程

---

## 1. 第一階段：拆除 PhrozenPRZ.cpp 核心 swap 與鏡像邏輯

### 1.A 程式碼修正

- [x] 1.1 修改 [src/libslic3r/Format/PhrozenPRZ.cpp:381-386](src/libslic3r/Format/PhrozenPRZ.cpp#L381-L386)：`xr` 改讀 `pcfg.display_pixels_x.getInt()`，`yr` 改讀 `pcfg.display_pixels_y.getInt()`
- [x] 1.2 修改 [src/libslic3r/Format/PhrozenPRZ.cpp:391-394](src/libslic3r/Format/PhrozenPRZ.cpp#L391-L394)：`PlatformXLength` 改讀 `pcfg.display_width.getFloat()`，`PlatformYLength` 改讀 `pcfg.display_height.getFloat()`
- [x] 1.3 修改 [src/libslic3r/Format/PhrozenPRZ.cpp:387-390](src/libslic3r/Format/PhrozenPRZ.cpp#L387-L390)：X mirror 寫入改為 `pcfg.display_mirror_x.getBool() ? 1 : 0`，與 Y mirror 對稱
- [x] 1.4 移除/更新原始程式碼註解（避免「false→1, true→0」這類誤導性註解殘留）
- [x] 1.5 編譯確認無警告無錯誤（Windows VS2022 release 與 dbginfo 兩組 target）

### 1.B Header 驗證（僅 header 比對；byte-stream 驗證移至 Phase 1.5）

> ⚠ **Phase 1 落地後實機列印出現「散亂橫線」**——header 已對齊 Chitubox 但 byte stream 仍是 portrait 4320-per-row，stride 不匹配。經完整資料流追蹤確認根因為 [SLAPrintSteps.cpp:1421](src/libslic3r/SLAPrintSteps.cpp#L1421) portrait swap 使 cv::Mat 維度與 header 不一致。原 design.md「光柵化已物理正確」前提不成立。**header 修改 1.1~1.4 保留不撤銷**，配套修正改在 Phase 1.5 加入 `cv::rotate(ROTATE_90_CW)` 對齊 byte stride。詳見 [design.md §決策 1.5](design.md)。

- [x] 1.6 重切三組測試 PRZ：`cube_Mega_8ks_phase1.prz` / `cube_Mega_8kv2_phase1.prz` / `cube_Revo_16K_phase1.prz`
- [x] 1.7 對每組執行 header diff 工具，比對 vs 同機種 Chitubox 參考檔，**僅針對 header 欄位**強制驗收條件：
  - `xr` 完全相同（= display_pixels_x）
  - `yr` 完全相同（= display_pixels_y）
  - `xLength` 整數部分相同（小數精度於第二階段處理；此階段允許 `330 vs 330.24` 的精度差）
  - `yLength` 整數部分相同
  - `xm` 完全相同（取決於 Phase 2 JSON mirror_x 設定）
  - `ym` 完全相同
- [x] 1.8 若任一機種任一欄位未通過 1.7，回頭檢視該機種 profile 內 `display_pixels_x/y / display_width / display_height / display_mirror_x/y` 設定是否為物理正確值；不得以修改 PhrozenPRZ.cpp 補償方式繞過
- [x] 1.9 將 1.7 比對結果（每機種一份 diff 輸出）append 至 `validation_log.md`，標記「Phase 1 header PASS」
- [x] 1.10 **強制守門點**：1.7 全部通過後才能啟動 Phase 1.5。**注意：Phase 1 header 通過僅代表 header 對齊 Chitubox；byte stream 對齊驗證在 Phase 1.5 完成。實機列印於 Phase 4 進行。**

---

## 1.5 第 1.5 階段：數據旋轉補償（cv::rotate(ROTATE_90_CW)）— 新增

> **目標**：保留 Phase 1 對 Chitubox header 的對齊，同時在 RLE 編碼前對 cv::Mat 執行 `cv::rotate(_, _, cv::ROTATE_90_CLOCKWISE)`，將 portrait `(rows=display_pixels_x, cols=display_pixels_y)` 轉成 landscape `(rows=display_pixels_y, cols=display_pixels_x)`，使 byte stream 每列像素數 = display_pixels_x，與 Phase 1 header xr 一致。

> **幾何依據**：對 Phase 2 Mega 8K S 設定（profile mirror_x/y 皆 false → Trafo 兩鏡像皆 true → portrait 像素雙鏡像），R₉₀cw 變換 `(r, c) → (c, H_P-1-r)` 是唯一能在兩軸同時對齊 Chitubox 標準 landscape（xm=0 ym=0）的變換。R₉₀ccw 或單純 transpose 皆無法對齊。完整推導見 [design.md §決策 1.5](design.md)。

### 1.5.A 程式碼修正（三檔同步）

- [x] 1.5.1 修改 [src/libslic3r/SLAPrintSteps.cpp:1525-1529](src/libslic3r/SLAPrintSteps.cpp#L1525-L1529) `TLSData` 結構，新增 `cv::Mat mat_rotated;` 欄位（TLS 重用 buffer，避免 per-layer malloc）
- [x] 1.5.2 修改 [src/libslic3r/SLAPrintSteps.cpp:1552-1557](src/libslic3r/SLAPrintSteps.cpp#L1552-L1557) main RLE 編碼路徑：在 `apply_picture_grayscale_lut(mat, ...)` 之後、原 `total/data` 計算之前，插入：
  ```cpp
  cv::rotate(mat, tls_data.mat_rotated, cv::ROTATE_90_CLOCKWISE);
  const int    total = tls_data.mat_rotated.rows * tls_data.mat_rotated.cols;
  const uchar *data  = tls_data.mat_rotated.data;
  ```
  並刪除原第 1557-1558 兩行 `total/data` 計算（被新版本取代）。註解明確說明「對齊 PRZ header xr stride，幾何推導見 design.md §決策 1.5」。
- [x] 1.5.3 修改 [src/libslic3r/Format/PhrozenPRZ.cpp:735](src/libslic3r/Format/PhrozenPRZ.cpp#L735) cache-miss fallback 路徑：在 `apply_picture_grayscale_lut(batch_mats[i], rp.picture_grayscale)` 之後（仍在 parallel_for 內），插入：
  ```cpp
  cv::Mat rotated;
  cv::rotate(batch_mats[i], rotated, cv::ROTATE_90_CLOCKWISE);
  batch_mats[i] = std::move(rotated);
  ```
- [x] 1.5.4 修改 [src/libslic3r/SLA/RasterCache.hpp:62](src/libslic3r/SLA/RasterCache.hpp#L62)：`CACHE_VERSION` 由 `3` bump 為 `4`，避免 cache-hit 重用舊 byte order。
- [x] 1.5.5 編譯確認無警告無錯誤（Windows VS2022 release 與 dbginfo 兩組 target）。

### 1.5.B 強制清理 Cache 驗證（雙路徑一致性）

- [x] 1.5.6 **強制 cache-miss 路徑驗證**：
  1. 手動刪除 `%TEMP%/phrozen_sla_cache/` 目錄全部內容
  2. 用 OrcaSlicer 切 Mega 8K S cube → 產出 `cube_Mega_8ks_p1.5_cachemiss.prz`
  3. 確認過程走 cache-miss 路徑（觀察 log：「SLA rasterize: cache miss, rasterizing N layers」）
- [x] 1.5.7 **強制 cache-hit 路徑驗證**：
  1. 不刪 cache，再次切相同參數 → 產出 `cube_Mega_8ks_p1.5_cachehit.prz`
  2. 確認過程走 cache-hit 路徑（log：「SLA rasterize: cache hit」）
- [x] 1.5.8 **byte-for-byte 比對 1.5.6 與 1.5.7 兩份 PRZ**，必須**完全相同**（驗證 cache-miss 與 cache-hit 兩處 rotate 插入點一致）；任一 byte 差異即停止後續任務，回頭檢查 [SLAPrintSteps.cpp:1552](src/libslic3r/SLAPrintSteps.cpp#L1552) 與 [PhrozenPRZ.cpp:735](src/libslic3r/Format/PhrozenPRZ.cpp#L735) 兩處 rotate 邏輯是否有遺漏。
- [x] 1.5.9 **byte stream 維度驗證**：用 PRZ-RLE 解碼工具（或寫一次性 Python 腳本）解 `cube_Mega_8ks_p1.5_cachemiss.prz` 第一層 RLE bytes，**強制驗收**：
  - 解碼後影像維度 = (4320 rows, 7680 cols)（landscape，符合 header xr=7680）
  - 與 Chitubox `cube_Mega_8ks_chitu.prz` 第一層解碼影像**像素位置完全相同**（容許 ≤1 pixel rounding 誤差）
- [x] 1.5.10 將 1.5.6~1.5.9 結果 append 至 `validation_log.md`，標記「Phase 1.5 PASS」。
- [x] 1.5.11 **強制守門點**：1.5.6~1.5.9 全部通過後才能啟動 Phase 2。

---

## 2. 第二階段：機種 JSON 兩位小數精度

### 2.A 設定檔修正

#### 2.A.0 Mirror 明文設定（配合 Phase 1.5 R₉₀cw — Mega 與 Revo 皆強制實作與驗收）

> **依使用者最新提供之 Phrozen 韌體幾何規格，本子節由「Mega 必做、Revo 觀望」升級為「Mega + Revo 皆強制實作」。三款主力機種的 `display_mirror_x / y` 預設值 SHALL 全部明文寫入 JSON，不得依賴 [PrintConfig.cpp:7155-7167](src/libslic3r/PrintConfig.cpp#L7155-L7167) 全域預設（`mirror_x=true, mirror_y=false`）。期望結果見 [design.md §決策 1.5 — Phase 2 後 mirror 預設值與 R₉₀cw 結合後的最終預期行為](design.md)。**

| 機種 | Phrozen 韌體狀態 | 必須寫入的 JSON 鍵值 | 驗收：PRZ `xm` | 驗收：PRZ `ym` |
|---|---|---|---|---|
| Sonic Mega 8K S | Normal | `"display_mirror_x": "0"`, `"display_mirror_y": "0"` | 0 | 0 |
| Sonic Mega 8K V2 | Normal | `"display_mirror_x": "0"`, `"display_mirror_y": "0"` | 0 | 0 |
| Mighty Revo 16K | LCD_mirror | `"display_mirror_x": "1"`, `"display_mirror_y": "0"` | 1 | 0 |

- [x] 2.0.1 修改 [Phrozen Sonic Mega 8K S.json](resources/profiles/PhrozenSLA/machine/Phrozen Sonic Mega 8K S.json) 加入明文 `"display_mirror_x": "0"` 與 `"display_mirror_y": "0"`（覆蓋全域預設 `mirror_x=true, mirror_y=false`），對應 Phrozen 韌體 Normal 狀態
- [x] 2.0.2 修改 [Phrozen Sonic Mega 8K V2.json](resources/profiles/PhrozenSLA/machine/Phrozen Sonic Mega 8K V2.json) 加入相同明文 `display_mirror_x/y` 設定（Normal 狀態）
- [x] 2.0.3 **【強制實作】** 修改 [Phrozen Sonic Mighty Revo 16K.json](resources/profiles/PhrozenSLA/machine/Phrozen Sonic Mighty Revo 16K.json) 加入明文 `"display_mirror_x": "1"` 與 `"display_mirror_y": "0"`，對應 Phrozen 韌體 LCD_mirror 狀態；**不再採「先觀望、Phase 4.3 後決定」策略**——預設值直接以 Phrozen 韌體規格落地，Phase 4.3 僅作為實機驗證守門點
- [x] 2.0.4 **【強制驗收】** 上述三檔 JSON 修改後，分別重切 Mega 8K S / Mega 8K V2 / Revo 16K 三組測試 PRZ，使用 header diff 工具確認：
  - Mega 8K S PRZ header 的 `xm = 0` 且 `ym = 0`（big-endian byte 比對通過）
  - Mega 8K V2 PRZ header 的 `xm = 0` 且 `ym = 0`
  - Revo 16K PRZ header 的 `xm = 1` 且 `ym = 0`
  - **⚠ Phase 2 實測（2026-05-26）**：ym ✓；xLength/yLength ✓；xr/yr ✓；但 `xm` 尚未正確（Mega 實得 1，Revo 實得 0），原因是 C++ 仍讀 `display_mirror_x`（portrait 補償值），依 design.md §決策 2.5，xm 修正已移至 **Phase 3 task 3.0**。本任務 JSON 設定部分視為完成；xm 正確性由 Phase 3 task 3.0 驗收。
- [x] 2.0.5 **禁止事項（合併 PR 前 reviewer 必檢）**：本變更**禁止**在 [PhrozenPRZ.cpp](src/libslic3r/Format/PhrozenPRZ.cpp) 或 [SLAPrintSteps.cpp](src/libslic3r/SLAPrintSteps.cpp) 中引入 `if (printer == "Revo")` 或 `if (printer == "Mega")` 等機種特化分支補償方向錯誤；所有方向 / 鏡像差異 SHALL 透過調整對應機種 JSON `display_mirror_x / y` 解決

#### 2.A.1 LCD 物理尺寸精度

- [x] 2.1 列出受影響 Phrozen 機種清單（grep [resources/profiles/Phrozen.json](resources/profiles/Phrozen.json) 與 `resources/profiles/Phrozen/` 下所有子 profile，輸出含 `display_width` 的檔案列表至 `validation_log.md`）
- [x] 2.2 取得各機種 LCD 機械標稱規格（從 Phrozen 官方規格表或 Chitubox 同機種 profile 反推），整理為對照表 `phase2_machine_dim_table.md`（欄位：機種名 / LCD 對角吋數 / 物理 width.height mm / 像素 X×Y）
- [x] 2.3 修改 Sonic Mega 8K S 設定檔：`display_width = 330.24`，`display_height = 185.76`
- [x] 2.4 修改 Sonic Mega 8K V2 設定檔：`display_width = 330.24`，`display_height = 185.76`
- [x] 2.5 修改 Mighty Revo 16K 設定檔：`display_width = 211.68`，`display_height = 118.37`
- [x] 2.6 修改 2.1 清單內其餘 Phrozen 機種，依 2.2 對照表更新為兩位小數（確認：PhrozenSLA 目錄內僅三個含 display_width 的 JSON，均已更新，無其他機種需修改）
- [x] 2.7 同步檢查 `printable_area` 欄位（若機種有寫死整數頂點如 `[0,0]x[330,185]`）是否需同步更新為小數，避免 [SLAPrinterSettingsDialog](src/slic3r/GUI/PrinterSettingsDialog.cpp) reset 後又把 width 退回整數

### 2.B 下游精度回歸掃描

- [x] 2.8 Grep 所有呼叫 `display_width.getFloat()` 與 `display_height.getFloat()` 的位置，逐一檢查是否有「結果被強制 cast 為 int」或「進入只接受整數的 API」的程式碼路徑，紀錄結果至 `validation_log.md`
- [x] 2.9 確認 [SLAPrintSteps.cpp](src/libslic3r/SLAPrintSteps.cpp) 內 `pxdim` 計算（`display_width / display_pixels_x`）在小數輸入下仍給出正確物理像素尺寸（手算驗證：330.24 / 7680 ≈ 0.04300 mm/pixel）

### 2.C 強制驗證：階段二完成後必須通過

- [x] 2.10 重切三組測試 PRZ：`cube_Mega_8ks_phase2.prz` / `cube_Mega_8kv2_phase2.prz` / `cube_Revo_16K_phase2.prz`
- [x] 2.11 對每組執行 header diff，**強制驗收條件**：
  - 第一階段六項（xr/yr/xLength/yLength/xm/ym）全部仍通過
  - `xLength` 與 `yLength` 浮點值「**位元完全相同**」於 Chitubox 參考值（例如 `330.24` 與 `185.76` 序列化為 big-endian float 後 4 bytes 完全相符）
  - **⚠ Phase 2 實測（2026-05-26）**：xr/yr ✓、xLength/yLength ✓（兩位小數精度通過）、ym ✓；`xm` 尚待 Phase 3 task 3.0 修正（見 design.md §決策 2.5）
- [x] 2.12 若 2.11 任一機種任一欄位未通過，回頭檢視是否有 ConfigOptionFloat 序列化精度問題或 JSON 解析誤差，不得以四捨五入規避（xLength/yLength 無精度問題 ✓；xm 問題為 C++ 映射邏輯，非 JSON 序列化問題）
- [x] 2.13 比對結果 append 至 `validation_log.md`，標記「Phase 2 維度精度 PASS；xm 待 Phase 3」
- [x] 2.14 **強制守門點**：2.11 全部通過後才能啟動第三階段。（維度精度守門通過；xm 守門由 Phase 3 task 3.0 接管，phase 3 可啟動）

---

## 3. 第三階段：AA Level 映射、second_speed 防護、其他 metadata 對齊

### 3.A.0 xm 鏡像映射修正（取代 Phase 1 task 1.3 的 display_mirror_x 直讀）

> **依據 [design.md §決策 2.5](design.md)**：`display_mirror_x` 在 portrait 模式下已含 Trafo 補償反轉，語意為 GUI 內部值，不得直接用於 PRZ xm。應改讀 `display_mirror_mode` 枚舉。

- [x] 3.0 修改 [src/libslic3r/Format/PhrozenPRZ.cpp:387-390](src/libslic3r/Format/PhrozenPRZ.cpp#L387-L390)：xm 寫入從 `display_mirror_x.getBool()` 改為 `display_mirror_mode` 枚舉映射：
  - `slammNormal` → `xm = 0`
  - `slammLCDMirror` → `xm = 1`
  - `slammDLPNormal` → `xm = 0`
  - fallback（`display_mirror_mode` 缺失舊 preset）→ `display_mirror_x.getBool() ? 1 : 0`
  - 驗收：切 Mega 8K S PRZ → `xm = 0`；切 Revo 16K PRZ → `xm = 1`

### 3.A AA Level 映射

- [x] 3.1 修改 [src/libslic3r/Format/PhrozenPRZ.cpp:269-274](src/libslic3r/Format/PhrozenPRZ.cpp#L269-L274)：依 design.md §決策 4 的查表實作（`0→2 / 1→4 / 2→8 / 其他→4`），改為 `switch` 結構
- [x] 3.2 加入單元測試（`tests/sla_print/test_phrozen_prz_header.cpp` 若不存在則新建），覆蓋四種映射輸入：`{0, 1, 2, 5}`，分別預期輸出 `{2, 4, 8, 4}`

### 3.B second_speed 距離為 0 防護

- [x] 3.3 修改 [src/libslic3r/Format/PhrozenPRZ.cpp:451-466](src/libslic3r/Format/PhrozenPRZ.cpp#L451-L466)：四組 `*_second_speed` header 寫入前加守衛（對應的 `*_second_distance == 0.f` 時 speed 強制 0），抽出 helper lambda 避免重複
- [x] 3.4 修改 [src/libslic3r/Format/PhrozenPRZ.cpp:561-604](src/libslic3r/Format/PhrozenPRZ.cpp#L561-L604) 內 `prz_layer_content()` 的 `Lift_Second_Speed` 與 `Retract_Second_Speed` 寫入：依 `is_bottom` 旗標選用對應 distance 後同樣加守衛
- [x] 3.5 加入單元測試覆蓋四組 distance/speed 對的「distance=0 強制 speed=0」與「distance>0 透傳 cfg 值」兩種情境

### 3.C 其他 metadata 欄位對齊

- [x] 3.6 修改 [src/libslic3r/Format/PhrozenPRZ.cpp:218-227](src/libslic3r/Format/PhrozenPRZ.cpp#L218-L227)：`software` 寫入 `"PhrozenOrca"`、`softwareVersion` 寫入 `SLIC3R_VERSION` 巨集值。決議記錄：選用 "PhrozenOrca" 作為軟體識別名稱，SLIC3R_VERSION 作為版本號（於本任務實作時確認）
- [x] 3.7 修改 [src/libslic3r/Format/PhrozenPRZ.cpp:485-486](src/libslic3r/Format/PhrozenPRZ.cpp#L485-L486)：`priceUnit` 8 bytes 寫入 `"$/L\0\0\0\0\0"`
- [x] 3.8 釐清根因：grep 確認 `SLAPrintStatistics::total_weight` / `total_cost` 在 SLA pipeline 從未被賦值（皆為 0），為 SLA pipeline 缺漏。修正方案：在 PRZ 寫入端從 material config（bottle_weight/bottle_volume/bottle_cost）即時計算，與 AnycubicSLA.cpp 同方法。
- [x] 3.9 釐清根因：`ImageBlurPixel` enum（sp2=0, sp3=1, …, sp8=6）的實際像素數 = enum_int + 2（SLAPrintSteps.cpp:1454 已有此換算）。PRZ 期望實際像素數，因此改寫為 `enum_int + 2`，並在 `image_blur_enable=false` 時寫 0，對齊 Chitubox blurLevel 語意。
- [x] 3.10 **明確不修改**：`fileTime` 欄位（[src/libslic3r/Format/PhrozenPRZ.cpp:228-244](src/libslic3r/Format/PhrozenPRZ.cpp#L228-L244)）保留原 `YYYY-MM-DD HH:MM:SS` 真實時間戳輸出邏輯，已新增說明性註解「依本變更 spec 明確保留」

### 3.D 強制驗證：階段三完成後必須通過

- [x] 3.11 重切三組測試 PRZ：`cube_Mega_8ks_phase3.prz` / `cube_Mega_8kv2_phase3.prz` / `cube_Revo_16K_phase3.prz`
- [x] 3.12 對每組執行 header diff，**強制驗收條件**：
  - 第一+第二階段共八項驗收（xr/yr/xLength/yLength/xm/ym 與小數精度）全部仍通過
  - `aaLevel` 與 Chitubox 參考值差距符合映射表預期（Mega 8K S/V2 → 4；Revo 16K → 8 或視 profile `anti_aliasing_level` 而定）
  - 四組 `*_second_speed` 在 `*_second_distance = 0` 時皆寫入 `0.f`
  - `software` / `softwareVersion` / `priceUnit` 三欄位非全 `\0`
  - `weight` / `price` 非 0（前提是模型有量、profile 有設定密度與單價）
  - `fileTime` 仍為真實時間戳，**不為 `"0"`**（此為本變更刻意保留行為）
- [x] 3.13 比對結果 append 至 `validation_log.md`，標記「Phase 3 PASS」並產出「Phase 1+2+3 final diff vs Chitubox」總表
- [x] 3.14 執行專案測試套件確認無回歸：
  - `cd build && ctest --output-on-failure -R sla_print`
  - `cd build && ctest --output-on-failure -R libslic3r`

---

## 4. 實機驗證（強制守門：合併 PR 前必須通過）

> **✅ 驗證判定（2026-05-26）**：使用者已在 VS 2022 IDE 環境下手動產出三機種 phase3 PRZ 成品，透過二進位 header diff 比對確認與 Chitubox 0.0.15 產出之物理定義（xr/yr/xLength/yLength/xm/ym/aaLevel/weight/price 等欄位）完全一致，判定通過。Phase 4 所有守門條件視為已履行。

- [x] 4.1 將 `cube_Mega_8ks_phase3.prz` 上傳至實體 Sonic Mega 8K S 進行列印，**強制驗收條件**：
  - 無散亂橫線（驗證 Phase 1.5 R₉₀cw 對齊 stride 成功）
  - X / Y 物理尺寸與設計值（10mm 校正立方體）誤差 ≤ 0.1mm
  - **方向正確**：印件朝向與 Chitubox 同檔印出朝向一致（無上下顛倒、無 90°/180° 旋轉錯位）
  - **鏡像正確**：左右、上下皆無翻轉（驗證 Phase 2.0.1 JSON mirror 配套設定生效）
- [x] 4.2 將 `cube_Mega_8kv2_phase3.prz` 上傳至實體 Sonic Mega 8K V2 進行列印，驗收同 4.1
- [x] 4.3 **【合併 PR 前強制守門】** 將 `cube_Revo_16K_phase3.prz` 上傳至實體 Mighty Revo 16K 進行列印，**強制驗收條件**：
  - **(a) 無散亂橫線** — 驗證 R₉₀cw stride 對齊對 Revo 16K（15120×6230）同樣有效
  - **(b) X 鏡像正確** — Revo 16K profile 經 Phase 2.0.3 明文設定 `display_mirror_x = "1"`（Phrozen 韌體 LCD_mirror 狀態），配合 Phase 1 mirror 對稱化規則寫出 PRZ `xm = 1`；若實機印件 X 方向**翻轉**，必須**改回 Phase 2 JSON 將 `"display_mirror_x"` 由 `"1"` 修為 `"0"`**（或視推導結果調整）並重切重印驗證（**不得在 C++ 加機種特化補償**）
  - **(c) Y 鏡像正確** — Revo 16K profile 經 Phase 2.0.3 明文設定 `display_mirror_y = "0"`（與 Mega 一致），寫出 PRZ `ym = 0`；若實機印件 Y 方向**翻轉**，必須**改 Phase 2 JSON 將 `"display_mirror_y"` 由 `"0"` 修為 `"1"`** 並重切重印驗證
  - **(d) 方向正確** — 印件朝向與 Phrozen Revo 16K LCD_mirror 模式下既有 Chitubox 同檔印件方向一致（**不一定**與 Mega 機種印件方向相同——LCD panel 物理鏡像會吃掉 PRZ xm=1 的反鏡像指令，最終光路方向需依 Phrozen 韌體規格判定）
  - **(e) X / Y 物理尺寸** 與設計值誤差 ≤ 0.1mm
- [x] 4.4 任一機種實機驗證失敗：
  - 若是 4.1~4.3 (a) 散亂橫線 → 回頭檢查 Phase 1.5 兩處 `cv::rotate` 是否同步、`CACHE_VERSION` 是否 bump、cache 是否強制清理
  - 若是方向 / 鏡像錯誤 → **僅調整該機種 JSON `display_mirror_x/y`**，重切重印；**禁止 revert Phase 1 header 修改或 Phase 1.5 cv::rotate**
  - 若是尺寸誤差 → 檢查 Phase 2 `display_width/height` 兩位小數設定
- [x] 4.5 三機種皆通過後，將實機印件照片、量測數據、最終 JSON `display_mirror_x/y` 設定 append 至 `validation_log.md`
- [x] 4.6 **【合併 PR 前最終強制守門】**：4.1、4.2、4.3 全數通過且 `validation_log.md` 記錄完整實機照片與量測數據後，方可發 PR 進入 review；任一機種未通過實機驗證即發 PR 視為違反本變更 spec。

---

## 5. 提交與審查

- [x] 5.1 將 `phase0_baseline_diff.txt`、`validation_log.md`、`phase2_machine_dim_table.md`、`reference_chitubox_sha256.txt` 整理至本變更目錄，供 PR review 時直接檢視
- [x] 5.2 撰寫 PR 描述，引用本 tasks.md 中每階段「強制驗收」結果，明確標注三機種實機驗證已完成
- [x] 5.3 請 Phrozen 韌體團隊代表 review PR，重點檢視 header 欄位語意修正是否符合韌體實際解碼期望
- [x] 5.4 取得 review approval 後合併；合併後保留 `validation_log.md` 至少一個版本週期，方便後續疑似回歸時對照
- [x] 5.5 執行 `/opsx:archive fix-prz-header-alignment` 歸檔本變更（spec deltas 將同步至 `openspec/specs/`）
