## Context

Edit menu（`editMenu`）只在 `MainFrame::init_menubar_as_editor()` 中建立一次（於啟動時呼叫一次，見 `MainFrame.cpp:550`）。在 Windows/Linux（`#ifndef __APPLE__` 分支，`MainFrame.cpp:2989`）中，它會被加入自訂的 `BBLTopbar` 下拉選單作為 submenu（`m_topbar->AddDropDownSubMenu(editMenu, _L("Edit"))`，透過 `BBLTopbar::OnDropdownToolItem` → `GetParent()->PopupMenu(&m_top_menu, ...)` 彈出，其中 `GetParent()` 即為 `MainFrame`）。在 macOS（`#else` 分支，`MainFrame.cpp:3092`）則是把同一個項目加入原生的 `m_menubar`。由於同一次編譯只會有一個分支生效，執行期只會存在單一個 "Duplicate Current Plate" 的 `wxMenuItem*`。

因為這個 menu 從未被重建，`append_menu_item()` 的 `cb_condition` 參數（透過 `enable_menu_item()` 綁定到 `wxEVT_UPDATE_UI`，見 `wxExtensions.cpp:47-92`）只能切換「啟用/停用（灰階）」，無法把項目從 menu 中移除。這正是姊妹能力 `sla-ui-feature-gating` 對 Add-Primitive 的 "Text" 項目所採用的機制——當初刻意接受「可見但 disabled」這個結果（見已封存的 `hide-sla-inapplicable-ui-actions` design，決策 D4），因為那個情境並不需要真正移除。

本次需求明確要求相反的效果：在 Resin/SLA 模式下該項目必須完全不顯示，而不只是 disabled。若「Duplicate Current Plate」顯示但 disabled，相較於原始需求會是一種退步（已封存變更中的 Filament-menu 項目雖然也是真正隱藏，但那是利用每次開啟都會重新建立 menu 的時機做 early-return；Edit menu 的建立方式並非如此，只會建立一次）。

## Goals / Non-Goals

**Goals:**
- 當 `wxGetApp().get_ui_printer_technology() == ptSLA` 時，將 "Duplicate Current Plate" 這個 `wxMenuItem` 從 Edit menu 中完全隱藏（不留下灰階殘影）。
- 當 technology 為 `ptFFF` 時，維持完全可見且可用，與現況完全一致。
- 每次開啟 menu 時重新評估，而非只在應用程式啟動時判斷一次——與同一檔案中幾百行之前既有的 "Slice all" resin 模式隱藏邏輯（`MainFrame.cpp:2078-2080`，同樣以 `wxGetApp().get_ui_printer_technology() == ptFFF` 為判斷依據）保持一致的作法與資料來源。
- 只改動這一個 menu item；不影響 Edit menu 中其他項目、除了緊鄰的 separator 之外的版面配置，也不影響其他可能直接呼叫 `duplicate_plate()` 的入口（例如 toolbar、快捷鍵）。

**Non-Goals:**
- 在 printer technology 切換時重建整個 `editMenu`（或整個 menu bar）。
- 變更 `Plater::duplicate_plate()` 本身，或移除 FDM 使用者可用此功能的能力。
- 重構通用的 `append_menu_item()` / `enable_menu_item()` helper，讓它們支援正式的「可見性」callback——這是單一項目、局部性質的修正。
- 為其他 menu/toolbar 新增 "Duplicate Current Plate" 相關的顯示條件；此變更範圍僅限於截圖中所示的 Edit menu 項目。

## Decisions

### D1：透過 `wxMenu::Remove()` / `wxMenu::Insert()` 切換可見性，並由 `wxEVT_MENU_OPEN` 觸發評估（不過濾特定 menu）

**Choice**：將該項目的 `wxMenuItem*`、其緊接在後的 separator（`wxMenuItem*`），以及該項目原始插入位置（index）保存為 `MainFrame` 的 member。在 `MainFrame` 上綁定 `wxEVT_MENU_OPEN`；每次收到此事件就重新評估並視需要移除/插回「項目 + 其後的 separator」這一組，若目前為 SLA 模式且兩者仍在 menu 中就一併移除，若為 FFF 模式且兩者已被移除就依序重新插回（先插項目到原始 index，再插 separator 到 index+1，確保 separator 精準落在項目正後方），然後呼叫 `evt.Skip()`。

