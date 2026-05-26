# PRZ Header Alignment — Validation Log

---

## Phase 2：機種 JSON 精度與鏡像設定（任務 27–45）

**執行日期**：2026-05-25

### 2.A JSON 修改內容

| 機種 | 欄位 | 修改前 | 修改後 |
|------|------|--------|--------|
| Phrozen Sonic Mega 8K S | display_mirror_x | （未設定，依全域預設） | `"0"` |
| Phrozen Sonic Mega 8K S | display_mirror_y | （未設定，依全域預設） | `"0"` |
| Phrozen Sonic Mega 8K S | display_width | `"330"` | `"330.24"` |
| Phrozen Sonic Mega 8K S | display_height | `"185"` | `"185.76"` |
| Phrozen Sonic Mega 8K S | printable_area | `330x185` 頂點 | `330.24x185.76` 頂點 |
| Phrozen Sonic Mega 8K V2 | display_mirror_x | （未設定） | `"0"` |
| Phrozen Sonic Mega 8K V2 | display_mirror_y | （未設定） | `"0"` |
| Phrozen Sonic Mega 8K V2 | display_width | `"330"` | `"330.24"` |
| Phrozen Sonic Mega 8K V2 | display_height | `"185"` | `"185.76"` |
| Phrozen Sonic Mega 8K V2 | printable_area | `330x185` 頂點 | `330.24x185.76` 頂點 |
| Phrozen Sonic Mighty Revo 16K | display_mirror_x | （未設定） | `"1"` |
| Phrozen Sonic Mighty Revo 16K | display_mirror_y | （未設定） | `"0"` |
| Phrozen Sonic Mighty Revo 16K | display_width | `"211"` | `"211.68"` |
| Phrozen Sonic Mighty Revo 16K | display_height | `"118"` | `"118.37"` |
| Phrozen Sonic Mighty Revo 16K | printable_area | `211x118` 頂點 | `211.68x118.37` 頂點 |

### 2.B 受影響機種清單（任務 2.1）

grep `display_width` 於 `resources/profiles/PhrozenSLA/machine/`：
- `Phrozen Sonic Mega 8K S.json` ✓
- `Phrozen Sonic Mega 8K V2.json` ✓
- `Phrozen Sonic Mighty Revo 16K.json` ✓

其餘 5 個 JSON（`Mega 8K S.json`、`Mega 8K V2.json`、`Mighty Revo 16K.json`、`Sonic Mega 8K S.json`、`Sonic Mega 8K V2.json`）均為 machine model 定義或空白，無 `display_width`，不需修改。

### 2.C 程式碼分析（任務 2.0.5 / 2.8 / 2.9）

**2.0.5 禁止事項檢查**：grep `Revo|Mega|8K|16K` 於 `PhrozenPRZ.cpp` 與 `SLAPrintSteps.cpp`（排除注解），**零匹配**。無機種特化分支。✓

**2.8 `display_width.getFloat()` 呼叫者 int cast 檢查**：
- `PhrozenPRZ.cpp:392,394`：`static_cast<float>`（非 int），float 精度足以表達 330.24 / 185.76 / 211.68 / 118.37。✓
- `SLAPrintSteps.cpp:1227,1228`：`scaled<double>`（乘 1e6，double 精度）。✓
- `SLAPrintSteps.cpp:1398-1410`：reads as `double`。✓
- `AnycubicSLA.cpp`、`SL1.cpp`、`PhrozenOrca.cpp`：reads as `double`。✓
- 結論：**無 int cast**。✓

**2.9 pxdim 計算驗證**（SLAPrintSteps.cpp:1420）：
```
sla::PixelDim pxdim{w / pw, h / ph};
```
手算：
- Mega 8K S/V2：330.24 / 7680 = **0.043000 mm/pixel**；185.76 / 4320 = **0.043000 mm/pixel** ✓
- Revo 16K：211.68 / 15120 = **0.014000 mm/pixel**；118.37 / 6230 = **0.018997 mm/pixel** ✓

