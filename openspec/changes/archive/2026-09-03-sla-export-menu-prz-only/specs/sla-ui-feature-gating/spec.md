## ADDED Requirements

### Requirement: SLA 模式下右上角匯出下拉選單只顯示 Export PRZ file

當 active printer technology 為 SLA 時，右上角匯出下拉選單（`m_print_option_btn` 展開的 `SidePopup`）SHALL 只顯示「Export PRZ file」一個項目；「Print」（send to printer / send_gcode）、「Export plate sliced file」、「Export all sliced file」等其他既有匯出/傳送選項 SHALL NOT 出現於此選單中。下拉選單本身與右側展開箭頭按鈕 SHALL 維持顯示與可操作，不因選單只剩一個項目而被移除或停用。

#### Scenario: SLA printer — 展開匯出下拉選單只看到 Export PRZ file
- **WHEN** active printer technology 為 SLA
- **AND** 使用者點擊右上角匯出按鈕旁的展開箭頭
- **THEN** 下拉選單中 SHALL 只顯示「Export PRZ file」一個項目
- **AND** 「Print」、「Export plate sliced file」、「Export all sliced file」等其他項目 SHALL NOT 出現

#### Scenario: SLA printer — 展開箭頭維持顯示與可操作
- **WHEN** active printer technology 為 SLA
- **THEN** 右上角匯出按鈕旁的展開箭頭按鈕 SHALL 維持可見且可點擊
- **AND** 點擊該箭頭 SHALL 正常開啟只含「Export PRZ file」的下拉選單

#### Scenario: SLA printer — 點擊 Export PRZ file 行為不變
- **WHEN** active printer technology 為 SLA
- **AND** 使用者在下拉選單中點擊「Export PRZ file」
- **THEN** 主匯出按鈕 label SHALL 更新為「Export PRZ file」
- **AND** 點擊主匯出按鈕 SHALL 觸發與現行相同的 PRZ 匯出流程（`EVT_GLTOOLBAR_EXPORT_PRZ`）

#### Scenario: FDM printer — 匯出下拉選單維持現有行為
- **WHEN** active printer technology 為 FDM (ptFFF)
- **THEN** 右上角匯出下拉選單的項目內容與顯示邏輯 SHALL 維持現況（Export G-code file、Export plate sliced file、Export all sliced file 及其他既有條件式項目），不受本需求影響

#### Scenario: 切換 printer technology 後選單內容正確更新
- **WHEN** 使用者在應用程式啟動後從 FDM 切換至 SLA，或從 SLA 切換至 FDM
- **THEN** 下次展開右上角匯出下拉選單時，選單內容 SHALL 反映目前的 printer technology
- **AND** 選單內容 SHALL NOT 固定為應用程式啟動時的 printer technology

#### Scenario: 其他匯出功能本體不受影響
- **WHEN** active printer technology 為 SLA
- **THEN** `export_prz()` / `ExportPRZJob`、`export_gcode()`、`export_sliced_file` 等匯出功能本體 SHALL NOT 被移除或修改

---

### Requirement: SLA 模式下 File → Export 子選單隱藏切層格式相關項目

當 active printer technology 為 SLA 時，File 選單下的 Export 子選單（`export_menu`）中的「Export plate sliced file」「Export all plate sliced file」「Export G-code」三個項目 SHALL NOT 出現（完全從選單中移除，而非灰階 disabled），且這三個項目對應的匯出動作 SHALL NOT 能透過鍵盤快捷鍵（例如「Export plate sliced file」原本的 `Ctrl+G`）繞過選單觸發。同一子選單中的「Export all objects as one STL」「Export all objects as STLs」「Export Generic 3MF」「Export Preset Bundle」SHALL 維持顯示，不受本需求影響。當 active printer technology 為 FDM (ptFFF) 時，Export 子選單的所有項目與其鍵盤快捷鍵 SHALL 正常顯示並可正常使用，行為與現況相同。

#### Scenario: SLA printer — 切層格式相關的三個匯出項目不出現在 Export 子選單
- **WHEN** active printer technology 為 SLA
- **AND** 使用者開啟 File 選單並展開 Export 子選單
- **THEN** 「Export plate sliced file」「Export all plate sliced file」「Export G-code」三個項目 SHALL NOT 出現在 Export 子選單中

#### Scenario: SLA printer — 其餘 Export 子選單項目正常顯示
- **WHEN** active printer technology 為 SLA
- **AND** 使用者展開 Export 子選單
- **THEN** 「Export all objects as one STL」「Export all objects as STLs」「Export Generic 3MF」「Export Preset Bundle」SHALL 正常顯示於 Export 子選單中

#### Scenario: SLA printer — Ctrl+G 不觸發 Export plate sliced file
- **WHEN** active printer technology 為 SLA
- **AND** 使用者按下鍵盤快捷鍵 `Ctrl+G`（「Export plate sliced file」在 FDM 模式下的原始快捷鍵）
- **THEN** SHALL NOT 觸發 `EVT_GLTOOLBAR_EXPORT_SLICED_FILE` 或任何匯出/另存流程

#### Scenario: FDM printer — Export 子選單正常顯示且可用
- **WHEN** active printer technology 為 FDM (ptFFF)
- **AND** 使用者展開 Export 子選單
- **THEN** Export 子選單中原有的全部項目（含「Export plate sliced file」「Export all plate sliced file」「Export G-code」）SHALL 正常顯示於選單中
- **AND** 點擊各項目 SHALL 正常觸發對應的匯出行為，與現況相同
- **AND** 按下 `Ctrl+G` SHALL 正常觸發「Export plate sliced file」的匯出行為，與現況相同

#### Scenario: 切換 printer technology 後 Export 子選單顯示狀態正確更新
- **WHEN** 使用者在應用程式啟動後從 FDM 切換至 SLA，或從 SLA 切換至 FDM
- **THEN** 下次展開 Export 子選單時，「Export plate sliced file」「Export all plate sliced file」「Export G-code」三個項目的顯示/隱藏狀態 SHALL 反映目前的 printer technology
- **AND** 狀態 SHALL NOT 固定為應用程式啟動時的 printer technology

#### Scenario: Export 子選單以外的入口與匯出功能本體不受影響
- **WHEN** active printer technology 為 SLA 或 FDM
- **THEN** 右上角匯出下拉選單（`m_print_option_btn` / `SidePopup`）的顯示邏輯 SHALL 不受本需求影響，維持依其自身條件運作
- **AND** `export_stl()`、`export_core_3mf()`、`export_config()`、`export_gcode()`、`EVT_GLTOOLBAR_EXPORT_SLICED_FILE`、`EVT_GLTOOLBAR_EXPORT_ALL_SLICED_FILE` 等匯出功能本體與其他呼叫路徑 SHALL NOT 被移除或修改
