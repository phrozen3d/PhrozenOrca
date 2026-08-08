# sla-support-point-top-field-display Specification

## Purpose
TBD - created by archiving change fix-sla-support-top-config-enum-set. Update Purpose after archive.
## Requirements
### Requirement: per-point 參數投影必須完整執行

`support_top_apply_point()` SHALL 將全部七個 Top 欄位寫入目標 `DynamicPrintConfig`，且 SHALL NOT 因任何 option 的型別而中途拋出例外。

對每個 key，寫入方式 SHALL 與該 option 的實際型別相符：`coFloat` 使用 `ConfigBase::set(key, double)`，`coEnum` SHALL 使用具型別的寫入路徑（例如 `set_key_value()` 搭配對應的 `ConfigOptionEnum<T>`），SHALL NOT 依賴 enum 到 `int` 的隱式轉換去呼叫 `ConfigBase::set(key, int)`。

`support_top_config_from_selection()` SHALL 回傳一份七個 key 皆已反映選中點的 config。

#### Scenario: contact type 為 Sphere 的點

- **GIVEN** 編輯模式，選中一顆 `contact_sphere_radius > 0` 的支撐點
- **WHEN** 呼叫 `support_top_config_from_selection()`
- **THEN** 回傳的 config 中 `support_contact_type` 為 `spSphere`
- **AND** 其餘六個 Top key 皆反映該點的值
- **AND** 不拋出 `BadOptionTypeException`

#### Scenario: contact type 為 None 的點

- **GIVEN** 編輯模式，選中一顆未啟用 contact sphere 的支撐點
- **WHEN** 呼叫 `support_top_config_from_selection()`
- **THEN** 回傳的 config 中 `support_contact_type` 為 `spNone2`
- **AND** 其餘六個 Top key 皆反映該點的值

#### Scenario: 未選取任何點

- **GIVEN** 編輯模式但無選取點
- **WHEN** 呼叫 `support_top_config_from_selection()`
- **THEN** 回傳的 config 等同 `sla_process_config()`，不套用任何 per-point 覆寫

### Requirement: 選取支撐點時 Top 欄位顯示該點的值

選取一顆支撐點時，系統 SHALL 將 Top 群組的七個欄位切換為顯示該點的 per-point 值。取消選取時 SHALL 還原為 preset 顯示。

欄位回填 SHALL NOT 被誤判為使用者編輯，SHALL NOT 因此反過來修改該支撐點的參數。

#### Scenario: 選取點後欄位反映該點

- **GIVEN** 編輯模式，存在兩顆 `support_head_front_diameter` 不同的手動支撐點
- **WHEN** 使用者點選其中一顆
- **THEN** Top 群組的 `Upper Diameter` 欄位顯示該點的值，而非 preset 值
- **AND** 改選另一顆後欄位隨之更新為後者的值

#### Scenario: 回填不觸發回寫

- **GIVEN** 編輯模式
- **WHEN** 使用者點選一顆支撐點，欄位完成回填
- **THEN** 該支撐點的參數與點選前完全相同
- **AND** 不產生新的 undo 快照

#### Scenario: 取消選取還原 preset 顯示

- **GIVEN** 編輯模式且已選取一顆支撐點，欄位顯示該點的值
- **WHEN** 使用者取消選取
- **THEN** Top 欄位還原為顯示 SLA print preset 的值

#### Scenario: SpinCtrl 型欄位不因非同步事件破壞 guard

- **GIVEN** Top 群組中以 SpinCtrl 呈現的欄位
- **WHEN** 選取支撐點觸發欄位回填
- **THEN** SpinCtrl 在 `set_value()` 之後送出的變更事件不被視為使用者編輯
- **AND** 該支撐點的參數不被修改

### Requirement: 編輯 Top 欄位時回寫選中點並重畫視圖

當存在選中的支撐點且使用者修改任一 Top 欄位時，系統 SHALL 將新值寫入選中點，SHALL 以該點的最新狀態回填欄位，且 SHALL 將 3D 視圖標記為需要重畫。

上述三個動作 SHALL 全部完成；SHALL NOT 因中間步驟拋出例外而略過重畫。

#### Scenario: 修改選中點的上部直徑

- **GIVEN** 編輯模式且已選取一顆支撐點
- **WHEN** 使用者在 Top 群組修改 `support_head_front_diameter`
- **THEN** 該點的 `head_front_radius` 更新為新值的一半
- **AND** 3D 視圖被標記為需要重畫，preview 錐體隨之更新
- **AND** 該編輯 SHALL NOT 寫入 SLA print preset

#### Scenario: 修改 contact type 下拉選單

- **GIVEN** 編輯模式且已選取一顆支撐點
- **WHEN** 使用者將 `support_contact_type` 由 `None` 改為 `Sphere`
- **THEN** 該點的 `contact_sphere_radius` 依既有規則更新
- **AND** 欄位回填後仍顯示 `Sphere`
- **AND** 3D 視圖被標記為需要重畫

### Requirement: per-point Top 編輯不得使應用程式終止

選取支撐點並編輯 Top 欄位的完整流程 SHALL NOT 使應用程式終止。

per-point Top 投影邏輯所拋出的任何例外 SHALL NOT 傳播至 UI thread 的主迴圈。`BadOptionTypeException` 的繼承鏈包含 `Slic3r::CriticalException`，依其定義即不得傳到 UI thread；一旦傳到，`generic_exception_handle()` 會記錄後重新拋出，使 `OnExceptionInMainLoop()` 無法正常返回，應用程式隨即終止。

`support_top_config_from_selection()` 的呼叫點 SHALL 具備例外邊界，使同類錯誤降級為「該次欄位更新失敗並記錄」而非致命。

#### Scenario: 選取點後修改欄位並失焦

- **GIVEN** 編輯模式且已選取一顆支撐點
- **WHEN** 使用者修改 `support_head_front_diameter` 並使欄位失焦
- **THEN** 應用程式繼續執行，不終止
- **AND** 該編輯正確套用至選中點
- **AND** 3D 視圖被標記為需要重畫

#### Scenario: 反覆選取與編輯

- **GIVEN** 編輯模式，存在若干參數各異的支撐點
- **WHEN** 使用者反覆選取不同的點並修改其 Top 參數
- **THEN** 應用程式在整個過程中不終止

#### Scenario: 投影邏輯拋出時不致命

- **GIVEN** per-point Top 投影邏輯因任何原因拋出例外
- **WHEN** 該例外於 `support_top_config_from_selection()` 的呼叫點被處理
- **THEN** 例外不傳播至 wx 主迴圈
- **AND** 應用程式繼續執行

### Requirement: 正常操作下不得拋出 BadOptionTypeException

Points preview 與 Top 欄位顯示路徑在正常操作下 SHALL NOT 拋出 `BadOptionTypeException`。

#### Scenario: 選取與編輯支撐點的例外計數

- **GIVEN** 編輯模式，存在若干支撐點
- **WHEN** 使用者依序選取數顆點並修改其 Top 參數
- **THEN** 整個過程中 `BadOptionTypeException` 的拋出次數為 0

#### Scenario: 取消選取不觸發投影

- **GIVEN** 編輯模式且已選取一顆支撐點
- **WHEN** 使用者點選空白處取消選取
- **THEN** 不呼叫 per-point 投影邏輯
- **AND** 不拋出任何例外

