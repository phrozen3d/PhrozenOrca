## Context

右上角匯出下拉選單由 `MainFrame::create_side_tools()`（[src/slic3r/GUI/MainFrame.cpp:1972-2343](src/slic3r/GUI/MainFrame.cpp)）建立，由兩個獨立元件組成：

- `m_print_btn`：主要匯出按鈕，label/行為依 `m_print_select` 決定。
- `m_print_option_btn`：右側展開箭頭（icon `sidebutton_dropdown`），點擊後建立 `SidePopup`（[src/slic3r/GUI/Widgets/SideMenuPopup.cpp](src/slic3r/GUI/Widgets/SideMenuPopup.cpp)），並以 `p->append_button(SideButton*)` 動態塞入選項按鈕。

目前 `m_print_option_btn` 的 click handler（2114-2330 行）在判斷 `bIsFDMMode = wxGetApp().get_ui_printer_technology() == ptFFF` 後，僅對「Export G-code file」vs「Export PRZ file」做二選一（2300-2324 行），但「Print」（send_gcode，2159-2174 行）、「Export plate sliced file」（2186-2187, 2297 行）、「Export all sliced file」（2189-2190, 2298 行）等按鈕在 SLA 模式下仍會被無條件 append 到 popup 中。

已有直接可參照的既有模式：同一函式內的 `m_slice_option_btn` 下拉選單，在 SLA 模式下 `slice_all_btn` 直接不建立、popup 內只 append `slice_plate_btn` 一個按鈕（2082-2109 行），而展開箭頭（`m_slice_option_btn`）本身完全不受影響地維持顯示——這證明 `SidePopup`/`SideButton` 架構原生支援「箭頭固定顯示、popup 內容按 mode 動態增減、可以只剩一項」。

**（範圍擴增）File → Export 子選單。** 手動驗證右上角下拉選單（task 2.1-2.3）之後，使用者發現 `MainFrame::init_menubar_as_editor()` 中另一個獨立入口——File → Export 子選單（`export_menu`，[MainFrame.cpp:2906-2935](src/slic3r/GUI/MainFrame.cpp)）——同樣殘留三個切層格式相關項目：「Export plate sliced file」（2918 行，`\tCtrl+G`）、「Export all plate sliced file」（2922 行）、「Export G-code」（2926 行）。這三項在 SLA 模式下沒有意義，理由與右上角下拉選單相同；子選單中其餘四項（「Export all objects as one STL」2908、「Export all objects as STLs」2911、「Export Generic 3MF」2914、「Export Preset Bundle」2929）是通用模型/設定匯出，與切層格式無關，SLA 使用者仍可能用到，維持顯示。

這個 menu 與右上角下拉選單的架構完全不同，不能直接套用 D1 的做法：`export_menu` 是傳統 `wxMenu`，透過 `m_menubar->Append(fileMenu, ...)`（3558 行）加入 `m_menubar`，並經 `m_topbar->SetFileMenu(fileMenu)`（3403 行）掛到自訂 topbar 的 File 按鈕——**只在 `init_menubar_as_editor()` 執行一次時建立，執行期間不會重建**。這與已封存的 `2026-09-01-hide-duplicate-plate-sla` change 處理 `Edit → Duplicate Current Plate` 時遇到的情境完全相同（同樣是「只建立一次的 wxMenu」，見該 change 的 design.md Context/D1），因此直接沿用其解法：記錄 `wxMenuItem*` 與原始插入位置，在 `wxEVT_MENU_OPEN` 時以 `wxMenu::Remove()`/`Insert()` 動態掛回或移除；`MainFrame` 上已經有一個為此目的綁定的 `wxEVT_MENU_OPEN` handler（3159 行，目前呼叫 `update_duplicate_plate_menu_item_visibility()`），可以在同一個 handler 內追加一次呼叫，不需要新增事件綁定。

