## ADDED Requirements

### Requirement: PRZ header `aaLevel` 欄位 SHALL 以採樣等級索引映射表寫入

`prz_header()` 寫入 PRZ header 的 `aaLevel` 欄位（2 bytes big-endian short）時，SHALL 將 `anti_aliasing_level` 設定值視為「採樣等級索引」，並依下表映射為「每軸 AA 採樣像素數」後寫入；不得直接寫入原始整數值。

| `anti_aliasing_level` 設定值 | PRZ `aaLevel` 寫入值 | 語意 |
|---|---|---|
| 0 | 2 | Low |
| 1 | 4 | Mid（預設） |
| 2 | 8 | High |
| 其他值（含 ≥3、負值或未列舉值） | 4 | 安全 fallback 至 Mid |

此映射僅影響 PRZ 寫入端，不影響 `SLAPrintSteps.cpp` 內 `aa_steps` 計算或其他切片器格式輸出。

#### Scenario: 設定值 1 寫入 aaLevel = 4

- **WHEN** profile `anti_aliasing_level = 1`，匯出 PRZ
- **THEN** PRZ header 的 `aaLevel` 欄位寫入 `4`

#### Scenario: 設定值 2 寫入 aaLevel = 8

- **WHEN** profile `anti_aliasing_level = 2`，匯出 PRZ
- **THEN** PRZ header 的 `aaLevel` 欄位寫入 `8`

#### Scenario: 設定值 0 寫入 aaLevel = 2

- **WHEN** profile `anti_aliasing_level = 0`，匯出 PRZ
- **THEN** PRZ header 的 `aaLevel` 欄位寫入 `2`

#### Scenario: 未列舉的設定值 fallback 至 Mid

- **WHEN** profile `anti_aliasing_level` 為 `5`（未列舉值）或負值，匯出 PRZ
- **THEN** PRZ header 的 `aaLevel` 欄位寫入 `4`（Mid fallback）

---

### Requirement: PRZ header 與 layer content 的 `*_second_speed` 在 `*_second_distance == 0` 時 SHALL 強制輸出 `0.f`

`prz_header()` 與 `prz_layer_content()` 寫入以下四組「第二段升降速度」欄位時，SHALL 先檢查對應的 distance 欄位是否為 `0.f`；若為 `0.f`，則 speed 欄位 SHALL 強制輸出 `0.f`，不得寫入 cfg 內的非零預設值。

對應關係：

| Distance cfg key | Speed cfg key |
|---|---|
| `bottom_lift_second_distance` | `bottom_lift_second_speed` |
| `lift_second_distance` | `lift_second_speed` |
| `bottom_retract_second_distance` | `bottom_retract_second_speed` |
| `retract_second_distance` | `retract_second_speed` |

判斷條件 SHALL 為精確相等（`dist == 0.f`），不使用浮點容差。

#### Scenario: 第二段升降距離為 0 時 header speed 寫入 0

- **WHEN** profile `bottom_lift_second_distance = 0` 且 `bottom_lift_second_speed = 45`，匯出 PRZ
- **THEN** PRZ header 的 `BottomLift_second_Speed` 欄位寫入 `0.f`

#### Scenario: 第二段升降距離非 0 時 header speed 寫入 cfg 值

- **WHEN** profile `lift_second_distance = 5` 且 `lift_second_speed = 60`，匯出 PRZ
- **THEN** PRZ header 的 `Lift_second_Speed` 欄位寫入 `60.f`

#### Scenario: 四組 distance/speed 對應皆受保護

- **WHEN** profile 四組 `*_second_distance` 皆為 `0`，且四組 `*_second_speed` 皆為非零預設值
- **THEN** PRZ header 中所有四組 `*_second_Speed`（bottom_lift / lift / bottom_retract / retract）皆寫入 `0.f`

#### Scenario: 每層 layer content 的 second_speed 同樣受保護

