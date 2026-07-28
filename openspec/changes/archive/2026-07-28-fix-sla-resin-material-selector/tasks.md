## 1. 共用的材料查找輔助函式

- [x] 1.1 新增一個輔助函式：輸入某個 `sla_prints` preset 的 `alias`，找出對應的 `sla_materials` preset——先拿 alias 原樣去比對 `sla_materials` 的 preset 名稱，找不到再加上 `"Phrozen "` 前綴重試（比照 `Preset::set_visible_from_appconfig()`，`Preset.cpp` 約第 680-682 行既有的 fuzzy match 寫法）。放置位置：`Preset.cpp`/`.hpp` 裡的自由函式 `find_sla_material_for_process_alias()`。
- [x] 1.2 當 alias 解析不出材料名稱、或兩種形式都找不到對應的 `sla_materials` preset 時，輔助函式要回傳「沒找到」（不能是崩潰或 assert）。

## 2. SLA Process preset 可見度反推

- [x] 2.1 在 `PresetBundle::load_selections()`（`PresetBundle.cpp`，緊接在既有的 `load_installed_sla_materials(config)` 呼叫之後）新增一個步驟：遍歷 `sla_prints`，用第 1 節的輔助函式對照 `sla_materials` 目前的 `is_visible` 狀態，設定每個 preset 的 `is_visible`。實作上抽成公開方法 `PresetBundle::update_sla_print_visibility_from_materials()`，不是直接寫死在 `load_selections()` 裡（理由見下一項）。
- [x] 2.2 輔助函式解析不出來的 preset（對應第 1.2 項），預設 `is_visible = true`；並記一筆警告 log 標明是哪個 preset 名稱（只在名稱裡有 `@` 卻對不到材料時才記，純無 `@` 的通用 preset 不算異常，不記 log）。**手動測試發現的追加修正**：`load_selections()` 在 `GuideFrame::apply_config()` 流程裡執行得比「重新宣告 wizard 勾選結果」那段還早，所以第一次執行時算出來的可見度是根據還沒修正的舊材料狀態——跟第一輪材料可見度那個 bug是同一個模式。修法比照當時的做法：在 `WebGuideDialog.cpp` 修正完 `sla_materials` 可見度的地方，多呼叫一次 `update_sla_print_visibility_from_materials()`，並補上跟 `load_selections()` 一樣的 fallback 重選邏輯（目前選取的 preset 若因此變不可見，改選 `first_compatible_idx()`）。
- [x] 2.3 **手動測試檢查點：Pass**。wizard 只勾選單一材料，`combo_sla_print`／`combo_sla_material` 都正確只顯示該材料。

## 3. SLA Material 選取同步（已完整實作並測試通過，隨後整個撤回——見下方說明）

> **狀態：已撤回。** 下面 3.1-3.8 記錄的同步機制原本已完整實作，且 3.4、3.5 都經使用者手動測試通過。但事後確認：(1) `sla_materials` 對應的參數其實全部已合併進 `sla_prints` 的 JSON，`sla_materials` 選中誰對切層輸出沒有任何影響；(2) `sla_prints` preset 可以另存成使用者自訂名稱，一旦如此就不符合 `"材料@印表機"` 命名慣例，同步邏輯連材料都解析不出來。因此決定整個撤回：`sla_materials` 的選取永遠停在使用者上次手動選的值，不再嘗試跟 `sla_prints` 同步。詳見 design.md D4、proposal.md「What Changes」。撤回動作：
> - [x] 撤回 `PresetBundle.cpp`：`load_selections()` 裡的同步呼叫、以及 `update_compatible(Always)` 之後的第二次同步都已移除。
> - [x] 撤回 `Plater.cpp`：`Plater::priv::on_select_preset()` 裡的同步呼叫與 `sidebar->update_presets(TYPE_SLA_MATERIAL)` 強制重繪都已移除。
> - [x] 撤回 `PresetComboBoxes.cpp`：`PlaterPresetComboBox::update()` 裡補的同步呼叫已移除。
>
> 以下 3.1-3.8 保留作為「這個方案曾經被完整實作並測試通過」的歷史紀錄，不代表目前程式碼狀態。

