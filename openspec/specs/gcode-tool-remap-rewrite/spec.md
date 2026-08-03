# gcode-tool-remap-rewrite Specification

## Purpose
TBD - created by archiving change phrozen-filament-tool-remap. Update Purpose after archive.
## Requirements
### Requirement: 重寫裸工具切換指令

G-code 重寫 SHALL 將每一行「裸工具切換指令」（形如 `T<digit>`，可含前置空白與行尾註解）的工具號，依不可變映射表（原始工具號→目標工具號）替換為目標工具號。

#### Scenario: 將 T0 切換行重映射為 T2
- **GIVEN** 映射表為 {0→2, 2→0}
- **WHEN** 重寫遇到一行 `T0`
- **THEN** 輸出 SHALL 為 `T2`

#### Scenario: 保留行尾註解與格式
- **GIVEN** 映射表為 {0→3}
- **WHEN** 重寫遇到一行 `T0 ; change tool`
- **THEN** 輸出 SHALL 為 `T3 ; change tool`，且保留原始空白與註解

### Requirement: 重寫帶工具號的預熱／溫度指令

G-code 重寫 SHALL 同時處理 `M104` 與 `M109` 指令中的 `T<digit>` 參數，依同一映射表替換工具號，且 MUST NOT 修改溫度數值（`S` 參數）或其他參數。

#### Scenario: 重映射提早預熱指令
- **GIVEN** 映射表為 {0→2}
- **WHEN** 重寫遇到一行 `M104 S210 T0`
- **THEN** 輸出 SHALL 為 `M104 S210 T2`，溫度 `S210` 不變

#### Scenario: 容忍參數順序
- **GIVEN** 映射表為 {1→3}
- **WHEN** 重寫遇到一行 `M109 T1 S240`
- **THEN** 輸出 SHALL 為 `M109 T3 S240`

#### Scenario: 不更動不帶工具號的溫度指令
- **GIVEN** 任意映射表
- **WHEN** 重寫遇到一行 `M104 S210`（無 `T` 參數）
- **THEN** 該行 SHALL 原樣輸出

### Requirement: 單次前向掃描避免二次覆蓋

重寫 MUST 以單次前向掃描方式進行——讀取來源、逐行依「不可變」映射表決定輸出並寫入目的，每個工具號 token 僅查表一次；MUST NOT 採用連續多次字串取代。藉此確保互換型映射（如 T0↔T2）不會發生二次覆蓋。

#### Scenario: 互換型映射不發生二次覆蓋
- **GIVEN** 映射表為 {0→2, 2→0}，來源依序包含 `T0`、`T2`
- **WHEN** 完成單次前向掃描重寫
- **THEN** 輸出 SHALL 依序為 `T2`、`T0`（兩者正確互換，無任何一行被套用兩次）

### Requirement: 不誤傷非工具號內容

重寫 MUST 僅依整行語法形態辨識工具切換行與 `M104/M109` 的 `T` 參數；MUST NOT 變更座標、進給、註解文字或檔名中出現的字元（如 `X`/`Y` 後的數字、註解內的字母 T）。

#### Scenario: 不更動移動指令
- **GIVEN** 任意映射表
- **WHEN** 重寫遇到一行 `G1 X10 Y20 E0.5`
- **THEN** 該行 SHALL 原樣輸出

#### Scenario: 不更動註解中的字母
- **GIVEN** 任意映射表
- **WHEN** 重寫遇到一行 `; Tool change for part T0`
- **THEN** 該整行（純註解）SHALL 原樣輸出

### Requirement: 寫入旁路檔且永不修改原檔（保證冪等）

系統 MUST NOT 修改原始切片 G-code 檔。每一次重映射 MUST 以「未變動的原檔」為唯一來源，寫出至獨立的旁路檔 `<gcode>.remapped.gcode`。藉此保證使用者重按或改選後，結果永遠等於「原檔套用當前映射一次」,不發生疊加。（背景：原檔在送印當下被預覽 memory-mapped，Windows 上無法覆蓋——`MoveFileExW` 回 `ACCESS_DENIED`,原地覆寫亦不可行；故不碰原檔。）

#### Scenario: 原檔位元不變
- **WHEN** 完成一次重映射寫出旁路檔
- **THEN** 原始切片 G-code 檔 SHALL 維持位元不變

#### Scenario: 連續改選兩次仍正確
- **GIVEN** 第一次以映射 {0→2} 寫出旁路檔
- **WHEN** 使用者改為映射 {0→3} 並再次觸發改檔
- **THEN** 重寫 SHALL 以「原檔」為來源，輸出 T0→T3 的結果（而非在「已成 T2」的旁路檔上再次套用）

### Requirement: 旁路檔寫入與失敗清理

重寫 SHALL 直接寫出旁路檔。重寫失敗或取消時 MUST 刪除該（半套）旁路檔，且 MUST NOT 更動原檔。檔案開啟 MUST 使用 UTF-8 路徑安全的機制（`boost::nowide`），以支援含非 ASCII 字元的路徑。

#### Scenario: 取消時不留半套檔
- **GIVEN** 重寫進行到一半被取消
- **WHEN** 重寫中止
- **THEN** 系統 SHALL 刪除旁路檔，且原始 G-code 檔 SHALL 維持原狀不變

#### Scenario: 來源不存在時失敗且不留檔
- **WHEN** 來源 G-code 無法開啟
- **THEN** 系統 SHALL 回報錯誤且 MUST NOT 留下旁路檔