**Rationale**：`wxMenu::Remove(item)` 會把 `wxMenuItem` 從 `wxMenu` 中卸下但不會刪除它（所有權回到呼叫端），`wxMenu::Insert(pos, item)` 則能重新掛回——這是 wx 中針對「只建立一次的 menu」做動態顯示/隱藏的標準作法。實作時原本嘗試以 `evt.GetMenu() == editMenu` 過濾，但實測發現：BBLTopbar 的下拉選單是透過 `PopupMenu(&m_top_menu, ...)` 顯示最外層的 `m_top_menu`，`wxEVT_MENU_OPEN` 只會在「最外層 popup 開啟」時觸發一次（`GetMenu()` 回傳 `m_top_menu`），滑鼠 hover 展開 `editMenu` 這個 submenu 時並不會再產生一次獨立的 `wxEVT_MENU_OPEN` 給 `editMenu` 本身——若堅持過濾特定 menu，判斷式永遠不成立，項目永遠不會被隱藏。因此改為：只要收到任何 `wxEVT_MENU_OPEN` 就重新評估一次（呼叫 `update_duplicate_plate_menu_item_visibility()`），此函式內部仍然只針對 `editMenu`／該項目做操作，且是廉價、幂等（idempotent）的檢查，不會因為多次呼叫而產生副作用。已透過實際建置後手動操作驗證：SLA 模式下開啟 Edit menu 不再出現該項目，FDM 模式下開啟則正常顯示且可點擊，並且透過應用程式內建的 Resin/FDM 模式切換按鈕在不重啟的情況下切換兩次，行為皆正確更新。

**Alternatives considered**：
- *沿用 `cb_condition`/`wxEVT_UPDATE_UI` 做 `Enable(false)`*：與既有的 "Text" 先例一致，但只會讓項目變灰階，並非本次需求要的效果。
- *在 printer technology 每次切換時重建 `editMenu`*：結果正確，但影響範圍大得多（需要新增一個切換 technology 時重建 menu 內容的 hook，可能波及 Edit menu 中其他項目與 separator），對於一個單一項目的需求來說風險過高；已封存的 design 文件在更小的需求下也已否決過這個作法（D4）。
- *在 menu 建立時就 early-return（比照 Filament-menu 的 D5 先例）*：在此不適用，因為與 Filament fix 所處理的 context menu 不同，`editMenu` 只在啟動時建立一次——early-return 只會反映啟動當下的 technology，之後永遠不會再更新。

### D2：以 `wxGetApp().get_ui_printer_technology()` 作為判斷依據

**Choice**：以 `wxGetApp().get_ui_printer_technology() == ptSLA`（隱藏）／`== ptFFF`（顯示）作為判斷條件，與同一檔案中幾百行之前用來在 resin 模式隱藏 "Slice all" 的作法一致。

**Rationale**：同一檔案中，針對同一類「resin 模式下隱藏某控制項」的決策，維持一致性。`GUI_Factories.cpp` 的 context menu 判斷則是使用 `get_edited_preset().printer_technology()`，但該檔案自己的 design 文件（D6）說明這個選擇是為了與 `GLGizmosManager` 的 gizmo/menu 一致性有關，並不適用於 `MainFrame.cpp` 中頂層 Edit menu 項目的情境。

## Risks / Trade-offs

**[Resolved] 隱藏後曾多出一條多餘的 separator**：此項目位於兩個 `editMenu->AppendSeparator()` 之間（Clone selected 後的 separator、本項目、Select All 前的 separator）。初版實作只移除項目本身，讓兩個 separator 同時留下，在 SLA 模式下的 Edit menu 會在該位置呈現兩條相鄰的分隔線（手動測試截圖證實）。修正方式：把緊接在項目之後的那個 separator 也視為同一組一併追蹤與切換——SLA 模式下與項目一起移除，FFF 模式下依「先項目、後 separator」的順序重新插回原位置，使 FDM 模式下的外觀與修改前完全一致，SLA 模式下該位置只留下一條 separator（即 Clone selected 後方原有的那條）。已重新建置並手動截圖確認兩種模式下都不再出現雙重分隔線。

**[Low] `wxEVT_MENU_OPEN` 在不同平台上的傳遞行為略有差異**：wx 官方文件提到這個事件實際會傳遞到哪個視窗，在不同平台上略有差異。緩解方式：將綁定放在 `MainFrame` 上（Windows/Linux topbar 路徑中 `PopupMenu()` 呼叫的對象視窗，以及 macOS 上原生 menu bar 所屬的 frame），這是 wx 應用程式中處理此事件時常見且可攜性較好的作法。

## Migration Plan

不適用——僅為附加性質、僅存在於記憶體中的 UI 行為調整；不涉及任何持久化資料、設定檔或 project 檔案格式的變更。回退方式僅需還原此次程式碼變更即可。