- [x] 3.1 ~~在 `PresetBundle::load_selections()`（`PresetBundle.cpp`，`sla_materials.select_preset_by_name_strict(initial_filament_profile_name)` 那一行），改成：當目前印表機技術是 SLA 時，透過第 1 節的輔助函式，從剛選好的 `sla_prints` preset（`get_edited_preset().alias`）反推出 `sla_materials` 的選取，解析不到才 fallback 回原本讀 ini 值的寫法。~~（已撤回）
- [x] 3.2 ~~**實作位置跟原計畫不同，已更新**：手動點選 `combo_sla_print` 實際觸發選取變更的地方不是 `PresetComboBoxes.cpp` 的 `OnSelect`（那邊只更新 `m_last_selected` 等 UI 狀態，`evt.Skip()` 之後事件會冒泡到 Sidebar），真正呼叫 `Tab::select_preset()` 套用選取的地方是 `Plater::priv::on_select_preset()`（`Plater.cpp`）。已在該函式呼叫 `select_preset()` 之後、`preset_type == Preset::TYPE_SLA_PRINT` 時加上同一段同步邏輯。~~（已撤回）
- [x] 3.3 ~~確認同一個目前作用中的 `sla_prints` 選取被多次呼叫同步邏輯時是幂等/不會有副作用的...~~（已撤回，連同整個同步邏輯一起移除）
- [x] 3.6 ~~**手動測試發現的追加修正 A（啟動時不同步）**...~~（已撤回）
- [x] 3.7 ~~**手動測試發現的追加修正 B（手動切換時畫面沒重繪）**...~~（已撤回）
- [x] 3.4 **手動測試檢查點：曾經 Pass，現已撤回**（3.7 修正後使用者當時驗證通過，`combo_sla_print` 切換不同項目時 `combo_sla_material` 畫面顯示正確跟著換；此行為已隨撤回移除，`combo_sla_material` 現在應該固定停在 Default Setting）。
- [x] 3.8 ~~**手動測試發現的追加修正 C（啟動時不同步的真正根因）**...~~（已撤回）
- [x] 3.5 **手動測試檢查點：曾經 Pass，現已撤回**（3.8 修正後使用者當時驗證通過，重開應用程式時 `combo_sla_material` 會同步成 `combo_sla_print` 對應的材料；此行為已隨撤回移除，`combo_sla_material` 現在應該固定停在 Default Setting，不隨啟動時的 `sla_prints` 選取而變）。
- [x] 3.9 **撤回後才發現的既有 bug（跟上面 3.1-3.8 的同步機制無關）**：從 Setup Wizard 按下 Finish、安裝新印表機時，`combo_sla_material` 還是會跳離 Default Setting。根因是 `PresetBundle::load_selections()`（`PresetBundle.cpp` 約第 1770-1782 行）裡既有的一段邏輯——仿照 FFF 的 `default_filament_profile` 寫法，只要有 `preferred_printer`，就會把 `initial_filament_profile_name` 從 `"Default Filament"` 覆寫成印表機宣告的 `default_sla_material_profile`，讓 `sla_materials` 選到一個真實材料；`WebGuideDialog.cpp`（約第 1231-1238 行）「resin wizard 還留著 FFF 印表機」的 fallback 分支也有同一個模式。兩處都已改成保留 `sla_materials` 停在 Default Setting（`sla_prints` 對應的覆寫邏輯維持不動）。**手動測試檢查點：Pass**（使用者確認：Setup Wizard 選印表機、勾選 resin、按下 Finish 回到 Prepare 頁面後，`combo_sla_material` 固定停在 Default Setting）。詳見 design.md D4 附註。

## 4. Fallback 與邊界情境防呆

- [x] 4.1 當目前作用中的 `sla_prints`/`sla_materials` 選取所依賴的樹脂在 wizard 裡被取消勾選時，改選一個可見且相容的 fallback preset。實作上直接用既有的 `PresetCollection::first_compatible_idx()`（本來就同時檢查 `is_compatible && is_visible`，比原計畫參考的 `first_visible_filament_name` 寫法更直接），在 `sla_prints`/`sla_materials` 各自 select 完之後檢查 `is_visible`，不可見就 fallback。
- [x] 4.2 確認結果：`GuideFrame`（`WebGuideDialog.cpp`）本身沒有等效於 `ConfigWizard` 的 `check_and_install_missing_materials()`。但「全部材料取消勾選」這個情境已經由 `LCD_resin.js` 的 `ResponseFilamentResult()` 在前端擋下（`nAll==0` 時呼叫 `ShowNotice(1)` 並 `return false`，根本不會送出 Finish），所以既有防呆已涵蓋這個 spec 要求的情境。至於「勾選的材料在目前印表機下沒有對應 process」這個更窄的資料完整性問題（design.md Risks 裡提到、非正式 spec 需求），決定不額外新增警告 UI：4.1 的 fallback 已確保不會卡死或崩潰（最差情況下 `first_compatible_idx()` 落到 index 0 的 `-- default --` 佔位 preset），範圍上不在本次 spec 要求內，先不做。
- [x] 4.3 **手動測試檢查點：Pass**。取消勾選目前使用中的材料，兩個下拉選單都正確落在有效的 fallback 選取上。

