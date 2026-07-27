## Context

Prepare 側邊欄的「樹脂/Resin」區塊裡疊放著兩個各自獨立的 preset 選擇器（同一個面板 `m_panel_sla_material_content`，最早在 `[Phase1] Fix SLA profile switching stability`、commit `28e6472d4c` 加入）：

- `combo_sla_print`（`Preset::TYPE_SLA_PRINT` → `PresetBundle::sla_prints`）
- `combo_sla_material`（`Preset::TYPE_SLA_MATERIAL` → `PresetBundle::sla_materials`）

Commit `a6fc416f3d`（"feat: Implement FDM/Resin switching logic and buttons"）讓這兩個在 `printer_technology == ptSLA` 時互斥顯示（`Plater.cpp` 約第 456-460 行：`m_hsizer_sla_print_in_resin->Show(showSLA)`、`m_hsizer_sla_material_row->Show(!showSLA)`），也就是**顯示 Process、隱藏 Material**，除了「先前決定把整個 Process 分頁藏起來（理由是『沒什麼列印參數好調』）」之外，沒有留下更明確的決策理由。

PhrozenOrca 的 `sla_prints` preset 命名方式是 `"<材料>@<印表機型號>"`（每個材料 × 相容印表機的組合各一份 JSON，例如 `"Speed Plus - Black@Phrozen Sonic Mighty Revo 16K"`），每一份都帶著校正過的 `exposure_time`／`bottom_exposure_time`／支撐／pad 數值。這讓 `combo_sla_print` 意外地能當材料選擇器用，但僅限於 Process 專屬的參數；Material 專屬的參數（`s_Preset_sla_material_options`：`material_correction_*`、`material_density`、`material_type/vendor/colour`、`initial_exposure_time`）則由目前選中的 `sla_materials` preset 控制——而使用者現在完全看不到也改不了它。

Setup Wizard 的 resin material 勾選已經正確地透過 `AppConfig::SECTION_MATERIALS` 驅動 `sla_materials.is_visible`（本次工作階段稍早已修好——參見 `WebGuideDialog.cpp` 裡 `GuideFrame::SaveProfile()` / `GuideFrame::apply_config()` 的 git 歷史）。`sla_prints` 完全沒有對應的可見度概念（`Preset::set_visible_from_appconfig()` 沒有 `TYPE_SLA_PRINT` 的分支），所以 wizard 的勾選狀態對使用者實際看到的下拉選單毫無影響。

本次工作階段調查過程中已經加進程式碼裡的診斷狀態（開發期間必須保留、最後才移除）：
- `Plater.cpp` 約第 456-461 行：`m_hsizer_sla_material_row->Show(true)`（原本是 `Show(!showSLA)`），標了 `TEMP DIAGNOSTIC` 註解，強制把真正的 Material 列跟 Process 列並排顯示以便比對。
- `WebGuideDialog.cpp`：`SaveProfile()` 和 `apply_config()` 裡有數行 `BOOST_LOG_TRIVIAL(error) << ... "DEBUG ..."`。
- `PresetComboBoxes.cpp`：`PlaterPresetComboBox::update()` 裡對 `TYPE_SLA_MATERIAL` 分支有 `BOOST_LOG_TRIVIAL(error) << ... "DEBUG ..."`。

這些特別用 `error` 等級是因為目前 `RelWithDebInfo` build 的 log 等級設定（`set_log_path_and_level(log_filename, 3)`）會把 `info`／`trace` 全部濾掉——這是本次工作階段中途實測 `info` 等級診斷完全沒輸出後才確認的。

## Goals / Non-Goals

**目標：**
- `sla_prints` preset 的可見度要反映 wizard 裡勾選/取消勾選的樹脂材料（從 `sla_materials.is_visible` 反推，而不是另外追蹤一份獨立的 AppConfig section）。
- 不管是透過 wizard 套用、切換印表機、應用程式啟動，還是手動點選下拉選單，選中某個 `sla_prints` preset 後都要讓 `sla_materials` 的選取保持同步，讓材料專屬參數永遠對應到目前選中的樹脂。
- 每一步都要能在目前的診斷 UI／log 狀態下手動測試驗證，才進到下一步。
- 不動到任何 FDM-only 程式碼，也不動 `PhrozenConnect`／`PartPlateList`／`IMToolbar`／`BUILD_PHROZEN_ORCA` 包住的程式碼。