與 Duplicate-Plate 案例的差異：`export_menu` 內這三個項目彼此相鄰、中間沒有 `AppendSeparator()`（2917-2929 行是連續的 `append_menu_item` 呼叫），所以不會遇到 Duplicate-Plate 案例中「項目與其後的 separator 要當一組」的複雜度，可以把三個項目視為一個連續區塊，用單一組 `attached` 狀態與三個獨立的 `wxMenuItem*`/原始位置成員追蹤即可。

## Goals / Non-Goals

**Goals:**
- Resin/SLA 模式下，`m_print_option_btn` 開出的 `SidePopup` 只 append「Export PRZ file」一個 `SideButton`。
- 展開箭頭 `m_print_option_btn` 與 popup 機制本身不因項目數變化而被隱藏或停用。
- FDM 模式（`bIsFDMMode == true`）下的 popup 內容與行為完全不變（沿用既有的 `support_send` / `support_print_all` / `enable_multi_machine` 等既有條件邏輯）。
- Resin/SLA 模式下，File → Export 子選單（`export_menu`）中的「Export plate sliced file」「Export all plate sliced file」「Export G-code」三個 `wxMenuItem` SHALL NOT 顯示（真正從選單移除，不是灰階），且切換 printer technology 後下次開啟選單即反映最新狀態。
- FDM 模式下 `export_menu` 的內容與行為完全不變；`export_menu` 中其餘四個項目（STL x2、Generic 3MF、Preset Bundle）在兩種模式下都維持顯示不受影響。

**Non-Goals:**
- 不改動 `export_prz()` / `ExportPRZJob` / `export_gcode()` / `export_sliced_file` / `export_stl()` / `export_core_3mf()` / `export_config()` 等匯出功能本體。
- 不改動 `m_print_btn` 主按鈕本身在 SLA 模式下預設顯示「Export PRZ file」的既有邏輯（1979-1985 行），該邏輯已正確。
- 不新增設定項或使用者可調整的顯示選項；純粹依 `ptSLA`/`ptFFF` 分支。
- 不重構 `append_menu_item()` / `enable_menu_item()` 這類通用 helper 使其支援正式的「可見性」callback——沿用 Duplicate-Plate 案例已驗證過的 `wxMenu::Remove()`/`Insert()` 局部作法即可。
- 不處理 `export_menu` 中其餘四個項目（STL x2、Generic 3MF、Preset Bundle）的顯示邏輯，維持現況。

## Decisions

**D1. 沿用既有 `bIsFDMMode` 旗標分支，收斂 SLA 分支只 append PRZ 按鈕。**
在 `m_print_option_btn` 的 click handler 中，把「Print」（send_gcode，dev mode）、`export_sliced_file_btn`、`export_all_sliced_file_btn` 的建立與 `p->append_button(...)` 呼叫，改為只在 `bIsFDMMode == true` 時才執行；SLA 分支（`else` 分支，2312-2324 行原邏輯）維持只建立並 append `export_prz_btn`。
- 替代方案：在 append 之後才用一個迴圈依技術別過濾/移除 popup 內按鈕。捨棄理由：`SidePopup` 沒有暴露移除單一子按鈕的 API，且會比「在建立階段就不 append」更複雜、更易造成佈局重算問題。
- 替代方案：新增一個獨立的 `bIsSLAExportSimplified` 開關並包一層 helper 函式。捨棄理由：範圍過小，不需要額外抽象；現有程式碼已經是 `if (bIsFDMMode) {...} else {...}` 的兩分支結構，直接在既有分支內增刪 append 呼叫即可，符合「不做超出需求的重構」原則。

**D2. 不變動 `m_print_option_btn`（箭頭）與 `SidePopup` 的建立/顯示邏輯。**
箭頭按鈕是與 popup 內容數量無關的獨立 sibling widget（1986, 1996-1997 行建立與加入 sizer），且其顯示/隱藏只綁定於「only_gcode_mode / using_exported_file」情境（1280-1289 行一帶，與本次改動的 `bIsFDMMode` 分支無關）。因此不需要新增任何「至少保留箭頭」的特殊處理——只要不去動 1986/1996-1997 行與 handler 綁定本身，箭頭自然維持顯示與可點擊。