## 5. exposure_time 文件化（proposal 第 3 點，方案 A）

- [x] 5.1 在 `exposure_time` 的材料端選項註冊處（`Preset.cpp`，`s_Preset_sla_material_options`）補上註解，說明它目前一律會被 `sla_prints` 覆蓋。
- [x] 5.2 `full_sla_config()` 覆蓋邏輯處（`PresetBundle.cpp`）既有的註解已經延伸補充，同時引用材料端的選項註冊處，並說明這個 if 判斷實際上是恆真（unconditional）。
- [x] 5.3 不做任何功能性/行為變更——只加了註解，`full_sla_config()` 的覆蓋邏輯（`out.set_key_value("exposure_time", co->clone())` 那行）完全沒被動到。

## 6. 完整手動驗證

- [x] 6.1 **Pass**。重新完整測試最初回報的兩個流程：(a) 選單 → Setup Wizard → resin material 頁面 → 取消勾選一個 → Finish → Prepare 頁面；(b) Prepare 頁面 resin icon → resin material 頁面 → 取消勾選一個 → Finish → Prepare 頁面。使用者實際看到的下拉選單只反映勾選過的材料，先前回報的「跳回第一個選項」不再重現。
- [x] 6.2 **Pass**。確認沒有造成 FDM 模式的回歸：切換到 FFF 印表機，filament 下拉選單行為沒有變化。
- [x] 6.3 **Pass**（重新定義後，原本的內容是「確認診斷用的 Material 列跟 combo_sla_print 一致」，那個前提已經不成立，改為驗證下面這件事）：確認診斷用的 Material 列（此時仍然強制顯示）不論 `combo_sla_print` 切換到哪個 preset，都固定停在 `-- default --`，涵蓋手動切換 `combo_sla_print`、重新啟動應用程式、wizard Finish（含 3.9 修正的情境）。使用者確認測試通過。

**測試過程中額外確認並記錄的既有行為（經討論後判斷不需要修改，見 design.md Risks）**：若使用者曾經自訂並選取過一個不遵守 `"材料@印表機"` 命名慣例的 `sla_prints` preset，之後在 wizard 裡只是改變材料勾選（未曾指定要換掉 Process preset），`combo_sla_print` 可能會在 Finish 之後跳去選中那個自訂 preset，而不是留在原本選著的系統 preset。根因分析：自訂 preset 因為 alias 解析不出材料，可見度反推邏輯 fail-open 而永遠 `is_visible = true`（不受材料勾選狀態影響）；同時 `first_compatible_idx()`（原選取的系統 preset 因材料被取消勾選而變不可見時的 fallback）沒有偏好比對條件，純粹按 preset 名稱字母序挑第一個可見且相容的——兩者疊加，若自訂 preset 名稱剛好排序在前，就會被選中，純屬巧合。這是既有 preset 架構（`first_compatible_idx()` 字母序 fallback、自訂 preset 永遠相容）疊加本次可見度反推機制後產生的行為，使用者確認不需要在本次修正範圍內處理。

## 7. 清理

- [x] 7.1 還原 `Plater.cpp` 的 `TEMP DIAGNOSTIC` 改動：把 `m_hsizer_sla_material_row->Show(!showSLA)` 還原回來（移除強制的 `Show(true)` 和它旁邊的註解）。
- [x] 7.2 移除 `WebGuideDialog.cpp`（`SaveProfile()`、`apply_config()`）裡本次工作階段加入的所有 `DEBUG` 標記 `BOOST_LOG_TRIVIAL(error)` log。
- [x] 7.3 移除 `PresetComboBoxes.cpp`（`PlaterPresetComboBox::update()` 的 `TYPE_SLA_MATERIAL`/`TYPE_SLA_PRINT` 分支、以及 `update_selection()` 之後）裡的 `DEBUG` 標記 log。**額外順手清理**：`PresetBundle.cpp` 的 `update_sla_print_visibility_from_materials()`（D1 可見度反推，保留）裡也有同一輪加入、每次重新載入都會對每個 preset 印一行 `error` 等級 `DEBUG` log 的診斷；已依 design.md D1／tasks 2.2 原本規劃的意圖收斂成只在「preset 名稱看起來像 `"材料@印表機"` 格式但解析不到對應材料」時才記一筆 `warning` 等級的正式警告 log（移除 `DEBUG` 字樣與逐筆印出配對成功的 log）。
- [x] 7.4 **Pass**。清理完成後重新建置，重跑一次第 6 節的完整流程，移除診斷 log 與強制顯示的 Material 列之後行為沒有變化，確認先前的修正不是依賴診斷才能運作。