**非目標：**
- 不重新規劃哪些參數該歸屬 `sla_prints` 還是 `sla_materials`（例如不會把 `exposure_time` 改成材料專屬）。第 3 點的規劃只做文件化，`full_sla_config()` 既有的覆蓋邏輯維持原樣。
- 不改變最終狀態下哪個下拉選單（`combo_sla_print` 還是 `combo_sla_material`）是使用者主要互動的那個——Process 仍然是主要且可見的那個，Material 仍然是隱藏、自動同步的那個。（要不要互換這兩者是更大的 UX 決策，使用者目前沒有提出這個需求。）
- 不處理同一個材料 alias 在不同印表機型號下有多筆 `sla_prints` preset 的去重問題——`is_compatible`（既有機制，跟本次改動無關）本來就已經把這個縮減成「目前作用中的印表機最多只有一筆符合可見條件」。
- 不修復另外查到的「三重重複刷新」問題（`GUI_App::load_current_presets()` 的 tabs_list 迴圈 + `Tab::on_presets_changed()` 的 `m_dependent_tabs` + `GuideFrame::apply_config()` 自己額外呼叫的部分）——不在本次範圍內；本次設計的同步邏輯是幂等的，就算被那個重複刷新機制多跑幾次結果也一樣，預期不需要為此另外修那個 cascade。

## Decisions

### D1：`sla_prints` 的可見度從 `sla_materials` 的可見度反推，不另開一份平行的 AppConfig section

**採用做法**：在 `PresetBundle::load_selections()` 裡，`load_installed_sla_materials()` 執行之後，新增一個步驟：依每個 `sla_prints` preset 的 alias 找到對應的 `sla_materials` preset，把它的 `is_visible` 複製過來。

**考慮過的替代方案**：另外寫一個 `load_installed_sla_prints()`，仿照 `load_installed_sla_materials()`，讀取一份獨立的 `AppConfig` section（用 process preset 名稱或材料名稱當 key）。
**為什麼不採用**：這樣會重新製造出這整輪調查一開始遇到的同一類 bug——兩份各自獨立追蹤的可見度資料（material 的 `AppConfig::SECTION_MATERIALS` vs 新開的 process 專屬 section）彼此可能不同步，而且還要把「第一次執行自動安裝」跟「使用者明確取消勾選」這個區分邏輯（本次工作階段稍早才用空字串標記修好的那套）再做一次。直接從已經正確的 `sla_materials.is_visible` 反推，才能維持單一真相來源。

### D2：重用 `Preset::alias`，不自己手動解析 `"@"`

**採用做法**：`Preset::alias` 在載入 vendor JSON 時就已經幫每種 preset 類型（包含 `sla_prints`）算好了（`PresetBundle.cpp` 約第 3181-3196 行：`preset_name.substr(0, preset_name.find_first_of("@"))`）。新的可見度／同步邏輯直接讀 `preset.alias`。

**考慮過的替代方案**：在新程式碼裡自己重新解析 `preset.name` 的 `"@"`（這是這一輪討論之前的原始計畫）。
**為什麼不採用**：會重複既有、已經測過的解析邏輯；如果自己重寫一份，容易跟既有邏輯（例如它同時還會填 `renamed_from`、呼叫 `boost::trim_right`）產生細微的行為落差。

### D3：材料查找共用一個輔助函式，可見度反推、選取同步、既有 fuzzy match 都重用它

**採用做法**：寫一個共用輔助函式（暫定叫 `find_matching_sla_material` 之類，確切放置位置與簽名留給 tasks.md 決定），輸入某個 `sla_prints` preset 的 alias，找出對應的 `sla_materials` preset——先試 alias 原樣，再試加上 `"Phrozen "` 前綴，跟 `Preset::set_visible_from_appconfig()`（`Preset.cpp` 約第 680-682 行）裡既有的 fuzzy match 邏輯一致。

**考慮過的替代方案**：在兩個新增的呼叫點（可見度反推、選取同步）各自內嵌一份查找邏輯。
**為什麼不採用**：`"Phrozen "` 前綴不一致本來就是已知的脆弱點；如果同一段比對邏輯散落三份（既有的 + 新增的兩份），以後這三份互相走鐘、行為不一致的風險會提高。

### D4：`sla_materials` 的選取同步只收斂在 2 個 choke point，不逐一 hook 每個呼叫點