**D3. `print_plate_btn` 已經是 `bIsFDMMode` 專屬（2178-2181, 2192-2201, 2275-2276 行），不需調整。**
確認現況：`print_plate_btn` 只在 `bIsFDMMode` 為真時建立，SLA 模式下本來就不會出現，符合需求，不在本次改動範圍內。

**D4. File → Export 子選單三項目，沿用 Duplicate-Plate 案例的 `wxMenu::Remove()`/`Insert()` + `wxEVT_MENU_OPEN` 機制，視為單一連續區塊處理。**
在 `MainFrame.hpp` 新增：
- `wxMenu* m_export_menu{ nullptr }`（儲存 `export_menu` 指標，供之後的 visibility 函式使用）
- 三個 `wxMenuItem*`（`m_export_sliced_file_menu_item`、`m_export_all_sliced_file_menu_item`、`m_export_gcode_menu_item`）
- 一個起始位置 `int m_export_sla_hidden_items_pos{ wxNOT_FOUND }`（三項目建立時的第一個位置；因三者連續、中間無 separator，第二、三項位置固定為 `pos+1`、`pos+2`，不需個別存位置）
- 一個共用的 `bool m_export_sla_hidden_items_attached{ true }`

在 `MainFrame.cpp` 建立 `export_menu` 時（2917-2929 行一帶），比照 `m_duplicate_plate_menu_item_pos = editMenu->GetMenuItemCount();` 的寫法，在 append「Export plate sliced file」之前記錄 `m_export_sla_hidden_items_pos = export_menu->GetMenuItemCount();`，並把三個 `append_menu_item(...)` 呼叫的回傳值存進對應成員；`export_menu` 指標存入 `m_export_menu`。

新增 `void MainFrame::update_export_menu_sla_gated_items_visibility()`：
- `should_show = wxGetApp().get_ui_printer_technology() == ptFFF`
- 顯示（FDM，且目前未 attach）：依序 `Insert(pos, sliced_file)` → `Insert(pos+1, all_sliced_file)` → `Insert(pos+2, gcode)`（由前往後插入，確保順序與原始一致，比照 Duplicate-Plate 案例「先插項目、讓後續項目自然被推後」的做法）。
- 隱藏（SLA，且目前 attach）：依序 `Remove(gcode)` → `Remove(all_sliced_file)` → `Remove(sliced_file)`（由後往前移除，移除較後面的項目不影響前面尚未移除項目的目前位置，避免重新計算 index）。
- 在既有的 `this->Bind(wxEVT_MENU_OPEN, ...)`（3159 行）內追加一行呼叫此函式，與 `update_duplicate_plate_menu_item_visibility()` 併存於同一個 handler，不新增事件綁定。

**Alternatives considered：**
- *比照 Filament-menu 的「每次開啟時重建」做法*：不適用，`export_menu`／`fileMenu` 只建立一次，不會在每次開啟時重新執行 `init_menubar_as_editor()`。
- *用 `cb_condition`/`wxEVT_UPDATE_UI` 做 `Enable(false)`*：只會讓項目灰階，不符合使用者「完全隱藏」的需求（且原本這三項的 `cb_condition`——`can_export_gcode()`/`can_export_all_gcode()`——是用來判斷「是否已完成切片」，與「是否為 SLA 模式」是兩件不同的事，不應混用同一個 callback）。
- *在 printer technology 切換時重建整個 `fileMenu`／`m_menubar`*：影響範圍過大，且已有更輕量、經過實測驗證的 Remove/Insert 方案可用。

