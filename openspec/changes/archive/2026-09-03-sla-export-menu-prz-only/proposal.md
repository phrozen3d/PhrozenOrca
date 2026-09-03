## Why

在 Resin/SLA 模式下，右上角的匯出下拉選單（`m_print_option_btn` → `SidePopup`）目前仍會列出「Export plate sliced file」「Export all sliced file」等 FDM 導向的匯出選項，這些格式對 SLA 使用者沒有意義，且容易造成誤點。SLA 使用者實際只會用到「Export PRZ file」。此問題與既有的 `sla-ui-feature-gating` 規格屬同一類（SLA 模式下隱藏 FDM-only 的 UI 入口），可直接延伸該規格。

實作並手動驗證右上角下拉選單後，使用者進一步發現 **File → Export 子選單**（`MainFrame.cpp` 中的 `export_menu`）裡同樣殘留三個 FDM/切層格式導向的項目——「Export plate sliced file」「Export all plate sliced file」「Export G-code」——在 SLA 模式下同樣沒有意義。這個子選單原本在本 change 的初版 proposal 中被列為 Non-Goal（刻意不動），但驗證階段（task 2.5）發現「不受影響」這個假設是錯的，因此在 change 尚未 archive 前，直接修正本提案的範圍與 Non-Goal，而不是另開新的 change——這是同一次驗證流程中發現的缺口，非獨立的新功能。

## What Changes

- Resin/SLA 模式下，右上角匯出下拉選單（`create_side_tools()` 中 `m_print_option_btn` 綁定的 `SidePopup`）只 append「Export PRZ file」按鈕，不再 append 「Print」（send_gcode，僅開發者模式）、「Export plate sliced file」、「Export all sliced file」等其他項目。
- 下拉選單本身（`m_print_option_popup_btn` / `SidePopup`）與其展開箭頭按鈕（`m_print_option_btn`，圖示 `sidebutton_dropdown`）維持既有的建立與顯示邏輯不變，不因項目數量降為 1 而被移除或隱藏——這與現有 `m_slice_option_btn` 在 SLA 模式下只剩「Slice plate」一個選項時仍保留下拉箭頭的行為一致（`MainFrame.cpp:2082-2094`）。
- **（新增範圍）** File → Export 子選單中的「Export plate sliced file」「Export all plate sliced file」「Export G-code」三個項目，在 Resin/SLA 模式下 SHALL NOT 顯示（完全從選單移除，而非灰階 disable）；同選單中的「Export all objects as one STL」「Export all objects as STLs」「Export Generic 3MF」「Export Preset Bundle」與 SLA 使用者的模型/設定匯出仍有意義，維持顯示不受影響。
- **（實測發現後追加）** 「Export plate sliced file」的鍵盤快捷鍵 `Ctrl+G` 是獨立於選單結構之外、寫死在 `MainFrame` 全域按鍵 handler 中的觸發路徑，隱藏選單項目並不會讓它失效；Resin/SLA 模式下 SHALL 一併擋掉此快捷鍵，不得繞過選單觸發匯出（實測發現原本會退化為「另存 .3mf」）。
- FDM 模式下，右上角匯出下拉選單、File → Export 子選單、以及 `Ctrl+G` 快捷鍵的內容與行為完全不變。
- 不修改 `export_prz()`/`generate_prz()`、`export_gcode()`、`export_sliced_file`、`export_stl()`、`export_core_3mf()`、`export_config()` 等匯出功能本體。

## Capabilities

### Modified Capabilities
- `sla-ui-feature-gating`: 新增兩項需求 —
  1. Resin/SLA 模式下右上角匯出下拉選單只顯示「Export PRZ file」，下拉選單本身與展開箭頭維持顯示與可操作；FDM 模式行為不變。
  2. Resin/SLA 模式下 File → Export 子選單中的「Export plate sliced file」「Export all plate sliced file」「Export G-code」SHALL NOT 顯示，且其鍵盤快捷鍵（`Ctrl+G`）SHALL NOT 繞過選單觸發匯出；其餘 Export 子選單項目與 FDM 模式行為不變。

## Impact

- Affected code:
  - [src/slic3r/GUI/MainFrame.cpp](src/slic3r/GUI/MainFrame.cpp) — `MainFrame::create_side_tools()`，`m_print_option_btn` 的 `wxEVT_BUTTON` handler（約 2114-2330 行）。
  - [src/slic3r/GUI/MainFrame.cpp](src/slic3r/GUI/MainFrame.cpp) — `MainFrame::init_menubar_as_editor()` 中 `export_menu` 的建立（約 2906-2935 行），以及既有的 `wxEVT_MENU_OPEN` handler（約 3159 行，目前用於動態更新 "Duplicate Current Plate" 可見性，將延伸同一機制）。
  - [src/slic3r/GUI/MainFrame.hpp](src/slic3r/GUI/MainFrame.hpp) — 新增追蹤 `export_menu` 與其中三個 `wxMenuItem*`/位置/attached 狀態的 private members，比照既有 `m_duplicate_plate_menu_item` 系列成員的命名與型別風格。
  - [src/slic3r/GUI/MainFrame.cpp:858](src/slic3r/GUI/MainFrame.cpp) — 全域鍵盤事件 handler 中處理 `Ctrl+G` 的判斷式，加上 printer technology 檢查。
- Not affected: `SidePopup`/`SideButton` widget 實作（[src/slic3r/GUI/Widgets/SideMenuPopup.cpp](src/slic3r/GUI/Widgets/SideMenuPopup.cpp)）、PRZ/G-code/sliced-file/STL/3MF/config 匯出功能本體、`export_menu` 中其餘四個項目（STL x2、Generic 3MF、Preset Bundle）。
