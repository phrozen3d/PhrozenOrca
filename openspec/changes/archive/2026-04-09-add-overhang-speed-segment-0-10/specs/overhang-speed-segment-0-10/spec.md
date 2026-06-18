## ADDED Requirements

### Requirement: 0-10% 懸空速度設定選項
系統 SHALL 提供 `overhang_0_4_speed` 設定選項（ConfigOptionFloatOrPercent），允許使用者指定 0%-10% 懸空範圍的列印速度。數值為 0 時表示不減速（使用 wall speed），數值為正時以 mm/s 或相對 outer_wall_speed 的百分比計算。

#### Scenario: 設為 0 時不減速
- **WHEN** 使用者將 `overhang_0_4_speed` 設為 0
- **THEN** G-code 生成時 0-10% 懸空區段使用 100% wall speed，與未啟用此選項行為相同

#### Scenario: 設為正值時套用慢速
- **WHEN** 使用者將 `overhang_0_4_speed` 設為正值（如 30 mm/s）
- **THEN** G-code 生成時 overlap >= 90% 的路徑段套用此速度

#### Scenario: 預設值行為
- **WHEN** profile 未包含 `overhang_0_4_speed` 欄位（舊 profile）
- **THEN** 系統使用預設值 0，行為與現有版本完全相同

### Requirement: GUI 顯示第五段速度輸入
系統 SHALL 在速度設定頁面（Overhang speed 群組）的 "Overhang speed" 行中，在現有四段前方顯示 `overhang_0_4_speed` 輸入欄，label 標示為 `[0%, 10%)`。

#### Scenario: 啟用懸空速度時顯示新欄位
- **WHEN** 使用者啟用 `enable_overhang_speed`
- **THEN** GUI 顯示包含 `overhang_0_4_speed` 在內的五個速度欄位

#### Scenario: 停用懸空速度時隱藏新欄位
- **WHEN** 使用者停用 `enable_overhang_speed`
- **THEN** `overhang_0_4_speed` 欄位連同其他四段欄位一起隱藏

### Requirement: G-code 生成正確套用第五段速度
系統 SHALL 在 GCode.cpp 的 `dynamic_overhang_speeds` 陣列第一個位置改為 `overhang_0_4_speed` 的條件式結構（值 < 0.5 時回退到 100%），適用於 `slowdown_for_curled_perimeters` 啟用與停用兩種模式。

#### Scenario: slowdown_for_curled_perimeters 啟用
- **WHEN** `slowdown_for_curled_perimeters = true` 且 `overhang_0_4_speed > 0`
- **THEN** 第一個 dynamic_overhang_speeds 條目使用 `overhang_0_4_speed` 的換算百分比值

#### Scenario: slowdown_for_curled_perimeters 停用
- **WHEN** `slowdown_for_curled_perimeters = false` 且 `overhang_0_4_speed > 0`
- **THEN** 第一個 dynamic_overhang_speeds 條目使用 `overhang_0_4_speed` 的換算百分比值

### Requirement: Profile JSON 新增預設值
系統 SHALL 在所有 Phrozen process profile JSON 中新增 `"overhang_0_4_speed": "0"` 欄位。

#### Scenario: 新 profile 包含第五段設定
- **WHEN** 使用者載入更新後的 Phrozen profile
- **THEN** `overhang_0_4_speed` 欄位值為 "0"，對應不減速行為