**D5.（實測發現後追加）`Ctrl+G` 是獨立於 menu 結構之外的原始鍵盤快捷鍵，需另外在 handler 內加上技術別判斷。**
`D4` 實作完成、手動驗證 4.1-4.3、4.5 皆通過後，實測 4.4（SLA 模式下按 `Ctrl+G`）發現：即使「Export plate sliced file」已從 `export_menu` 移除，`Ctrl+G` 仍會觸發匯出（實際表現為「另存 .3mf」）。追查後發現根因與 `wxMenuItem` 的 accelerator（D4 原本記錄的 Risk 項）無關，而是 [MainFrame.cpp:858](src/slic3r/GUI/MainFrame.cpp) 有一段完全獨立、寫死的全域按鍵處理：

```cpp
else if (evt.CmdDown() && evt.GetKeyCode() == 'G') { if (can_export_gcode()) { wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_EXPORT_SLICED_FILE)); } evt.Skip(); return; }
```

這段程式碼綁在 `MainFrame` 的原始鍵盤事件 handler 中（與 `wxMenu`/`wxMenuBar`/`wxAcceleratorTable` 完全無關的另一條路徑），只檢查 `can_export_gcode()`（是否已完成切片），從未檢查 printer technology，因此不管 `export_menu` 中的項目是否被 `Remove()`，這個快捷鍵都會直接 `wxPostEvent(EVT_GLTOOLBAR_EXPORT_SLICED_FILE)` → `Plater::export_gcode_3mf()`。這解釋了「另存 .3mf」的現象：SLA 情境下沒有 gcode 可用，`export_gcode_3mf()` 退化為單純的 3mf 另存流程。

**Choice**：直接在這行既有判斷式中加上 `wxGetApp().get_ui_printer_technology() == ptFFF &&`，與 D4 中隱藏該選單項目的判斷依據保持一致，讓快捷鍵與選單可見性同步。
**Rationale**：這是唯一一處會繞過 `export_menu` 觸發相同匯出行為、且未做過 SLA 判斷的路徑（已排查同一個 handler 中相鄰的 `Ctrl+Shift+G`——觸發 Print/Send Gcode，是不同的動作，不在本次「匯出選單」的需求範圍內；也已排查 `EVT_GLTOOLBAR_EXPORT_SLICED_FILE`/`EVT_GLTOOLBAR_EXPORT_ALL_SLICED_FILE` 的所有其他觸發點——`m_print_option_btn` 的 popup 已在 D1 收斂為 `bIsFDMMode` 專屬，`export_menu` 的選單點擊已在 D4 处理，因此只剩這一處需要修補）。
**Alternatives considered**：
- *在 `Plater::export_gcode_3mf()` / `on_action_export_sliced_file()` 內加上技術別 guard*：屬於更集中、防禦性更高的作法，但目前排查到的唯一漏洞路徑就是這一行鍵盤 handler；在尚未發現其他真正需要防禦的呼叫點之前，直接於 handler 層修補是更貼近問題本身、影響面更小的做法，避免對 `Plater` 的公用匯出函式做超出本次需求的變更。

**修補後的再次實測發現（非新問題，記錄供釐清用）**：套用 D5 修補後，SLA 模式下按 `Ctrl+G` 改為觸發 Preview 頁籤既有的「跳轉到指定層數」（[GLCanvas3D.cpp:3705-3713](src/slic3r/GUI/GLCanvas3D.cpp)，`m_layers_slider->show_go_to_layer(true)`），且運作正常。這是完全獨立、與本次改動無關的既有功能——`GLCanvas3D.cpp:3711` 的判斷式是 `(evt.CmdDown() || evt.ShiftDown()) && evt.GetKeyCode() == 'G'`，只要求 `m_canvas_type == CanvasPreview` 且未啟用 gizmo，與 printer technology 完全無關，FDM/SLA 皆適用。以 `git diff`/`git status` 確認本次 change 全程只修改過 `MainFrame.cpp`／`MainFrame.hpp` 兩個檔案，`GLCanvas3D.cpp`／`IMSlider.cpp` 未被觸碰。真正的原因是：`MainFrame.cpp:858` 原本（修補前後皆然）在判斷式跑完後一律呼叫 `evt.Skip()`，讓事件繼續往下傳給當時取得焦點的視窗；過去這個「跳轉到指定層數」的觸發本來就會同時發生，只是被 `export_gcode_3mf()` 跳出的 modal 另存對話框搶走焦點、擋住畫面，才讓人誤以為只有「另存」被觸發。修補讓另存流程不再啟動後，這個原本就存在的既有行為就正常顯現，並非本次改動新增或誤動的熱鍵。

