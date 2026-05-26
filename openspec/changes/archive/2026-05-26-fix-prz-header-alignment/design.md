## Context

`PhrozenPRZ.cpp` 的 `prz_header()` 自最初實作以來，存在四組相互糾結的歷史性「補償邏輯」，導致輸出檔案與 Phrozen 韌體期望解碼格式不一致：

1. **XY 軸 swap**：`xr` 取自 `display_pixels_y`、`yr` 取自 `display_pixels_x`（[PhrozenPRZ.cpp:381-386](src/libslic3r/Format/PhrozenPRZ.cpp#L381-L386)）；`PlatformXLength` 取自 `display_height`、`PlatformYLength` 取自 `display_width`（[PhrozenPRZ.cpp:391-394](src/libslic3r/Format/PhrozenPRZ.cpp#L391-L394)）。
2. **X mirror 反向寫入**：[PhrozenPRZ.cpp:387-390](src/libslic3r/Format/PhrozenPRZ.cpp#L387-L390) `mirror_x=false→1, true→0` 與 Y mirror 的 `false→0, true→1` 不對稱。
3. **AA Level 直接整數寫入**：[PhrozenPRZ.cpp:269-274](src/libslic3r/Format/PhrozenPRZ.cpp#L269-L274) 直接將 `cfg_i(cfg, "anti_aliasing_level")` 寫入 header，未做 PRZ 期望的「採樣等級對應到 AA pixel 數」轉換。
4. **`*_second_speed` 在 distance 為 0 時仍輸出非零**：[PhrozenPRZ.cpp:451-466](src/libslic3r/Format/PhrozenPRZ.cpp#L451-L466) 與 [PhrozenPRZ.cpp:561-604](src/libslic3r/Format/PhrozenPRZ.cpp#L561-L604) 直接讀 cfg 值寫入，未考量 distance=0 時 speed 應為 0 的隱含契約。

實機驗證結果顯示，光柵化階段（[RasterToCvMat.cpp:182-183](src/libslic3r/SLA/RasterToCvMat.cpp#L182-L183)）輸出的 `cv::Mat` 維度已是物理正確（`cols = display_pixels_x`、`rows = display_pixels_y`），swap 才是錯誤端。一旦 swap 拆除，header 將與 byte 流自然對齊。

機種設定檔（[resources/profiles/](resources/profiles/) 下 Phrozen JSON）目前以整數記錄 `display_width / display_height`，與 Chitubox 標準的兩位小數精度（`330.24 / 185.76`）存在量化誤差。

**stakeholders**：Phrozen Orca 韌體團隊（解碼端契約）、Phrozen 切片器使用者（列印正確性）、QA（三機種實機驗證）。

## Goals / Non-Goals

**Goals:**

- 修正 PRZ 檔頭 `xr / yr / PlatformXLength / PlatformYLength` 與 byte 流維度不一致導致的列印錯位 bug。
- 對齊 PRZ 檔頭 metadata 欄位與 Chitubox 預設輸出，使兩切片器產出的檔頭在「韌體解碼參數」與「pure metadata」兩層皆一致或語意相容。
- 提供「`*_second_distance == 0` 時 `*_second_speed` 強制輸出 `0.f`」的 C++ 寫入端防護，使 PRZ 檔頭內部自洽。
- 提升機種設定檔內 `display_width / display_height` 至兩位小數精度，與 LCD 機械標稱對齊。
- AA Level 寫入端依 PRZ 標準完成「設定值 → header 值」的對應轉換。

**Non-Goals:**

- **不修改 `fileTime` 寫入邏輯**——保留 [PhrozenPRZ.cpp:228-244](src/libslic3r/Format/PhrozenPRZ.cpp#L228-L244) 現有 `YYYY-MM-DD HH:MM:SS` 真實時間戳輸出，不對齊 Chitubox 的 `"0"`。Orca 的時間戳對使用者除錯與檔案歸檔有價值，且韌體不解析此欄位。
- 不修改 `RasterToCvMat.cpp` / `SLAPrintSteps.cpp` 內 `Resolution` 或 `Trafo.flipXY` 邏輯——光柵化方向已是物理正確。
- 不修改 [RasterCache.cpp](src/libslic3r/SLA/RasterCache.cpp) 的 cache key 計算——raster 方向未變，現有 cache 仍可繼續使用。
- 不引入新 capability。
- 不調整 `printer_settings_id / printer_model / sla_print_settings_id` 等字串欄位的格式（profile 設定差異，不在本次範圍）。

## Decisions

### 決策 1：拆除 `xr / yr` swap

**選擇**：在 [PhrozenPRZ.cpp:381-386](src/libslic3r/Format/PhrozenPRZ.cpp#L381-L386) 將 `xr` 改讀 `pcfg.display_pixels_x`、`yr` 改讀 `pcfg.display_pixels_y`。

```cpp
// 修正後
short xr = static_cast<short>(pcfg.display_pixels_x.getInt());
short yr = static_cast<short>(pcfg.display_pixels_y.getInt());
write_be(fh, xr);
write_be(fh, yr);
```

**理由**：光柵化輸出的 `cv::Mat` 已是 `cols = display_pixels_x`、`rows = display_pixels_y`，PRZ 主體 byte 流以 row-major 線性掃描 [PhrozenPRZ.cpp:800-812](src/libslic3r/Format/PhrozenPRZ.cpp#L800-L812)，韌體解 RLE 時依賴 `xr`（每列像素數）= cv::Mat.cols 才能正確切列。拆 swap 後 header 與 byte 流首次一致。

**Alternatives considered**：在 raster 階段 transpose 或調整 `Trafo.flipXY` 翻轉影像方向以維持現有 header swap——已否決（影響 raster cache、preview、profile mirror 預設值等多處，爆炸半徑遠大於 header 修正）。

### 決策 2：拆除 `PlatformXLength / PlatformYLength` swap

**選擇**：在 [PhrozenPRZ.cpp:391-394](src/libslic3r/Format/PhrozenPRZ.cpp#L391-L394) 將 `PlatformXLength` 改讀 `pcfg.display_width`、`PlatformYLength` 改讀 `pcfg.display_height`。

```cpp
// 修正後
{ float v = static_cast<float>(pcfg.display_width.getFloat());  write_be(fh, v); ... }
{ float v = static_cast<float>(pcfg.display_height.getFloat()); write_be(fh, v); ... }
```

**理由**：與決策 1 配對；物理上 `display_width` 對應 `display_pixels_x` 方向（長邊），不應交換。

### 決策 3：X mirror 寫入邏輯對稱化

**選擇**：在 [PhrozenPRZ.cpp:387-390](src/libslic3r/Format/PhrozenPRZ.cpp#L387-L390) 將 X mirror 寫入語意改為與 Y mirror 對稱。

```cpp
// 修正前
{ fh += static_cast<char>(pcfg.display_mirror_x.getBool() ? 0 : 1); }  // false→1, true→0  ← 反向
{ fh += static_cast<char>(pcfg.display_mirror_y.getBool() ? 1 : 0); }  // false→0, true→1

// 修正後
{ fh += static_cast<char>(pcfg.display_mirror_x.getBool() ? 1 : 0); }  // false→0, true→1
{ fh += static_cast<char>(pcfg.display_mirror_y.getBool() ? 1 : 0); }  // false→0, true→1
```

**理由**：
1. `display_mirror_x = true` 表示「需要鏡像」，PRZ 寫 `1` 才是符合直覺的「mirror flag = on」語意；現有反向是 swap 補償的耦合副作用。
2. Mighty Revo 16K 既有 profile 設定 `display_mirror_x = true`，修正後 PRZ 將輸出 `xm = 1`，與 Chitubox 輸出一致。
3. 此修正需與決策 1/2 同步上線——三項合一才能讓 X 軸 (resolution / length / mirror) 三欄全部與 Chitubox 對齊；單獨上線任一項會導致中間態行為更混亂。

**驗證重點**：Mighty Revo 16K 必須以實機測試，確認 X 方向不會因「swap 拆 + mirror 改」雙修而出現翻轉。

> ⚠ **Phase 1.5 補述（Phase 1 落地後實機回饋導入）**：決策 1/2/3 的「光柵化已物理正確」前提經事後驗證**不成立**——[SLAPrintSteps.cpp:1421](src/libslic3r/SLAPrintSteps.cpp#L1421) 在 portrait 模式有意 swap pw/ph，導致 cv::Mat 維度為 `(rows=display_pixels_x, cols=display_pixels_y)`，而非原 design 所假設的相反值。Phase 1 header 修改本身保留（與 Chitubox 對齊），但**必須配套加入 RLE 編碼前的 cv::rotate** 對齊 byte stride。詳見**決策 1.5**。

### 決策 1.5：以 `cv::rotate(ROTATE_90_CLOCKWISE)` 對齊 RLE byte stream 與 Phase 1 header

**背景**：Phase 1 落地後實機列印出現「散亂橫線」。經 [SLAPrintSteps.cpp:1411-1423](src/libslic3r/SLAPrintSteps.cpp#L1411-L1423) 與 [RasterToCvMat.cpp:182-189](src/libslic3r/SLA/RasterToCvMat.cpp#L182-L189) 程式碼追蹤，root cause 為：

- Portrait 模式 SLAPrintSteps:1421 強制 `std::swap(pw, ph)` → res.width_px = display_pixels_y, res.height_px = display_pixels_x
- 隨後產出的 cv::Mat 為 `(rows = display_pixels_x = 7680, cols = display_pixels_y = 4320)`（Mega 8K S）
- RLE row-major 掃描 → 每列實際 4320 像素
- Phase 1 header 宣告 xr=7680 → 韌體解 RLE 用錯 stride → 散亂橫線

**選擇**：在「光柵化完、RLE 編碼前」對 cv::Mat 執行 `cv::rotate(src, dst, cv::ROTATE_90_CLOCKWISE)`（語意等價 `transpose ∘ horizontal_flip`），把 portrait 矩陣轉成 landscape，使 byte stream 每列像素數 = display_pixels_x，與 Phase 1 header xr 一致。

**插入點（兩處，必須同步）**：

1. [src/libslic3r/SLAPrintSteps.cpp:1552-1557](src/libslic3r/SLAPrintSteps.cpp#L1552-L1557) — main RLE 編碼路徑（寫入 RasterCache 前）
2. [src/libslic3r/Format/PhrozenPRZ.cpp:735](src/libslic3r/Format/PhrozenPRZ.cpp#L735) — cache-miss fallback 路徑

並同步 [RasterCache::CACHE_VERSION](src/libslic3r/SLA/RasterCache.hpp#L62) 由 3 bump 為 4，強制清舊 cache（避免 cache-hit 重用舊 byte order）。

**為何是 90° CW（而非 CCW 或單純 transpose）—— 幾何推導**：

對 Phase 2 後 Mega 8K S（profile `display_mirror_x = false, display_mirror_y = false` → Trafo.mirror_x = true, Trafo.mirror_y = true, flipXY = true），依 [RasterToCvMat.cpp:30-38](src/libslic3r/SLA/RasterToCvMat.cpp#L30-L38) 公式，世界點 (X, Y) 對應 portrait 像素：

```
col = W_P - 1 - Y·sy   (W_P = display_pixels_y = 4320)
row = H_P - 1 - X·sx   (H_P = display_pixels_x = 7680)
```

Chitubox 標準 landscape（top-left origin, no extra mirror, 等價 xm=0 ym=0）：

```
col_C = X·sx
row_C = H_L - 1 - Y·sy = 4320 - 1 - Y·sy
```

驗證 R₉₀cw 變換 `(r, c) → (c, H_P - 1 - r)` 在兩個世界樣本點皆精準對齊：

| 世界點 | Orca portrait 像素 | R₉₀cw 後 | Chitubox 期望 | 對齊 |
|--------|---------------------|----------|---------------|------|
| (10, 20) | (7446, 3852) | (3852, 233) | (3852, 233) | ✓ |
| (300, 180) | (698, 116) | (116, 6981) | (116, 6981) | ✓ |

R₉₀ccw（`T ∘ My`）或單純 T 在兩軸無法同時對齊（推導見 [plan §2](C:\Users\kwshi\.claude\plans\fix-prz-header-alignment-phase-1-header-shimmying-fiddle.md)）。

**Mirror 經 transpose 的軸交換性質**：

```
T ∘ Mx = My ∘ T   （前置 X 鏡像 ≡ 後置 Y 鏡像）
T ∘ My = Mx ∘ T   （前置 Y 鏡像 ≡ 後置 X 鏡像）
```

這條規則解釋了**為何 Phase 2 須把 Phrozen profile 雙鏡像都設 false**——透過 Trafo 的 portrait 反轉，渲染時兩鏡像皆作用；R₉₀cw 後雙鏡像會彼此抵消（外加 R₉₀cw 自帶的水平 flip 抵消 Y 鏡像殘留），最終影像等價於「無額外鏡像的標準 landscape」，與 Chitubox xm=0 ym=0 對齊。

---

#### Phase 2 後 mirror 預設值與 R₉₀cw 結合後的最終預期行為（機種對照表）

> 本子節為配合使用者最新提供的 Phrozen 機種韌體幾何規格而新增，作為 [§決策 1.5](#決策-15-以-cv-rotate-rotate-90-clockwise-對齊-rle-byte-stream-與-phase-1-header) 的**機種展開**：明確列出 Mega 與 Revo 在 Phase 2 加入「明文 JSON mirror 預設值」後，透過 `Trafo`、portrait 渲染、`cv::rotate(ROTATE_90_CLOCKWISE)`、PRZ header `xm/ym`、最後到 Phrozen 韌體解碼／LCD 顯示的完整資料流預期結果。
>
> Phrozen 韌體側的鏡像 UI 規劃（`LCD_mirror / DLP_normal / Normal`）三選一目前以 `LCD_mirror / DLP_mirror` 暫存呈現，後續會由 UI / 設定團隊另案修正，**不在本變更範圍**；但 PRZ header 對應的 `xm / ym` 輸出值在本變更必須明確、可驗證、由 JSON 顯式驅動。

**機種預設狀態與 PRZ 期望輸出**：

| 機種 | Phrozen 韌體狀態 | Profile `display_mirror_x` | Profile `display_mirror_y` | PRZ `xm` | PRZ `ym` |
|---|---|---|---|---|---|
| Sonic Mega 8K S | Normal | `false` | `false` | 0 | 0 |
| Sonic Mega 8K V2 | Normal | `false` | `false` | 0 | 0 |
| Mighty Revo 16K | LCD_mirror | `true` | `false` | 1 | 0 |
| （未來）DLP_normal 機種 | DLP_normal | `false` | `false` | 0 | 0 |

> `xm / ym` 直接取自 profile `display_mirror_x / y`（[§決策 3](#決策-3-x-mirror-寫入邏輯對稱化) 對稱化規則）。

**從世界座標 (X, Y) 到 LCD 顯示像素的完整資料流推導**：

引入符號：
- `sx = SCALING_FACTOR / pxdim.w_mm`, `sy = SCALING_FACTOR / pxdim.h_mm`（像素縮放係數）
- `W_P = display_pixels_y`, `H_P = display_pixels_x`（portrait cv::Mat 維度，因 [SLAPrintSteps.cpp:1421](src/libslic3r/SLAPrintSteps.cpp#L1421) swap）
- `W_L = display_pixels_x`, `H_L = display_pixels_y`（landscape cv::Mat 維度，R₉₀cw 後）
- 略去 `cx / cy` 中心偏移（與導向結論無關）

Trafo 建構規則（[RasterBase.hpp:74-79](src/libslic3r/SLA/RasterBase.hpp#L74-L79)）：

```
Trafo.mirror_x = (orient == Portrait) ? !profile.mirror_x : profile.mirror_x
Trafo.mirror_y = !profile.mirror_y
Trafo.flipXY  = (orient == Portrait)
```

Portrait cv::Mat 像素公式（[RasterToCvMat.cpp:30-57](src/libslic3r/SLA/RasterToCvMat.cpp#L30-L57)，flipXY=true）：

```
col_P = Y·sy                  ;  若 Trafo.mirror_x：col_P = W_P − col_P
row_P = X·sx                  ;  若 Trafo.mirror_y：row_P = H_P − row_P
```

`cv::rotate(ROTATE_90_CLOCKWISE)`：`(r, c) → (c, H_P − 1 − r)`，landscape (R, C) = (col_P, H_P − 1 − row_P)。

**Mega（Normal、profile mx=false / my=false）**：

| 步驟 | 結果 |
|---|---|
| Trafo | mirror_x=true, mirror_y=true, flipXY=true |
| Portrait 像素 | col_P = W_P − Y·sy ; row_P = H_P − X·sx |
| R₉₀cw 後 landscape | **C = X·sx ; R = H_L − Y·sy** |
| PRZ header | xm=0, ym=0 |
| 韌體解碼 → LCD 顯示 | 直接顯示 file bytes，無額外鏡像 |
| 最終 LCD 影像 | **C_d = X·sx ; R_d = H_L − Y·sy**（標準 top-left origin、X 向右、Y 向上） |
| 與 Chitubox 同機種輸出 | **完全等價**（byte stream 對齊、xm/ym 對齊） |

**Mighty Revo 16K（LCD_mirror、profile mx=true / my=false）**：

| 步驟 | 結果 |
|---|---|
| Trafo | mirror_x=false, mirror_y=true, flipXY=true |
| Portrait 像素 | col_P = Y·sy（無 X 鏡像）; row_P = H_P − X·sx |
| R₉₀cw 後 landscape | **C = X·sx ; R = Y·sy**（注意 R 未被 Y-flip） |
| PRZ header | xm=1, ym=0 |
| 韌體解碼 → LCD 顯示 | 對 file bytes 做 X 反鏡像（un-mirror）；Y 不動 |
| 最終 LCD 影像 | **C_d = (W_L − 1) − X·sx ; R_d = Y·sy** |
| Phrozen 韌體期望 | 與 Phrozen Revo 16K 韌體之 `LCD_mirror` 硬體鏡像狀態相容（**Phase 4.3 實機驗證為最終仲裁**） |

**為什麼 Revo 與 Mega 的最終 LCD 影像方向不同仍是「正確」**：

Revo 16K 的 LCD panel 在硬體層級為 LCD_mirror 安裝狀態——`xm=1` 是給韌體的「請反鏡像 X 軸」指令，配合 panel 物理鏡像後，**實際照射到樹脂的光路方向**會與 Mega 一致。換言之：

```
Mega:  cv::Mat ──(direct display)──→ resin   （panel 為 Normal）
Revo:  cv::Mat ──(firmware un-X-mirror)──→ panel ──(LCD physical X-mirror)──→ resin
```

`cv::rotate(ROTATE_90_CLOCKWISE)` 與 Phase 2 的明文 JSON mirror 預設值之所以「對 Mega 達到 Chitubox xm=0/ym=0 對齊、對 Revo 達到 Chitubox xm=1/ym=0 對齊」，是因為兩者共用同一條 R₉₀cw 變換規則，**差異完全由 profile mirror 預設值驅動，C++ 不含任何機種特化分支**。任何「Revo print 方向錯誤」的回饋必須透過調整 Revo profile JSON 的 `display_mirror_x / y` 解決，不得在 [PhrozenPRZ.cpp](src/libslic3r/Format/PhrozenPRZ.cpp) 或 [SLAPrintSteps.cpp](src/libslic3r/SLAPrintSteps.cpp) 加入 `if printer == "Revo"` 補償（見 [§Risks](#risks--trade-offs) 與 [tasks §4.3](tasks.md)）。

**Phase 4.3 強制守門條件回顧**：

| 機種 | 預期 LCD 物理印件方向 | 失敗時的修正方向 |
|---|---|---|
| Mega 8K S | 與 Chitubox 同檔印件方向一致（無翻轉、無 90°/180° 旋轉） | 重新檢查 Phase 2.0.1 是否確實寫入 `display_mirror_x="0"`, `display_mirror_y="0"` |
| Mega 8K V2 | 同上 | 重新檢查 Phase 2.0.2 |
| Mighty Revo 16K | LCD_mirror 模式下與 Phrozen 同款裸機既有「Chitubox + LCD_mirror panel」印件方向一致 | 重新檢查 Phase 2.0.3 是否確實寫入 `display_mirror_x="1"`, `display_mirror_y="0"`；若仍錯誤，再依方向細節微調該機種 JSON（**禁止 revert R₉₀cw 或加 C++ 分支**） |

**Alternatives considered**：

- **(a) 拆 [SLAPrintSteps.cpp:1421](src/libslic3r/SLAPrintSteps.cpp#L1421) 的 portrait swap**：須連動修 [RasterBase.hpp Trafo 建構](src/libslic3r/SLA/RasterBase.hpp#L74-L79)、[RasterToCvMat.cpp 內 7 處 flipXY 使用點](src/libslic3r/SLA/RasterToCvMat.cpp)、[AGGRaster.hpp:102](src/libslic3r/SLA/AGGRaster.hpp#L102)、[RasterToPolygons.cpp:79-82](src/libslic3r/SLA/RasterToPolygons.cpp#L79-L82)；並使所有 SLA 格式（SL1/SL1S/Anycubic 等）的 RasterCache 失效。爆炸半徑遠大於 PRZ header 本身——**否決**。
- **(b) RLE encoder 改 column-major 掃描**：每像素 cache miss，~+15-20 ms/層（比 cv::rotate 慢 2 倍）且需重寫 `flush_run` 邏輯——**否決**。
- **(c) 撤銷 Phase 1 header 修改**（回到 xr=display_pixels_y）：最小變動，但放棄與 Chitubox header 對齊的目標——**已被顧問否決**，本變更採對齊路線。

**效能影響**：cv::rotate 內部為 SIMD + cache-tiled 實作，~8-10 ms/層（Mega 8K S 7680×4320）。TLS 新增 `mat_rotated` buffer ~33 MB/thread，8 thread 加總 +264 MB peak（從 ~520 MB 提升至 ~780 MB，仍在合理範圍）。

### 決策 2.5：採用 display_mirror_mode 映射 PRZ xm（取代 display_mirror_x 直讀）

**背景**：Phase 2 完成後，三款 Phrozen 機種 JSON 同時存在兩個鏡像欄位：

- `display_mirror_x` / `display_mirror_y`：由 `SLAPrinterSettingsDialog::apply_mirror_mode()` 寫入，語意為「portrait 模式下 Trafo 建構所需的補償值」，**已包含 portrait 方向的 X 反轉**。以 Mega（Normal）為例：portrait 下 `final_mirror_x = !false = true` → JSON 寫入 `"1"`；這個 `"1"` 是 Trafo 建構用的，**不代表韌體側 xm=1**。
- `display_mirror_mode`：commit `54e59cc86` 新增的語意欄位，直接反映韌體 UI 三選一狀態（`normal / lcd_mirror / dlp_normal`）；語意清晰、無方向補償耦合。

若 Phase 1 task 1.3 維持「讀 `display_mirror_x.getBool() ? 1 : 0`」，則：

| 機種 | `display_mirror_x` | PRZ xm（現行） | 韌體期望 xm | 正確？ |
|---|---|---|---|---|
| Sonic Mega 8K S | `"1"`（portrait 補償） | 1 | 0 | ✗ |
| Sonic Mega 8K V2 | `"1"`（portrait 補償） | 1 | 0 | ✗ |
| Mighty Revo 16K | `"0"`（portrait 補償） | 0 | 1 | ✗ |

**選擇**：在 [PhrozenPRZ.cpp:387-390](src/libslic3r/Format/PhrozenPRZ.cpp#L387-L390) 將 xm 寫入改為從 `display_mirror_mode` 取值，不再讀 `display_mirror_x`：

```cpp
// 決策 2.5 映射表
// display_mirror_mode: normal      → xm = 0  （Mega Normal 韌體狀態）
// display_mirror_mode: lcd_mirror  → xm = 1  （Revo 16K LCD_mirror 韌體狀態）
// display_mirror_mode: dlp_normal  → xm = 0  （DLP 類機種）
// fallback（欄位缺失，舊 preset）  → display_mirror_x.getBool() ? 1 : 0

int xm_val = 0;
auto mode = pcfg.display_mirror_mode.value;
if (mode == slammLCDMirror) {
    xm_val = 1;
} else if (mode == slammNormal || mode == slammDLPNormal) {
    xm_val = 0;
} else {
    // fallback: 舊版 preset 無 display_mirror_mode 欄位
    xm_val = pcfg.display_mirror_x.getBool() ? 1 : 0;
}
write_be(fh, static_cast<char>(xm_val));
```

修正後：

| 機種 | `display_mirror_mode` | PRZ xm（修正後） | 韌體期望 xm | 正確？ |
|---|---|---|---|---|
| Sonic Mega 8K S | `"normal"` | 0 | 0 | ✓ |
| Sonic Mega 8K V2 | `"normal"` | 0 | 0 | ✓ |
| Mighty Revo 16K | `"lcd_mirror"` | 1 | 1 | ✓ |

**理由**：
1. `display_mirror_mode` 直接表達韌體 LCD 面板的硬體鏡像狀態，語意明確、無方向補償耦合，是比 `display_mirror_x` 更合適的 PRZ xm 資料來源。
2. `display_mirror_x` 的「portrait 補償反轉」語意是 GUI Trafo 建構的內部細節，不應洩漏至 PRZ 寫入端——讓 PRZ 寫入端直接依賴 GUI 內部補償邏輯是架構污染。
3. fallback 機制確保未包含 `display_mirror_mode` 的舊版 preset 不會靜默輸出錯誤的 xm 值（雖然舊 preset 的 display_mirror_x 語意反轉問題仍存在，但影響範圍限於歷史檔，不影響現有三款主力機種）。

**Alternatives considered**：
- (a) 調整 JSON，使 `display_mirror_x` 恢復「非 portrait 補償」語意（即 Mega 填 `"0"`、Revo 填 `"1"`）——拒絕，這會破壞 `apply_mirror_mode()` 的正確運作，導致 Trafo 建構錯誤，進而影響渲染方向。
- (b) 在 PhrozenPRZ.cpp 加入 portrait 反轉邏輯（讀 `display_orientation` 再決定是否 flip）——拒絕，這是把 GUI 特化補償邏輯搬到格式寫入端，日後更難維護。

### 決策 4：AA Level 採樣等級映射

**選擇**：在 [PhrozenPRZ.cpp:269-274](src/libslic3r/Format/PhrozenPRZ.cpp#L269-L274) 將 `anti_aliasing_level` 從「直接整數寫入」改為「採樣等級索引 → PRZ AA 級數」對應表。

**映射表**：

| Orca `anti_aliasing_level` 設定值 | PRZ `aaLevel` 寫入值 | 語意 |
|---|---|---|
| 0 | 2 | Low（每軸 2 樣本） |
| 1 | 4 | Mid（每軸 4 樣本，預設） |
| 2 | 8 | High（每軸 8 樣本） |
| 其他 (含 ≥3 或負值) | 4 | 安全 fallback 回 Mid，避免韌體吃到未知值 |

**實作骨架**：

```cpp
// AA level mapping: anti_aliasing_level (sampling index) -> PRZ aaLevel (pixels per axis)
{
    int aa_idx = cfg_i(cfg, "anti_aliasing_level");
    short aa_level;
    switch (aa_idx) {
        case 0:  aa_level = 2; break;
        case 1:  aa_level = 4; break;
        case 2:  aa_level = 8; break;
        default: aa_level = 4; break;  // safe fallback
    }
    write_be(fh, aa_level);
    layerContent_position_offset += 2;
}
```

**理由**：
1. Chitubox 三份對照檔（Mega 8K S / V2 / Revo 16K）的 `aaLevel` 值分別為 4 / 4 / 8，皆為 2 的次方數，與「每軸採樣像素數」語意一致。
2. Orca 內部 `anti_aliasing_level` 採「設定索引」表示（UI dropdown 的 0/1/2 三檔位），與 PRZ 期望的「實際 AA 倍率」是不同維度，必須轉換。
3. fallback 設為 4（Mid）而非 0，因為 PRZ 韌體可能以 `aaLevel = 0` 為「停用 AA」或為非法值——保守起見回安全預設值。

**Alternatives considered**：
- (a) `aa_level = 1 << (aa_idx + 1)` 公式化——拒絕，未列舉的索引（例如 4）會產生 32 這種未定義的 PRZ 值，較表查法更安全。
- (b) 同步修改 `PrintConfig.cpp` 將 `anti_aliasing_level` 預設值由 4 改為 1——拒絕，會影響其他切片器（非 Phrozen）匯出邏輯與 UI 顯示。本次僅在 PRZ 寫入端做映射。

### 決策 5：`*_second_speed` distance=0 防護

**選擇**：在 [PhrozenPRZ.cpp:451-466](src/libslic3r/Format/PhrozenPRZ.cpp#L451-L466)（header）與 [PhrozenPRZ.cpp:561-604](src/libslic3r/Format/PhrozenPRZ.cpp#L561-L604)（layer content）所有 `*_second_speed` 寫入處加入 distance=0 守衛：

```cpp
// 範本（適用四組：bottom_lift / lift / bottom_retract / retract）
{
    float dist  = cfg_f(cfg, "bottom_lift_second_distance");
    float speed = cfg_f(cfg, "bottom_lift_second_speed");
    if (dist == 0.f) speed = 0.f;
    write_be(fh, speed);
}
```

**理由**：當第二段距離為 0 時，無實際升降動作，speed 欄位應與 distance 自洽（Chitubox 慣例亦如此），避免韌體側除錯時的混淆。

**涵蓋範圍**（四組對稱欄位）：
- `bottom_lift_second_distance` ↔ `bottom_lift_second_speed`
- `lift_second_distance` ↔ `lift_second_speed`
- `bottom_retract_second_distance` ↔ `bottom_retract_second_speed`
- `retract_second_distance` ↔ `retract_second_speed`

**Alternatives considered**：
- (a) 改 machine JSON 預設值——拒絕，會限制使用者在 UI 上手動填非 0 distance 時的彈性。
- (b) 容差 `< 1e-6` 而非 `== 0.f`——拒絕，浮點等於 0 的判斷在「未填寫 / 顯式歸零」情境足夠安全，過度寬鬆反而可能誤殺實際小值。

### 決策 6：機種 JSON `display_width / display_height` 改為兩位小數

**選擇**：修改 [resources/profiles/](resources/profiles/) 下受影響 Phrozen 機種 JSON（Sonic Mega 8K S、Sonic Mega 8K V2、Mighty Revo 16K 等），將 `display_width / display_height` 由整數改為精確小數，符合 LCD 機械標稱。

**具體值**（依 LCD 規格）：
- Sonic Mega 8K S / V2：`display_width = 330.24`、`display_height = 185.76`
- Mighty Revo 16K：`display_width = 211.68`、`display_height = 118.37`
- 其餘 Phrozen 機種：依各自 LCD 機械規格逐一確認（於 tasks 階段列出清單）。

**理由**：純 metadata 提升精度，零風險；C++ 寫入端已是 float 路徑，不需配套修改。

### 決策 7：Pure metadata 欄位對齊（僅列範圍，細節值於 tasks 決定）

**選擇**：對齊以下純 metadata 欄位的寫入值；具體取值來源於 tasks.md 階段確認後實作：
- `software`：暫定 Orca 版本字串（由 `SLIC3R_VERSION` 或 `OrcaSlicer_VERSION` 巨集取得）。
- `softwareVersion`：與 `software` 相同或主版本號。
- `priceUnit`：寫入 `"$/L"`。
- `weight` / `price`：確認 [PhrozenPRZ.cpp:476-484](src/libslic3r/Format/PhrozenPRZ.cpp#L476-L484) `SLAPrintStatistics` 為何輸出 0，並修正資料來源。
- `image_blur_pixel`：當前直接寫入 enum int 值（[PhrozenPRZ.cpp:288-292](src/libslic3r/Format/PhrozenPRZ.cpp#L288-L292)），需確認 Chitubox 的 `blurLevel = 2` 對應的 enum 索引並調整。

**不在此次範圍**：`fileTime`（保留現有時間戳輸出）。

## Risks / Trade-offs

- **[風險] Mighty Revo 16K 雙修（swap 拆 + X mirror 對稱）後實機方向錯誤** → 在 tasks 階段強制要求三機種逐一實機驗證後才能合併 PR；任一機種驗證失敗則回退整組 X 軸修正並重新檢視 machine profile 的 `display_mirror_x` 設定。
- **[風險] 舊版 Orca 切出的 PRZ 檔案目前能不能正確列印的真相未明** → 不論真相為何，本次修正後**新切的檔案**將與 byte 流自洽；舊檔案不會被回溯改寫，使用者重切後即取得正確輸出。
- **[風險 — Phase 1.5] cv::rotate 兩處插入點不同步** → 若忘記改 [PhrozenPRZ.cpp:735](src/libslic3r/Format/PhrozenPRZ.cpp#L735) 的 cache-miss 路徑，cache 失效時走 fallback 會吐出未旋轉的 PRZ → 散亂橫線回歸。**緩解**：強制驗證任務要求刪除 `%TEMP%/phrozen_sla_cache/` 走 cache-miss 路徑切一次、再走 cache-hit 切一次，兩份 PRZ 必須 byte-for-byte 完全相同。
- **[風險 — Phase 1.5] Chitubox row 起點假設未驗證** → 本決策假設 Chitubox 用 top-left 像素起點（標準 PNG/cv::Mat 慣例）。若 Chitubox 實際用 bottom-left，R₉₀cw 結果會與 Chitubox 上下顛倒。**緩解**：實機列印 cube 觀察方向，若上下倒則一鍵改 `cv::ROTATE_90_COUNTERCLOCKWISE`，無須改動其他邏輯。
- **[風險 — Phase 1.5] Mighty Revo 16K 的 R₉₀cw 後鏡像方向需獨立驗證** → Mega 與 Revo 的 profile mirror 默認值可能不同，且 Chitubox Revo 參考檔的 xm/ym 值未確認；R₉₀cw 對兩機種是否皆能達成 Chitubox 對齊，必須由實機列印驗證。**緩解**：合併 PR 前強制執行 Revo X/Y 鏡像實機驗證任務（[tasks §4.3](tasks.md)），方向錯誤須調整該機種 JSON `display_mirror_x/y`，**不在 C++ 內加硬編碼 if 分支**。
- **[風險 — Phase 1.5] TLS mat_rotated 增 264 MB peak 記憶體** → 從 ~520 MB 升至 ~780 MB；4 GB 記憶體 / 32-bit 切片器若 OOM，可降 `RASTERIZE_CONCURRENCY` 由 8 至 4 緩解。
- **[風險] AA Level 映射表無法涵蓋 Orca UI 未來新增的等級選項** → fallback 至 Mid（4）作為防呆；後續若 UI 新增「Ultra (16x)」需同步更新映射表，並寫測試。
- **[風險] `display_width / display_height` 改小數後，下游依賴整數值的其他寫檔路徑（SL1 / SL1S / 其他切片器格式）可能受影響** → tasks 階段列出 grep 點，逐一檢查 `display_width.getFloat()` / `getInt()` 的所有呼叫，確認無整數截斷導致的精度回歸。
- **[風險] `weight` / `price` 輸出 0 的根因若是 `SLAPrintStatistics` 未填值，非 PRZ 寫入問題** → 本次僅修「PRZ 寫入端如何取值」，若上游統計未填，提案修正範圍可能擴及 SLA print pipeline；於 tasks 階段先用 grep + 實機切片驗證根因再決定。
- **[Trade-off] 不一併修 `fileTime` 對齊 Chitubox 的 `"0"`** → 接受。Orca 真實時間戳對使用者價值高於「外觀完全一致」。

## Migration Plan

1. **第一階段：PRZ header 純函式修正（已落地）**
   - 拆 `xr / yr` swap、拆 `XLength / YLength` swap、X mirror 對稱化。
   - 落地後實機驗證出現「散亂橫線」→ 觸發 **Phase 1.5**。

2. **第 1.5 階段：數據旋轉補償（新增，本變更核心修正）**
   - 在 [SLAPrintSteps.cpp:1552-1557](src/libslic3r/SLAPrintSteps.cpp#L1552-L1557) main RLE 編碼路徑與 [PhrozenPRZ.cpp:735](src/libslic3r/Format/PhrozenPRZ.cpp#L735) cache-miss fallback 路徑，雙處插入 `cv::rotate(_, _, cv::ROTATE_90_CLOCKWISE)`。
   - TLSData 擴 `cv::Mat mat_rotated` 重用 buffer 避免 per-layer malloc。
   - [RasterCache::CACHE_VERSION](src/libslic3r/SLA/RasterCache.hpp#L62) 3 → 4 強制清舊 cache。
   - **強制驗證**：刪 `%TEMP%/phrozen_sla_cache/` 走 cache-miss 切一次、再 cache-hit 切一次，兩份 PRZ byte-for-byte 必須完全相同。

3. **第二階段：機種 JSON 精度 + Mirror 對齊**
   - `display_width / display_height` 改兩位小數（決策 6）。
   - **Mega 機種 profile 加 `display_mirror_x: false, display_mirror_y: false`** 明文設定（配套決策 1.5）。
   - Revo 16K 暫不動 mirror，待 Phase 4 實機驗證後決定。

4. **第三階段：AA Level / second_speed / metadata 對齊**（決策 4/5/7）
   - 不需實機驗證（不影響韌體解碼）。

5. **第四階段：三機種實機驗證 — 合併 PR 前強制守門**
   - 切 Mega 8K S 校正立方體 → 印 → 量測 X/Y 尺寸、確認方向與鏡像皆正確。
   - 切 Mega 8K V2 → 印 → 同上。
   - **切 Mighty Revo 16K → 印 → 重點驗證 X/Y 方向是否因 R₉₀cw 旋轉而與 Chitubox 一致；若不一致，調整 JSON `display_mirror_x/y`，不在 C++ 加硬編碼。**

**Rollback strategy**：所有 C++ 修正集中於 [PhrozenPRZ.cpp](src/libslic3r/Format/PhrozenPRZ.cpp) 與 [SLAPrintSteps.cpp 第 1525-1560 區段](src/libslic3r/SLAPrintSteps.cpp#L1525-L1560)，可分塊 revert；機種 JSON 修改可逐機種 revert。實機驗證任一階段失敗即回滾該機種的相關修正並重新檢視 profile。

## Open Questions

- `software` / `softwareVersion` 字串應使用何種版本表達？候選：
  - (i) Orca 主版本（如 `"2.3.1"`，從 `SLIC3R_VERSION` 取得）
  - (ii) 固定字串 `"PhrozenOrca"` + 版本號
  - (iii) 維持空字串（不對齊 Chitubox 的 `"0.0.15"`，因該值看來不是有意義的版本標記）
  → 需在 tasks 階段與 Phrozen 韌體團隊或產品端確認後定案。
- `weight` / `price` 為何輸出 0：是 `SLAPrintStatistics` 未填，還是 PRZ 寫入路徑取值錯誤？需於 tasks 階段以 grep 與斷點驗證根因，再決定修正點位於 `PhrozenPRZ.cpp` 還是 `SLAPrint.cpp`。
- `image_blur_pixel` enum 值與 Chitubox `blurLevel = 2` 的對應關係：是否需與 AA Level 類似建立映射表？需於 tasks 階段查 `ImageBlurPixel` enum 定義後決定。
- Phrozen 旗下其他機種（非本次三款）的 `display_width / display_height` 精確小數值來源：是否有官方規格表可一次性更新所有機種？