**採用做法**：
1. `PresetBundle::load_selections()`（`PresetBundle.cpp` 約第 1770-1771 行）——把既有的 `sla_materials.select_preset_by_name_strict(initial_filament_profile_name)`（目前讀的是一個不相干、被借用的 ini key）改成／補上：從剛選好的 `sla_prints` preset 的 alias 反推對應材料。這一個地方就能涵蓋所有「整批重新載入」的情境：wizard 套用、切換印表機、應用程式啟動（追蹤過的呼叫點：`PresetBundle.cpp:110`、`:632`、`:1770`，以及 `WebGuideDialog.cpp:1217`，全部最終都會流經 `load_presets()` → `load_selections()`）。
2. `combo_sla_print` 的 `OnSelect` 事件（`PresetComboBoxes.cpp`）——涵蓋 `load_selections()` 涵蓋不到的唯一情境：使用者手動點選下拉選單，這是一次輕量的 `select_preset_by_name` 呼叫，不會整批重跑 `load_selections()`。

**考慮過的替代方案**：逐一 hook 目前已知的每個 `sla_prints.select_preset*` 程式呼叫點（調查過程中列出的有 4 個以上）。
**為什麼不採用**：以後新增的呼叫點很容易被漏掉；集中收斂到 `load_selections()`（本來就已經位在所有「整批重載」路徑的下游）能直接消除這一整類風險。只有「使用者手動點選」這條路徑結構上不會經過 `load_selections()`，所以精準需要 2 個 choke point，不是 N 個。

**考慮過的替代方案**：在 `full_sla_config()` 裡即時依選中的 `sla_prints` preset 的 alias 動態算出材料設定，完全不去真的改動 `sla_materials` 的選取指標。
**為什麼不採用**：其他 UI（如果直接開啟 Material Settings 分頁、`update_sla_preset_sidebar_buttons()`、Save/Delete preset 按鈕狀態）都會直接讀 `sla_materials.get_selected_preset()`；如果讓這個指標一直停在舊值、只有切層設定是對的，反而會製造出一種更難懂的新的不一致（實際切出來的 g-code/PRZ 是對的，但畫面上到處顯示的「目前選中材料」卻是錯的）。

### D5：`sla_materials` 上的 `exposure_time`——只做文件化，不改行為（proposal 第 3 點，走方案 A）

**採用做法**：在 `exposure_time` 的材料端選項註冊處、以及 `full_sla_config()` 覆蓋邏輯附近補上註解，說明它目前一律會被 `sla_prints` 蓋掉。不做任何功能性程式碼變更。

**理由**：已經實測確認,目前沒有任何一份 material JSON（包含 `sla_material_common.json`，在 commit `38be6b9170` 把它原本明確寫的 `exposure_time: 12` 移除之後）會設這個 key，而目前 6 份 process 葉節點全部都有設——所以這個覆蓋邏輯目前是無條件生效的，而且以現有的資料模型，根本沒辦法區分「process 明確設了這個值」跟「process 只是吃到繼承鏈／schema 的預設值」（兩者最後都會解析成 edited preset 上一個非 null 的 `ConfigOption`）。要做成有條件的覆蓋，得靠一個脆弱的「跟寫死的 3.0 是否不同」的判斷式,或是新增一套「明確設定 vs 繼承而來」的追蹤機制——這已經超出 proposal 的非目標範圍。

### D6：開發順序——先在目前的診斷狀態下開發，最後才清理

**採用做法**：D1-D4 的實作期間，維持目前暫時性強制顯示的 Material 列（`Plater.cpp`）和 `error` 等級 `DEBUG` log（`WebGuideDialog.cpp`、`PresetComboBoxes.cpp`）不動，讓每個增量都能透過同時觀察（現在並排顯示的）Process 和 Material 下拉選單、以及讀 log 輸出來手動驗證。只有在完整流程（wizard 勾選 → 套用 → 兩個下拉選單都正確 → 選取同步正確）的手動測試全部通過之後，才把 `Plater.cpp` 的 `TEMP DIAGNOSTIC` 改動還原成 `Show(!showSLA)`，並把 `DEBUG` log 全部刪掉，作為最後一項任務。

## Risks / Trade-offs