## Risks / Trade-offs

- [Risk] 誤把 SLA 分支裡其他無關按鈕（如 `print_multi_machine_btn`、`send_to_printer_btn` 系列）一併影響到，導致 FDM 行為跟著改變 → Mitigation：改動嚴格限定在 `else` (SLA) 分支內新增/移除 append 呼叫，FDM 分支（`if (bIsFDMMode)`）程式碼區塊不做任何修改，並於實作後以「Resin/SLA 模式」與「FDM 模式」兩者分別手動驗證選單內容。
- [Risk] `bIsPhrozenVender` 目前被硬編碼為 `true`（2124 行），代表 `if (... !(is_bbl_vendor || bIsPhrozenVender))` 分支（2128-2156 行，ThirdParty Buttons）在現行程式碼路徑下永遠不會執行；若未來移除這行硬編碼，需重新檢視 ThirdParty 分支是否也要套用相同的 SLA-only-PRZ 邏輯 → Mitigation：本次改動只處理目前實際會執行到的 Phrozen 分支（2157 行以後），不動 ThirdParty 分支；在 design 中記錄此既有技術債，不在本次範圍內處理。
- [Trade-off] 開發者模式（`IsPhrozenDeveloperMode()`）下的「Print」（send_gcode）按鈕在 SLA 模式下也會被隱藏，即使原本它與匯出格式無關 → 可接受，因需求明確指出「Resin/SLA 模式下選單只保留匯出 PRZ 檔案」，且此按鈕與其他匯出格式屬同一個下拉選單/情境。
- [Risk][已確認發生並修復] 原本預期「Export plate sliced file」的 accelerator（`\tCtrl+G`）會隨 `wxMenu::Remove()` 一併失效，但手動驗證（task 4.4）實測發現 `Ctrl+G` 在 SLA 模式下仍會觸發匯出（表現為「另存 .3mf」）。根因並非 accelerator table 快取問題，而是 [MainFrame.cpp:858](src/slic3r/GUI/MainFrame.cpp) 有一段完全獨立於 `export_menu` 的原始鍵盤 handler，只檢查切片是否完成、從未檢查 printer technology，見 D5 → Mitigation：已在 D5 修補該行，加上 `wxGetApp().get_ui_printer_technology() == ptFFF` 判斷；tasks.md 新增 3.6（程式碼修補）與更新 4.4（需重新驗證修補後行為）。
- [Risk] 三個項目的 `cb_condition`（`can_export_gcode()`/`can_export_all_gcode()`）在項目被 `Remove()` 期間不會被求值，重新 `Insert()` 回來後由既有的 `wxEVT_UPDATE_UI` 機制接手，理論上會自動恢復正確的 enable/disable 狀態 → Mitigation：驗證階段確認 FDM 模式下重新顯示後，這三項的 enable/disable 狀態（切片完成前後）與修改前一致。

## Open Questions

無——需求與現有程式碼路徑均明確，且有 `m_slice_option_btn`（右上角下拉選單）與已封存的 `2026-09-01-hide-duplicate-plate-sla`（File/Edit 類單次建立 menu 的 Remove/Insert 手法）兩個既有先例可直接參照實作方式。
