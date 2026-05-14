## Why

Phrozen Orca Resin 的 SLA 列印設定對話框（`SLAPrinterSettingsDialog`）在使用者修改平台 X/Y 尺寸後，僅將值寫回 `printable_area`，未同步寫入 `display_width`/`display_height`——而 PRZ 匯出器直接讀取後者作為 `PlatformXLength`/`PlatformYLength`，導致 PRZ 檔案的平台尺寸永遠固定在機器 JSON profile 的原始值（例如 Mighty Revo 16K 恆為 211×118），完全忽略使用者的修改。此外，整個 UI↔PRZ 參數映射機制從未被系統性地審查，潛在的類似斷層與未開放的 PRZ 欄位尚待清點。

## What Changes

- **修復 `sync_local_to_tab()` 寫回遺漏**：在 `SLAPrinterSettingsDialog::sync_local_to_tab()` 中補上 `display_width` 與 `display_height` 的 `set_key_value` 呼叫，使其與 `printable_area` 保持同步。
- **新增 UI↔PRZ 完整參數映射文件**：建立一份全面的映射健康度審查，列出所有 UI 已開放且 PRZ 也有對應的欄位、其對應鍵名與資料型別，並標注任何已知的斷層或風險。
- **新增 PRZ 未覆蓋參數清單**：整理出 PRZ 格式支援但 UI 目前尚未開放設定的欄位，並附上未來擴充至 UI 的優先級建議。

## Capabilities

### New Capabilities

- `sla-printer-dim-sync`：確保 `SLAPrinterSettingsDialog` 的 Size X/Y 欄位在寫回時同時更新 `printable_area`、`display_width`、`display_height` 三個 config key，消除 UI 與 PRZ 匯出器之間的參數同步斷層。
- `prz-ui-parameter-mapping`：系統性盤點所有 UI 開放欄位與 PRZ header/layer 欄位之間的對應關係，包含健康度審查（對應正確、對應有風險、無對應）以及未來擴充建議。

### Modified Capabilities

（無：現有 `sla-on-demand-prz-export` 規格的匯出流程與格式結構不變，僅修正寫入前的 config 值正確性。）

## Impact

**直接修改**
- `src/slic3r/GUI/SLAPrinterSettingsDialog.cpp`：`sync_local_to_tab()` 函數（新增兩行 `set_key_value`）

**間接受益（不需修改）**
- `src/libslic3r/Format/PhrozenPRZ.cpp`：`prz_header()` 讀取 `display_width`/`display_height` 的邏輯不變，修復後能拿到正確值
- `src/libslic3r/SLAPrintSteps.cpp`：rasterization 的中心點計算（lines 1398-1399）同樣讀取 `display_width`/`display_height`，修復後與使用者設定的 `printable_area` 一致
- `src/libslic3r/Format/SL1.cpp`：SL1 匯出格式同樣讀取 `display_width`/`display_height`，受益相同

**無破壞性影響**
- 所有既有 JSON profile 的 `display_width`/`display_height` 已與 `printable_area` 一致，修復不影響現有機器的預設行為
- Config 繼承機制不受影響：child preset 覆蓋 parent 的行為不變
- 精度無損：`ConfigOptionFloat` 與 `ConfigOptionPoints`（`Vec2d`）均為 64-bit double，寫回無 downcast
