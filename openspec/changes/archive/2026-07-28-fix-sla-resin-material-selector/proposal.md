## Why

Prepare 側邊欄的「樹脂/Resin」區塊實際顯示給使用者的是 `combo_sla_print`（SLA Print/Process preset 下拉選單），而不是 `combo_sla_material`（SLA Material preset 下拉選單）——`combo_sla_material` 那一列在 `printer_technology` 為 SLA 時被強制隱藏（`Plater.cpp` 裡的顯示/隱藏互換邏輯，來自 commit `a6fc416f3d`）。因為 PhrozenOrca 把每個 Process preset 用它校正過的那個材料來命名（例如 `"Speed Plus - Black@Phrozen Sonic Mighty Revo 16K"`），這意外地讓它看起來像一個材料選擇器——但實際上只涵蓋了屬於 Process preset 的參數（`exposure_time`、`bottom_exposure_time`、supports、pad）。只屬於 Material 的參數（`initial_exposure_time`、`material_correction_*`、`material_density`、`material_type/vendor/colour`）是由 `sla_materials` 目前選中的 preset 控制，使用者現在完全看不到也改不到它，這些參數就這樣悄悄停在上次選中的那個 preset 上。

另外，Setup Wizard 的 resin material 勾選會寫進 `AppConfig::SECTION_MATERIALS`，也正確地驅動了 `sla_materials` 的 `is_visible`（這個部分在本次工作階段已修好）——但 `sla_prints`（實際顯示在 UI 上的那個 preset 類型）完全沒有「安裝／可見度」這個概念，導致在 wizard 裡取消勾選某個 resin，對使用者實際互動的那個下拉選單毫無作用。

## What Changes

- `sla_prints` preset 的可見度改為從 `sla_materials` 的可見度反推：一個 Process preset 只有在它的名稱／alias 所對應的那顆樹脂已被勾選／安裝時（透過已經正確的 `AppConfig::SECTION_MATERIALS` 狀態）才會顯示，實作上重用 `Preset::alias`（載入時就已經解析好 `"材料@印表機"` 這個命名格式），不再自己寫字串解析。
- 新增一個共用的輔助函式，依 `sla_prints` preset 的 alias 找出對應的 `sla_materials` preset，同時處理兩邊命名慣例裡既有的 `"Phrozen "` 前綴不一致問題（重用 `Preset::set_visible_from_appconfig()` 裡既有的 fuzzy match 寫法）。目前唯一的呼叫點是上面的可見度反推。
- **（已實作並測試通過，隨後撤回）** 原本規劃「選擇某個 Process preset 時自動同步選中對應的 `sla_materials` preset」，讓材料專屬參數跟著連動，並在 3 個 choke point（`PresetBundle::load_selections()`、`Plater::priv::on_select_preset()`、`PresetComboBoxes.cpp` 的 `PlaterPresetComboBox::update()`）都加上了同步邏輯，經使用者手動測試全數通過。但後來確認：(1) 材料可能貢獻的每一個參數其實都已經合併進 `sla_prints` 的 JSON 裡，`sla_materials` 選中誰對切層輸出沒有任何影響；(2) `sla_prints` preset 可以另存成使用者自訂名稱，一旦如此就不再符合 `"材料@印表機"` 命名慣例，同步邏輯連材料都解析不出來。因此決定整個撤回，`sla_materials` 的選取永遠停留在使用者上次手動選的值（實務上等同永遠是 `-- default --`），不再嘗試跟 `sla_prints` 同步。詳見 design.md D4。
- `sla_materials` 的 `exposure_time` 欄位會補上說明用的註解（純程式碼註解，不是 UI 隱藏），標明它目前一律會被 `sla_prints` 的 `exposure_time` 覆蓋——`PresetBundle::full_sla_config()` 裡既有的覆蓋邏輯不做任何功能性變更。
- 開發順序安排：先在**目前**已經加在程式裡的暫時性診斷狀態下實作與驗證（`Plater.cpp` 裡強制顯示的 `combo_sla_material` 那一列，以及本次工作階段加在 `WebGuideDialog.cpp` / `PresetComboBoxes.cpp` 裡 `error` 等級的 `DEBUG` log），讓每一步都能完整手動測試。只有在手動測試確認修正有效之後，才把診斷用的 UI 改動還原、移除暫時 log，作為最後一步收尾。