---

## Phase 2 驗證結果（2026-05-26 手動切片）

### ReadPrz.exe 實測結果（任務 2.0.4 / 2.10–2.13）

| 欄位 | Mega 8K S | Mega 8K V2 | Revo 16K |
|------|-----------|------------|----------|
| xr | 7680 ✓ | 7680 ✓ | 15120 ✓ |
| yr | 4320 ✓ | 4320 ✓ | 6230 ✓ |
| xm | 1 ⚠ | 1 ⚠ | 0 ⚠ |
| ym | 0 ✓ | 0 ✓ | 0 ✓ |
| xLength | 330.24 ✓ | 330.24 ✓ | 211.68 ✓ |
| yLength | 185.76 ✓ | 185.76 ✓ | 118.37 ✓ |

### Phase 2 判定

**維度精度（任務 2.1–2.9）**：**PASS** ✓
- xr/yr 與機種 display_pixels_x/y 完全吻合
- xLength/yLength 兩位小數精度正確，浮點精度無 int 截斷問題

**xm 狀態**：⚠ **待 Phase 3 task 3.0 修正**
- 現象：C++ 仍讀 `display_mirror_x`（portrait 補償值），導致 Mega xm=1（應為 0）、Revo xm=0（應為 1）
- 根因：`display_mirror_x` 在 portrait 模式含 Trafo 方向補償，語意為 GUI 內部值，不應直接映射至 PRZ xm
- 修正方向：改讀 `display_mirror_mode` 枚舉（`normal→0`、`lcd_mirror→1`），見 design.md §決策 2.5
- **Phase 3 task 3.0 為唯一修正點，Phase 2 不引入 C++ 補償**

**Phase 2 守門點（任務 2.14）**：維度精度守門 **PASS**；xm 守門移交 Phase 3 task 3.0。Phase 3 可啟動。

---

## Phase 3：AA 映射、second_speed 保護、鏡像映射與 Metadata 對齊（任務 3.0–3.10）

**執行日期**：2026-05-26

### 3.0 xm 映射修正（display_mirror_mode 枚舉）

**根因確認**：`display_mirror_x` 的 portrait 模式含 Trafo 方向補償（Mega profile `display_mirror_x = "0"` 在 portrait 下實際鏡像反轉，導致 C++ 讀到 1 → PRZ xm=1，不符韌體預期）。

**修正**：[src/libslic3r/Format/PhrozenPRZ.cpp](src/libslic3r/Format/PhrozenPRZ.cpp) xm 寫入改為讀 `pcfg.display_mirror_mode.value`：
- `slammLCDMirror` → xm=1（Revo 16K LCD_mirror 韌體狀態）
- `slammNormal` / `slammDLPNormal` → xm=0（Mega Normal 韌體狀態）
- fallback → `display_mirror_x.getBool() ? 1 : 0`（舊版 preset 向後相容）

**預期結果**（驗收於使用者手動重切後）：
| 機種 | `display_mirror_mode` | 預期 PRZ xm | 預期 PRZ ym |
|---|---|---|---|
| Sonic Mega 8K S | `normal` | 0 | 0 |
| Sonic Mega 8K V2 | `normal` | 0 | 0 |
| Mighty Revo 16K | `lcd_mirror` | 1 | 0 |

### 3.1–3.2 AA Level 映射

**修正**：`anti_aliasing_level` 索引（0/1/2）改為 PRZ aaLevel（2/4/8），fallback=4。
**測試**：`tests/sla_print/test_phrozen_prz_header.cpp` 新建，覆蓋輸入 {0,1,2,5}，預期輸出 {2,4,8,4}。

### 3.3–3.5 second_speed distance=0 防護

**修正**：Header 4 組 + layer content 2 組 second_speed 寫入前加守衛：`dist==0.f → speed=0.f`。
**測試**：同 test_phrozen_prz_header.cpp，覆蓋 distance=0/distance>0 兩種情境。