- **WHEN** profile `retract_second_distance = 0` 且 `retract_second_speed = 150`，匯出 PRZ
- **THEN** 每層 layer content 的 `Retract_Second_Speed` 欄位皆寫入 `0.f`

#### Scenario: 底層與一般層分別套用對應 distance/speed 對

- **WHEN** profile `bottom_retract_second_distance = 0`、`bottom_retract_second_speed = 150`、`retract_second_distance = 3`、`retract_second_speed = 80`，匯出 PRZ
- **THEN** 底層（index < bottom_layer_count）layer content 的 `Retract_Second_Speed` 寫入 `0.f`，一般層的 `Retract_Second_Speed` 寫入 `80.f`

---

### Requirement: PRZ header `fileTime` 欄位 SHALL 保留 Orca 既有真實時間戳輸出

`prz_header()` 寫入 `fileTime` 欄位時，SHALL 維持現有 `YYYY-MM-DD HH:MM:SS` 真實本地時間戳寫入邏輯，不對齊 Chitubox 輸出的 `"0"` 或其他固定字串。

理由：`fileTime` 對使用者除錯、檔案歸檔、版本追蹤有實質價值，且 Phrozen 韌體不解析此欄位，無相容性問題。

#### Scenario: PRZ header 含有真實切片時間戳

- **WHEN** 在本地時間 `2026-05-25 14:30:00` 匯出 PRZ
- **THEN** PRZ header 的 `fileTime` 欄位寫入 `"2026-05-25 14:30:00"`（以 24 bytes 字元字串填充，尾端補 `\0`）

#### Scenario: fileTime 不因「對齊 Chitubox」要求而被改寫為 0

- **WHEN** 本變更實作完成後檢視 [src/libslic3r/Format/PhrozenPRZ.cpp](src/libslic3r/Format/PhrozenPRZ.cpp) 寫入 `fileTime` 的程式碼區段
- **THEN** 寫入邏輯仍呼叫 `time()` / `localtime()` 取得當下本地時間並格式化為 `YYYY-MM-DD HH:MM:SS`，無「寫入 `"0"` 或空字串」的分支

---

### Requirement: PRZ header 的 `software` 與 `softwareVersion` 欄位 SHALL 寫入有意義版本字串

`prz_header()` 寫入 `software`（32 bytes）與 `softwareVersion`（24 bytes）兩欄位時，SHALL 寫入可辨識 Phrozen Orca 來源的版本字串，不得保留空字串輸出。

字串來源（具體值於 tasks 階段最終決定）：
- `software`：寫入辨識字串（如 `"PhrozenOrca"` 或 Orca 主版本字串）
- `softwareVersion`：寫入版本號（如 Orca 內部 `SLIC3R_VERSION` 巨集值）

字串長度超過欄位寬度時 SHALL 截斷至欄位寬度；不足時尾端以 `\0` 填充。

#### Scenario: software 欄位非空

- **WHEN** 匯出 PRZ
- **THEN** `software` 欄位前綴含非 `\0` 字元，可被 hex dump 或檔頭解析工具辨識為有意義字串

#### Scenario: softwareVersion 欄位非空

- **WHEN** 匯出 PRZ
- **THEN** `softwareVersion` 欄位前綴含非 `\0` 字元，且能對應到 Phrozen Orca 的版本號

---

### Requirement: PRZ header 的 `priceUnit` 欄位 SHALL 寫入單位字串

`prz_header()` 寫入 `priceUnit`（8 bytes）欄位時，SHALL 寫入 `"$/L"` 字串，尾端以 `\0` 填充至 8 bytes；不得保留現有的全 `\0` 輸出。

#### Scenario: priceUnit 寫入 "$/L"

- **WHEN** 匯出 PRZ
- **THEN** `priceUnit` 欄位的前 3 bytes 為 ASCII `$`、`/`、`L`，後 5 bytes 為 `\0`