## Capabilities

### New Capabilities
- `sla-resin-material-selector`：規範 Prepare 側邊欄的 SLA Process（`combo_sla_print`）preset 可見度要依樹脂材料是否已安裝／勾選而定；Material（`combo_sla_material`）preset 的選取則刻意不跟著 Process preset 同步（見上方「已實作並測試通過，隨後撤回」段落）。

### Modified Capabilities
（無——目前沒有既有 spec 涵蓋這個行為）

## Impact

- `src/libslic3r/PresetBundle.cpp` —— 新增公開方法 `update_sla_print_visibility_from_materials()`，供 `load_selections()` 和 `GuideFrame::apply_config()`（`WebGuideDialog.cpp`）共用，依 `sla_materials.is_visible` 反推 `sla_prints` 的可見度。**（已撤回）** 原本 `load_selections()` 裡還有一段依選中的 `sla_prints` preset 同步 `sla_materials` 選取的邏輯，以及尾端 `update_compatible(Always)` 之後的第二次同步——這兩處都已移除，見 design.md D4。
- `src/libslic3r/Preset.cpp` / `.hpp` —— 新增共用輔助函式 `find_sla_material_for_process_alias()`，依 `sla_prints` preset 的 alias 解析出對應的 `sla_materials` preset（含 `"Phrozen "` 前綴的 fuzzy match，`real=true` 避開 `PresetCollection` 的 `m_edited_preset` 替換陷阱）。目前唯一呼叫點是上面的可見度反推。
- **（已撤回）** `src/slic3r/GUI/Plater.cpp` —— `Plater::priv::on_select_preset()` 原本有呼叫材料同步輔助函式並明確呼叫 `sidebar->update_presets(TYPE_SLA_MATERIAL)` 強制重繪，處理使用者手動選取的情境；這段連同強制重繪呼叫已移除，見 design.md D4。
- **（已撤回）** `src/slic3r/GUI/PresetComboBoxes.cpp` —— `PlaterPresetComboBox::update()` 原本在既有的 `ensure_sla_print_not_on_default_placeholder()` 之後補了材料同步；已移除，見 design.md D4。
- `src/slic3r/GUI/Plater.cpp` —— 收尾步驟（尚未執行，等 6.3 手動驗證通過後才做）：把 Material 那一列暫時性強制顯示的診斷改動還原（程式碼裡標有 `TEMP DIAGNOSTIC` 註解可以定位到確切位置）。
- `src/slic3r/GUI/WebGuideDialog.cpp`、`src/slic3r/GUI/PresetComboBoxes.cpp`、`src/libslic3r/PresetBundle.cpp` —— 收尾步驟（尚未執行）：移除本次工作階段加入的暫時性 `error` 等級 `DEBUG` log（正式的可見度邏輯本身保留，只移除診斷用的 log 呼叫）。
- `src/libslic3r/PresetBundle.cpp` —— `load_selections()` 裡既有的一段邏輯（安裝新印表機時，把 `sla_materials` 的初始選取從 `"Default Filament"` 覆寫成印表機宣告的 `default_sla_material_profile`）已移除，改為固定保留 Default Setting；`sla_prints` 對應的覆寫邏輯不受影響。
- `src/slic3r/GUI/WebGuideDialog.cpp` —— 「resin wizard 還留著 FFF 印表機」的 fallback 分支裡同一個模式（強制選取 `default_sla_material_profile`）也已移除。這是撤回同步機制之後才發現的既有 bug，不是本次同步機制本身造成的，已驗證修正：Setup Wizard 選印表機並按 Finish 後，`combo_sla_material` 固定停留在 Default Setting。
- 不會動到任何 FDM-only 的程式碼；所有改動都侷限在 SLA 的 preset 類型（`sla_prints`、`sla_materials`）以及 SLA 專屬的 UI 路徑。