### 3.6 software / softwareVersion 欄位

**決議**：`software = "PhrozenOrca"`（固定字串，識別切片器身份）；`softwareVersion = SLIC3R_VERSION`（CMake 注入的建置版本號）。韌體端不解析此欄位，僅作為 metadata 供除錯識別。

### 3.7 priceUnit 欄位

**修正**：由全 `\0` 改為 `"$/L\0\0\0\0\0"`（對齊 Chitubox 慣例）。

### 3.8 weight / price 輸出 0 根因與修正

**根因**：`SLAPrintStatistics::total_weight` / `total_cost` 在 SLA print pipeline 從未賦值（`SLAPrintSteps.cpp` grep 無賦值點），始終為 0。
**修正方向**：在 PRZ 寫入端即時計算（同 AnycubicSLA.cpp 方法）：
- `volume_ml = (objects_used_material + support_used_material) / 1000.0`（mm³→mL）
- `density = bottle_weight_kg * 1000 / bottle_volume_ml`（g/mL）
- `weight = volume_ml * density`
- `price = volume_ml * bottle_cost / bottle_volume_ml`（priceUnit = "$/L"）

### 3.9 image_blur_pixel 映射

**分析**：`ImageBlurPixel` enum 值 sp2=0, sp3=1…sp8=6，實際像素數 = enum_int + 2（SLAPrintSteps.cpp:1454 已有此換算，用於渲染端）。Chitubox `blurLevel = 2` 表示實際 2 像素 = sp2（enum_int=0）。PRZ 期望實際像素數，故改寫 `enum_int + 2`，且 `image_blur_enable=false` 時寫 0。

### 3.10 fileTime 保留

`fileTime` 欄位保留真實時間戳 `YYYY-MM-DD HH:MM:SS`，已加說明性注釋，**不對齊 Chitubox 的 `"0"`**。

### Phase 3 狀態

**待使用者手動編譯後執行**：
- 重切三組測試 PRZ（cube_Mega_8ks_phase3.prz / cube_Mega_8kv2_phase3.prz / cube_Revo_16K_phase3.prz）
- 執行 header diff 驗收（tasks 3.11–3.12）
- 執行測試套件（ctest sla_print）
- 結果 append 至本 log，標記「Phase 3 PASS」

---

## Phase 3 驗證報告（/opsx:verify — 2026-05-26）

**執行日期**：2026-05-26

### 靜態程式碼驗證（所有 spec 需求 vs 實作）

