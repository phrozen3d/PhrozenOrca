## 1. 共用的材料查找輔助函式

- [ ] 1.1 新增一個輔助函式：輸入某個 `sla_prints` preset 的 `alias`，找出對應的 `sla_materials` preset——先拿 alias 原樣去比對 `sla_materials` 的 preset 名稱，找不到再加上 `"Phrozen "` 前綴重試（比照 `Preset::set_visible_from_appconfig()`，`Preset.cpp` 約第 680-682 行既有的 fuzzy match 寫法）。放置位置（`PresetBundle.cpp` 裡的自由函式，或是 `Preset`/`PresetCollection` 的方法）依 design.md 裡尚未決定的 open question 再定。
- [ ] 1.2 當 alias 解析不出材料名稱、或兩種形式都找不到對應的 `sla_materials` preset 時，輔助函式要回傳「沒找到」（不能是崩潰或 assert）。

## 2. SLA Process preset 可見度反推

- [ ] 2.1 在 `PresetBundle::load_selections()`（`PresetBundle.cpp`，緊接在既有的 `load_installed_sla_materials(config)` 呼叫之後）新增一個步驟：遍歷 `sla_prints`，用第 1 節的輔助函式對照 `sla_materials` 目前的 `is_visible` 狀態，設定每個 preset 的 `is_visible`。
- [ ] 2.2 輔助函式解析不出來的 preset（對應第 1.2 項），預設 `is_visible = true`；並記一筆警告 log 標明是哪個 preset 名稱。
- [ ] 2.3 **手動測試檢查點**（維持目前強制顯示 Material 列的診斷狀態）：跑一次 wizard，勾選/取消勾選樹脂材料，完成後確認 `combo_sla_print` 的清單內容跟勾選狀態一致，同時 `combo_sla_material`（診斷列）繼續正確顯示（跟本次工作階段稍早已驗證過的結果一致）。

## 3. SLA Material 選取同步

- [ ] 3.1 在 `PresetBundle::load_selections()`（`PresetBundle.cpp` 約第 1770-1771 行），把既有的 `sla_materials.select_preset_by_name_strict(initial_filament_profile_name)` 改成／補上：當目前印表機技術是 SLA 時，透過第 1 節的輔助函式，從剛選好的 `sla_prints` preset 反推出 `sla_materials` 的選取。
- [ ] 3.2 在 `combo_sla_print` 的 `OnSelect` 事件（`PresetComboBoxes.cpp`）裡加上同一段同步呼叫，處理不會整批重跑 `load_selections()` 的手動點選情境。
- [ ] 3.3 確認同一個目前作用中的 `sla_prints` 選取被多次呼叫同步邏輯時是幂等/不會有副作用的（考量到 design.md 非目標裡提到的既有重複刷新 cascade）。
- [ ] 3.4 **手動測試檢查點**：在診斷用的 Material 列仍然顯示的狀態下，手動在 `combo_sla_print` 裡切換不同項目，確認 `combo_sla_material` 每次都正確跟著換到對應材料。也重新跑一次 wizard 完成流程，確認重新載入時也會同步。

## 4. Fallback 與邊界情境防呆

- [ ] 4.1 當目前作用中的 `sla_prints`/`sla_materials` 選取所依賴的樹脂在 wizard 裡被取消勾選時，改選一個可見且相容的 fallback preset（比照既有的 `first_visible_filament_name` 寫法，`PresetBundle.cpp` 約第 1840-1845 行），不要留著一個不可見/無效的 preset 繼續被選中。
- [ ] 4.2 確認 `GuideFrame`（`WebGuideDialog.cpp`）有沒有等效於 `ConfigWizard` 的 `check_and_install_missing_materials()`，用來警告「這台印表機型號沒有相容的材料可選」（design.md 裡的 open question）。如果沒有，先確認既有的「全部材料都取消勾選」防呆是否已經涵蓋這個零材料情境，再決定是否需要新增額外的防呆邏輯。
- [ ] 4.3 **手動測試檢查點**：在 wizard 裡取消勾選目前正在使用中的材料，確認兩個下拉選單最後都落在一個有效的 fallback 選取上，而不是空的/不可見的狀態。

## 5. exposure_time 文件化（proposal 第 3 點，方案 A）

- [ ] 5.1 在 `exposure_time` 的材料端選項註冊處（`Preset.cpp`，`s_Preset_sla_material_options`）補上註解，說明它目前一律會被 `sla_prints` 覆蓋。
- [ ] 5.2 確認 `full_sla_config()` 覆蓋邏輯處（`PresetBundle.cpp` 約第 2411-2415 行）既有的註解是否已經說清楚這件事；如果需要，補充讓它同時引用材料端的選項註冊處。
- [ ] 5.3 不做任何功能性/行為變更——確認 `full_sla_config()` 的覆蓋邏輯完全沒被動到。

## 6. 完整手動驗證

- [ ] 6.1 重新完整測試最初回報的兩個流程：(a) 選單 → Setup Wizard → resin material 頁面 → 取消勾選一個 → Finish → Prepare 頁面；(b) Prepare 頁面 resin icon → resin material 頁面 → 取消勾選一個 → Finish → Prepare 頁面。確認使用者實際看到的下拉選單（`combo_sla_print`，依 design.md 非目標仍是主要/可見的那個）只反映勾選過的材料，且先前回報的「跳回第一個選項」症狀不再重現。
- [ ] 6.2 確認沒有造成 FDM 模式的回歸：切換到 FFF 印表機，確認 filament 下拉選單行為沒有變化。
- [ ] 6.3 確認診斷用的 Material 列（此時仍然強制顯示）結果跟 `combo_sla_print` 一致，作為移除它之前的最後一次視覺交叉檢查。

## 7. 清理

- [ ] 7.1 還原 `Plater.cpp` 的 `TEMP DIAGNOSTIC` 改動：把 `m_hsizer_sla_material_row->Show(!showSLA)` 還原回來（移除強制的 `Show(true)` 和它旁邊的註解）。
- [ ] 7.2 移除 `WebGuideDialog.cpp`（`SaveProfile()`、`apply_config()`）裡本次工作階段加入的所有 `DEBUG` 標記 `BOOST_LOG_TRIVIAL(error)` log。
- [ ] 7.3 移除 `PresetComboBoxes.cpp`（`PlaterPresetComboBox::update()` 的 `TYPE_SLA_MATERIAL` 分支）裡的 `DEBUG` 標記 log。
- [ ] 7.4 清理完成後,再重跑一次第 6 節的手動驗證,確認移除診斷不會讓某個依賴它才能運作的問題被掩蓋掉。
