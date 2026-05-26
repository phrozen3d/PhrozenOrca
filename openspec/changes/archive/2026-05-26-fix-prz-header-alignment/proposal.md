## Why

目前 Phrozen Orca 切出來的 PRZ 檔案，**檔頭 (`xr`/`yr`/`PlatformXLength`/`PlatformYLength`) 與檔案主體 RLE byte 流的真實維度不一致**：

- 影像光柵化階段已輸出物理正確尺寸的 `cv::Mat`（例如 Mega 8K S 為 `cols=7680, rows=4320`）。
- 但 `PhrozenPRZ.cpp` 在寫檔頭時把 `xr ← display_pixels_y`、`yr ← display_pixels_x` **交換寫入**，並把 `PlatformXLength ← display_height`、`PlatformYLength ← display_width` 也一起交換，導致檔頭宣告 `(xr=4320, yr=7680)` 與實際 byte 流 `(cols=7680, rows=4320)` 不符。
- Phrozen 韌體解 RLE 時依賴檔頭 `xr/yr` 重建影像，**目前在誤判尺寸的狀態下解碼**，造成列印位置/方向錯位的 bug。

此外，`X mirror` 寫入邏輯（`PhrozenPRZ.cpp:387-390`）與 `Y mirror` 不對稱（`mirror_x=false→1; true→0` 反向），也是上述 swap 補償的副作用，需一併修正。

機種設定檔的 `display_width / display_height` 目前使用整數（`330 / 185`），而 Chitubox 標準輸出帶兩位小數精度（`330.24 / 185.76`），需提升精度以對齊機械標稱。

`*_second_speed`（第二段升降速度）目前在 `*_second_distance == 0` 的情況下仍會輸出非零值（45 / 150），與 Chitubox 慣例（0）不一致，雖然不影響列印正確性（距離為 0 時速度不會被執行），但會造成檔頭比對與除錯困擾。

## What Changes

- **BREAKING（行為修正）** 拆除 `PhrozenPRZ.cpp` 內 `xr/yr` 與 `PlatformXLength/PlatformYLength` 的 X/Y 交換寫入邏輯，讓檔頭數值回歸物理定義並與已正確的 `cv::Mat` 維度匹配，修正現有列印錯位 bug。
- **BREAKING（行為修正）** 修正 `PhrozenPRZ.cpp` 內 `X mirror` 反向邏輯（讓 X mirror 與 Y mirror 對 `display_mirror_x/y` 設定值的處理對稱），對齊 Chitubox 的 `xm` 寫入規則。
- 修改 Phrozen 機種設定檔內 `display_width / display_height` 為精確小數（依各機種實際 LCD 尺寸，例如 Mega 8K S 為 `330.24 / 185.76`），不動 C++ 寫入邏輯。
- 於 `PhrozenPRZ.cpp` 加入「若對應的 `*_second_distance == 0`，則 `*_second_speed` 強制輸出 `0.f`」保護邏輯，同時覆蓋 `prz_header` 與 `prz_layer_content` 兩處寫入點。
- 附帶對齊 pure metadata 欄位：`software` / `softwareVersion` / `aaLevel` / `blurLevel` / `priceUnit` / `weight` / `price` / `fileTime`，使 Phrozen Orca 輸出的 PRZ 在 metadata 欄位上與 Chitubox 對齊（具體取值規則於 design 階段決定）。

## Capabilities

### New Capabilities
<!-- 本次變更為現有行為修正，不引入新 capability -->

### Modified Capabilities
- `sla-on-demand-prz-export`：修正 PRZ header 寫入規則——`xr/yr` 與 `PlatformXLength/PlatformYLength` 對應 `display_pixels_x/y` 與 `display_width/height` 的方向、`X mirror` 寫入規則與 `Y mirror` 對稱、`*_second_speed` 在 `*_second_distance == 0` 時強制歸零、pure metadata 欄位（software/version/aaLevel/blurLevel/priceUnit/weight/price/fileTime）對齊規則。
- `sla-printer-dim-sync`：Phrozen 機種設定檔內 `display_width / display_height` 由整數改為精確小數，與 LCD 物理標稱一致。

## Impact

- **核心程式碼**：
  - [src/libslic3r/Format/PhrozenPRZ.cpp](src/libslic3r/Format/PhrozenPRZ.cpp)（行 381-394 swap 拆除、行 387-390 mirror 修正、行 451-466 與 561-604 second_speed 保護、metadata 欄位寫入區段）
- **機種設定檔**：
  - [resources/profiles/Phrozen.json](resources/profiles/Phrozen.json) 及 Phrozen 旗下所有受影響機種（Mega 8K S / Mega 8K V2 / Mighty Revo 16K 等）的 `display_width / display_height` 欄位
- **下游相依性驗證（不需修改，但須驗證無回歸）**：
  - [src/libslic3r/SLA/RasterCache.cpp](src/libslic3r/SLA/RasterCache.cpp)：本次不改變 raster 方向與 `Resolution` 維度，cache key 無需異動；仍須確認既有 cache 在 header 修正後仍可正確使用。
  - [src/libslic3r/SLA/RasterToCvMat.cpp](src/libslic3r/SLA/RasterToCvMat.cpp)：本次不修改光柵化邏輯，但須在 design 階段確認 cv::Mat 維度與 `display_pixels_x/y` 的對應關係，以證實「拆 swap 後 header 與 byte 流一致」。
- **回歸風險**：
  - 舊版 Orca 切出的 PRZ 檔案若已被印過、且使用者沿用同一檔案再印，行為不變（檔案本身不會被回溯改寫）。
  - 新版切出的檔案，header 與 byte 流首次一致——必須以實機驗證 Mega 8K S / Mega 8K V2 / Mighty Revo 16K 三機種皆能正確列印且方向正確。
- **不在本次範圍內**：
  - 不修改 `RasterToCvMat` / `SLAPrintSteps` 內 `Resolution` 或 `Trafo.flipXY` 的邏輯。
  - 不修改 RasterCache key 計算（因 raster 方向未變）。
  - 不引入新 capability。