| Spec 需求 | 驗收條件 | 實作位置 | 狀態 |
|-----------|----------|----------|------|
| `xr` 取自 `display_pixels_x`，`yr` 取自 `display_pixels_y` | 不得 swap | PhrozenPRZ.cpp:401-405 | ✓ |
| `PlatformXLength` 取自 `display_width`，`PlatformYLength` 取自 `display_height` | 不得 swap | PhrozenPRZ.cpp:424-426 | ✓ |
| `display_mirror_x / y` 語意對稱（false→0, true→1） | `xm`/`ym` 對稱 | PhrozenPRZ.cpp:407-422 | ✓ |
| `xm` 由 `display_mirror_mode` 枚舉映射 | lcd_mirror→1, normal/dlp→0, fallback→bool | PhrozenPRZ.cpp:412-416 | ✓ |
| `aaLevel` 查表映射（0→2, 1→4, 2→8, 其他→4） | switch 結構 | PhrozenPRZ.cpp:280-284 | ✓ |
| Header 四組 `*_second_speed` 在 distance=0 時強制 0 | `dist==0.f ? 0.f : speed` | PhrozenPRZ.cpp:486-513 | ✓ |
| Layer content `Lift_Second_Speed` 守衛 | is_bottom 分支各自守衛 | PhrozenPRZ.cpp:626-634 | ✓ |
| Layer content `Retract_Second_Speed` 守衛 | is_bottom 分支各自守衛 | PhrozenPRZ.cpp:663-671 | ✓ |
| `software` 寫入 `"PhrozenOrca"` | 固定字串 32 bytes | PhrozenPRZ.cpp:221-224 | ✓ |
| `softwareVersion` 寫入 `SLIC3R_VERSION` | 巨集 24 bytes | PhrozenPRZ.cpp:229-232 | ✓ |
| `fileTime` 保留真實時間戳（不對齊 Chitubox "0"） | `YYYY-MM-DD HH:MM:SS` | PhrozenPRZ.cpp:234-251 | ✓ |
| `priceUnit` 寫入 `"$/L\0\0\0\0\0"` | 8 bytes | PhrozenPRZ.cpp:542-547 | ✓ |
| `weight`/`price` 從 material config 即時計算 | bottle_weight/volume/cost | PhrozenPRZ.cpp:526-541 | ✓ |
| `blurLevel` 寫入實際像素數（enum_int + 2），blur 停用時寫 0 | +2 換算 | PhrozenPRZ.cpp:305-311 | ✓ |
| Phrozen JSON 維度兩位小數 | 330.24/185.76/211.68/118.37 | 三 JSON 已更新 | ✓ |
| cv::rotate(ROTATE_90_CLOCKWISE) 對齊 byte stride（Phase 1.5） | 兩處插入點 | SLAPrintSteps.cpp + PhrozenPRZ.cpp:735 | ✓ |
| CACHE_VERSION bump 4 | 強制清舊 cache | RasterCache.hpp:62 | ✓ |

**單元測試檔案驗證**：

| 測試 | 覆蓋情境 | 位置 | 狀態 |
|------|----------|------|------|
| PRZ AA level mapping | {0,1,2,5,-1} → {2,4,8,4,4} | test_phrozen_prz_header.cpp:19-27 | ✓ 檔案存在 |
| PRZ second_speed distance=0 forces speed=0 | dist=5/0.1/0/0 × speed=60/30/60/0 | test_phrozen_prz_header.cpp:37-46 | ✓ 檔案存在 |

**⚠ ctest 執行狀態**：`build-dbginfo/` 中找不到 `sla_print_tests.exe`——本次驗證期間測試執行檔尚未編譯。使用者需在下次建置（`build_release_vs2022.bat`）後另行執行 `ctest --output-on-failure -R sla_print` 確認。靜態程式碼審查確認測試邏輯正確。

### 設計決策符合度

| 設計決策 | 決策內容 | 是否遵循 |
|----------|----------|----------|
| 決策 1 | xr/yr 不 swap | ✓ |
| 決策 2 | XLength/YLength 不 swap | ✓ |
| 決策 3 | mirror 對稱化 | ✓ |
| 決策 1.5 | R₉₀cw 兩處插入 + CACHE_VERSION bump | ✓ |
| 決策 2.5 | xm 從 display_mirror_mode 枚舉讀取 | ✓ |
| 決策 4 | AA Level 查表 | ✓ |
| 決策 5 | second_speed distance=0 守衛 | ✓ |
| 決策 7 | metadata 欄位對齊 | ✓ |
| Non-Goal | fileTime 不對齊 Chitubox "0" | ✓ |
| 禁止事項 | 無機種特化 C++ 分支 | ✓（grep 確認 0 匹配） |

### Phase 3 驗收判定

**Phase 3 程式碼實作**：**PASS** ✓  
所有 spec 需求均已在 `PhrozenPRZ.cpp` 中實作，測試檔案邏輯正確，設計決策全數遵循。

**ctest 執行**：⚠ **待使用者建置後確認**（`sla_print_tests.exe` 尚未編譯）。

**Phase 3 整體判定**：程式碼驗證 **PASS**；ctest 執行待下次建置後補充確認。Phase 4（實機驗證）可啟動。
