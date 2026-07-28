## ADDED Requirements

### Requirement: SLA Process preset 的可見度要跟著樹脂材料的安裝狀態
系統 SHALL 依照該 `sla_prints`（SLA Process）preset 名稱所指涉的那顆樹脂在 `sla_materials`（SLA Material）裡的安裝／勾選狀態，來決定該 Process preset 的可見度，而不是另外幫 `sla_prints` 追蹤一份獨立的安裝狀態。

#### Scenario: 在 wizard 裡取消勾選材料會隱藏對應的 Process preset
- **WHEN** 使用者在 Setup Wizard 的 resin material 頁面取消勾選某顆樹脂，並完成 wizard
- **THEN** 每一個名稱可解析對應到那顆材料的 `sla_prints` preset，在 `combo_sla_print` 下拉選單裡都不再可見

#### Scenario: 在 wizard 裡勾選材料會顯示對應的 Process preset
- **WHEN** 使用者在 Setup Wizard 的 resin material 頁面勾選某顆樹脂，並完成 wizard
- **THEN** 名稱可解析對應到那顆材料、且跟目前作用中印表機相容的 `sla_prints` preset，會出現在 `combo_sla_print` 下拉選單裡

#### Scenario: 每次重新載入都會自動重算可見度，不需要手動介入
- **WHEN** preset 被重新載入（wizard 套用、切換印表機、或應用程式啟動）
- **THEN** `sla_prints` 的可見度會依當下 `sla_materials` 的可見度狀態重新計算，且一定排在 `sla_materials` 本身的可見度先算完之後

### Requirement: 無法解析對應材料的 Process preset 預設為可見
系統 SHALL 在 `sla_prints` preset 的名稱無法解析出對應的 `sla_materials` preset 時（沒有 `@` 分隔符號，或是解析出來的材料名稱在 `sla_materials` 裡完全找不到匹配），把該 preset 視為可見，而不是把它隱藏起來。

#### Scenario: Process preset 名稱沒有材料前綴
- **WHEN** 某個 `sla_prints` preset 的名稱／alias 裡完全沒有 `@` 分隔符號（例如一份共用/通用的 process preset）
- **THEN** 該 preset 的可見度不受依材料過濾的邏輯影響（視為可見）

#### Scenario: Process preset 的材料前綴找不到對應的材料 preset
- **WHEN** 某個 `sla_prints` preset 的 alias，不論是原樣還是加上 `"Phrozen "` 前綴，都對不上任何一個 `sla_materials` preset 的名稱
- **THEN** 該 preset 預設為可見，並記錄一筆警告 log 標明是哪個對不上的 preset 名稱

### Requirement: SLA Material preset 的選取不需要跟著 SLA Process preset 同步
系統 SHALL NOT 嘗試讓 `sla_materials` 的選取跟著 `sla_prints` 的選取連動。一顆樹脂材料理論上可能貢獻的每一個參數，實際上都已經被合併進對應的 `sla_prints` preset（PhrozenOrca 依「材料 × 印表機型號」在 process JSON 裡個別校正），所以 `sla_materials` 目前技術上選中哪一個 preset，不會影響任何切層輸出；`combo_sla_material` 本身也不會顯示給使用者看（[Plater.cpp](PhrozenOrca/src/slic3r/GUI/Plater.cpp) 讓它的那一列保持隱藏）。這個決定的背景記錄在 design.md D4。

（本專案曾經在同一輪工作階段裡實作過完整的三個 choke point 同步機制並手動驗證通過，之後才確認材料端的參數實際上不會被使用，因而決定移除；相關程式碼異動與理由仍保留在 git 歷史與 design.md 裡。）

#### Scenario: 選擇者不需要處理使用者自訂（另存新檔）的 SLA Process preset 名稱
- **WHEN** 使用者把 `sla_prints` preset 修改參數後另存成一份使用者自訂名稱的新 preset（不再遵守 `"材料@印表機"` 命名慣例）
- **THEN** 系統不需要嘗試從這個自訂名稱解析出對應材料——`sla_materials` 的選取維持原樣不受影響，因為它本來就不需要跟 `sla_prints` 保持同步

### Requirement: preset 被隱藏時要有 fallback 選取
系統 SHALL 在使用者於 wizard 裡取消勾選、導致目前選中的 `sla_prints` 或 `sla_materials` preset 被隱藏時，改選一個仍然可見／相容的 fallback preset。

#### Scenario: 目前作用中的材料被取消勾選
- **WHEN** 使用者取消勾選了目前作用中的 `sla_prints`/`sla_materials` 選取所依賴的那顆樹脂，並完成 wizard
- **THEN** 系統會從其餘仍然可見且相容的 preset 裡挑一個 fallback 選取，而不是留著一個已隱藏或無效的 preset 繼續被選中

#### Scenario: 全部材料都被取消勾選
- **WHEN** 使用者在 wizard 裡把每一顆樹脂材料都取消勾選
- **THEN** 系統套用跟 wizard 流程裡既有的「至少要保留一個材料」防呆邏輯一致的防呆，而不是讓兩個下拉選單都變空、也沒有任何提示

### Requirement: SLA Material 上的 exposure_time 要標明目前不生效
系統 SHALL 在 `sla_materials` 的 `exposure_time` 選項註冊處，以及 `full_sla_config()` 的覆蓋邏輯處，都補上文件說明，標明目前的合併邏輯下 material 端的值一律會被 `sla_prints` 端的值取代——但不改變這個合併行為本身。

#### Scenario: Material 端設定的 exposure_time 不會生效
- **WHEN** 某個 `sla_materials` preset 明確設定了 `exposure_time` 的值
- **THEN** 最終合併出來的 SLA 切層設定仍然使用 `sla_prints` preset 的 `exposure_time` 值（跟目前行為一致，沒有改變），並且旁邊的程式碼註解會說明這一點

### Requirement: 開發要在既有診斷狀態下進行，清理是最後一步
本次改動的實作過程 SHALL 在實作與手動測試上述各項需求的期間，保留本次工作階段已經加入的暫時性診斷（`Plater.cpp` 裡強制顯示的 `combo_sla_material` 列，以及 `WebGuideDialog.cpp`／`PresetComboBoxes.cpp` 裡 `error` 等級的 `DEBUG` log），並且只有在手動測試確認上述需求都已達成之後，才將它們移除。

#### Scenario: 實作期間診斷保持可用
- **WHEN** 實作以上任何一項需求
- **THEN** 診斷用的強制顯示 Material 列以及 DEBUG log 仍然存在且正常運作，讓改動可以被手動驗證

#### Scenario: 只有在手動驗證通過之後才移除診斷
- **WHEN** 手動測試確認可見度反推、選取同步、以及 fallback 行為都照規格運作，且涵蓋選單走 wizard 和 Prepare 頁面 resin icon 捷徑這兩條流程
- **THEN** 把 `Plater.cpp` 裡 Material 列的可見度邏輯還原成原本的 `Show(!showSLA)`，並移除本次工作階段加入的所有 `DEBUG` 標記 log