- **[風險]** `sla_prints` preset 的名稱／alias 不符合 `"材料@印表機"` 這個命名慣例（例如 `sla_print_common.json`，或未來新增的、不綁定材料的通用 process preset）→ alias 查找找不到對應材料。**因應方式**：預設為 `is_visible = true`（寧可多顯示、不要因為分類不出來就藏起來），並記一筆警告 log 方便之後追查。
- **[風險]** alias 解析出來的字串在 `sla_materials` 裡找不到對應的 preset（打字錯誤、材料被改名、process JSON 變成孤兒檔）→ 跟上面一樣，預設可見 + 警告 log。
- **[風險]** `sla_prints` 的 alias（`"Speed Plus - Black"`）跟 `sla_materials` 的 name（`"Phrozen Speed Plus - Black"`）之間的 `"Phrozen "` 前綴不一致，導致比對失敗。**因應方式**：共用輔助函式（D3）兩種形式都會試，跟既有寫法一致。
- **[風險]** 使用者取消勾選目前正在使用中的那顆材料 → 兩個下拉選單可能都變成沒有有效選取。**因應方式**：fallback 到第一個可見且相容的 preset，仿照既有的 `first_visible_filament_name` 寫法（`PresetBundle.cpp` 約第 1840-1845 行）。
- **[風險]** 使用者勾選的材料，在目前選中的印表機型號下完全沒有對應的 `sla_prints` preset（資料本身不完整——不是每個材料 × 印表機的組合都已經校正好）→ Process 下拉選單可能變成空的，即使使用者以為自己「裝了」材料。**因應方式**：仿照既有的「這台印表機沒有預設材料」警告模式（`ConfigWizard.cpp` 的 `check_and_install_missing_materials()`）提示使用者；`GuideFrame` 這條實際在跑的路徑有沒有等效機制待確認（見下方 Open Questions）。
- **[風險]** 全部材料都被取消勾選 → 兩個下拉選單都變空。**因應方式**：跟既有 filament wizard 流程一樣的「至少要保留一個」防呆。
- **[取捨]** 2-choke-point 的同步設計（D4）比逐一 hook 每個呼叫點更簡單、風險更低，但代價是**依賴 `load_selections()` 繼續是所有「整批重載」路徑共同的下游收斂點**——如果以後新增了一條會跳過 `load_selections()` 的重載路徑，這個同步邏輯在那條路徑上會悄悄失效。
- **[取捨]** 把 UI／log 清理留到最後一項任務（D6）意味著暫時性的強制顯示和 error 等級 log 會一直留在程式碼裡（包含中間任何 commit），直到最後才清掉——這是使用者明確要求的順序，可以接受,但不該在這個狀態下上到 `main`／正式版本。

## Migration Plan

1. 先實作 D3（共用材料查找輔助函式）——這一步本身不會改變任何可見行為，但後兩步都依賴它。
2. 實作 D1（`sla_prints` 可見度反推）——在目前強制顯示 Material 列的診斷狀態下測試，比對兩個下拉選單的內容跟 wizard 勾選狀態是否一致。
3. 實作 D4（2 個 choke point 的選取同步）——透過選不同的 Process preset,確認（診斷用的）Material 下拉選單有沒有跟著換。
4. 實作 D5（純文件化的註解）——不需要額外測試,讀一遍確認即可。
5. 完整手動測試：重跑最初回報 bug 的兩個流程（選單走 wizard、Prepare 頁面的 resin icon 捷徑），確認下拉選單筆數、選取狀態是否持久，以及「跳回第一個選項」這個症狀不再重現。
6. 清理：把 `Plater.cpp` 診斷用的 `Show(true)` 還原成 `Show(!showSLA)`，移除本次工作階段加入的所有 `DEBUG` 標記 log。

除了正常的 git revert 之外不需要額外的 rollback 機制——這是在既有 preset 基礎架構上新增的邏輯，不涉及資料遷移。

## Open Questions

- `GuideFrame`（實際在跑的那個 wizard，`WebGuideDialog.cpp`）有沒有等效於 `ConfigWizard` 的 `check_and_install_missing_materials()`，用來警告「這台印表機型號沒有相容的材料/process 可選」？調查過程中沒有確認過——在依賴它作為「勾選的材料在這台印表機下沒有 process preset」這個風險的因應方式之前，需要先查清楚。
- 共用的材料查找輔助函式（D3）應該放在 `Preset`、`PresetCollection`，還是 `PresetBundle.cpp` 裡的自由函式？留給實作（tasks.md）決定，目前沒有哪種既有慣例特別適合這種跨 collection 查找。
