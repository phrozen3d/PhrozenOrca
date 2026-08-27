# sla-support-head-penetration Specification

## Purpose
SLA 支撐頭刺入模型的深度必須依該點的局部可用深度動態夾限，使支撐頭不會貫穿薄壁而在承載面另一側露出。涵蓋夾限式、Contact Sphere 的最深點換算、沿頭軸的量測方式、射線無命中時的 fail-safe，以及四個夾限提交點的語意與時序。

## Requirements

### Requirement: 支撐頭刺入深度必須依局部可用深度動態夾限

切片端支撐頭的有效刺入深度 MUST 依承載面的局部可用深度動態夾限。夾限的對象 MUST 為 **front depth**（自模型接觸點沿 `-Head::dir` 進入模型的深度，即 `point_contact_front_depth_mm()` 的語意），MUST NOT 直接夾限 `Head::penetration_mm`。

夾限式：

```
offset        = (r_contact > EPSILON && r_contact <= r_pin) ? (r_pin - r_contact) : 0
front_clamped = clamp(min(configured_front, local_thickness * 0.5 - offset), 0, configured_front)
```

其中 `configured_front` 為該支撐點解析後的 front depth（per-point 覆寫值優先，否則為 `support_head_penetration`；Branching/Organic 樹讀取 preset），`local_thickness` 為沿支撐頭軸線量得的可用深度。

夾限結果 MUST 經 `point_head_penetration_mesh_mm()` 換算後寫入該支撐頭的 `Head::penetration_mm` 成員，使所有依賴刺入深度的下游計算（`fullwidth()`、`junction_point()`）自動保持一致。系統 MUST NOT 以修改 `SupportTreeMesher` 網格生成邏輯的方式達成防貫穿。

本需求採「上表面零凸點優先」政策：明確接受在極薄物件上因咬合深度不足而於列印時脫落的物理代價。

#### Scenario: 刺入深度大於可用深度

- **GIVEN** 厚 0.2 mm 的水平薄板
- **AND** `support_head_penetration = 0.3`，未啟用 Contact Sphere
- **WHEN** 於下表面放置支撐頭
- **THEN** 有效 front depth MUST 為 0.1（= 0.2 × 0.5）
- **AND** 支撐頭幾何 MUST NOT 突出於上表面之上

#### Scenario: 刺入深度小於可用深度一半

- **GIVEN** 厚 10 mm 的模型
- **AND** `support_head_penetration = 0.4`
- **WHEN** 於承載面放置支撐頭
- **THEN** 有效 front depth MUST 為 0.4（夾限失效，維持設定值）

#### Scenario: 支撐幾何與模型的布林交集驗證

- **GIVEN** 任意模型於任意 `support_head_penetration` 設定下完成支撐生成
- **WHEN** 將支撐網格與模型網格取布林交集，並檢查承載面另一側
- **THEN** 承載面另一側 MUST NOT 存在任何支撐幾何

---

### Requirement: Contact Sphere 的最深點換算必須正確

`Head::penetration_mm` 是**網格空間值**，當 `r_contact > EPSILON` 時由 `point_head_penetration_mesh_mm()` 換算為 `front + r_pin - r_contact`。支撐頭的實際最深點依 Contact Sphere 組態而異，夾限 MUST 依實際最深點計算。

三種組態的實際最深點：

| 組態 | `Head::penetration_mm` | 實際最高幾何 | 最深點 |
|---|---|---|---|
| 無接觸球（`r_contact <= EPSILON`） | `front` | pin 球頂 | `front` |
| 接觸球有效（`r_contact > r_pin`） | `front + r_pin - r_contact` | 接觸球頂 | `front` |
| 退化帶（`0 < r_contact <= r_pin`） | `front + r_pin - r_contact` | pin 球頂 | `front + r_pin - r_contact` |

退化帶的成因是 `point_head_penetration_mesh_mm()` 只要 `r_contact > EPSILON` 即無條件換算，而 `head_mesh_body()` 僅在 `r_contact > r_pin` 時才附加接觸球。此不一致為既有行為，本能力 MUST NOT 修正它，但夾限 MUST 以 `offset` 項涵蓋它。

#### Scenario: 啟用接觸球時的夾限

- **GIVEN** 厚 0.2 mm 的薄板
- **AND** `support_head_penetration = 0.3`、`r_pin = 0.2`、`r_contact = 0.4`（接觸球有效）
- **WHEN** 放置支撐頭
- **THEN** 有效 front depth MUST 為 0.1
- **AND** `Head::penetration_mm` MUST 為 `0.1 + 0.2 - 0.4 = -0.1`
- **AND** 接觸球球頂 MUST 位於 z = 0.1，MUST NOT 突出於上表面之上

#### Scenario: 退化帶的夾限

- **GIVEN** 厚 0.2 mm 的薄板
- **AND** `r_pin = 0.3`、`r_contact = 0.1`（`0 < r_contact <= r_pin`，接觸球不生成）
- **AND** `support_head_penetration = 0.3`
- **WHEN** 放置支撐頭
- **THEN** `offset` MUST 為 `0.3 - 0.1 = 0.2`
- **AND** 有效 front depth MUST 為 `clamp(min(0.3, 0.1 - 0.2), 0, 0.3) = 0`
- **AND** 支撐頭幾何 MUST NOT 突出於上表面之上

#### Scenario: 未啟用接觸球時換算為恆等

- **GIVEN** `support_contact_type` 非 Sphere，`r_contact = 0`
- **WHEN** 施加夾限
- **THEN** `offset` MUST 為 0
- **AND** `Head::penetration_mm` MUST 等於夾限後的 front depth

---

### Requirement: 局部可用深度必須沿支撐頭軸線以單位方向向量量測

局部可用深度 MUST 沿支撐頭的軸線量測，入模方向定義為 `-Head::dir`。量測 MUST 使用射線查詢，且傳入的方向向量 MUST 為單位向量。

射線起點 MUST 自接觸點沿**入模方向**踏入材料內部一個極小量 `eps` 以避免自我命中，量得距離後 MUST 加回 `eps`。`eps` MUST 遠小於最薄可列印壁厚。

系統 MUST NOT 將起點沿反入模方向退到模型外側——射線查詢無正/反面過濾，命中的是路徑上第一個三角形，自外側起算會恆得 `2 * eps` 而與真實壁厚無關。

系統 MUST NOT 沿表面法線量測可用深度——刺入深度定義於支撐頭軸線上，故軸向距離才是正確的可用深度。

量測所用網格 MUST 為 `SLAPrintObject::get_mesh_to_print()` 導出的 `m_supportdata->emesh`，即已套用中空與鑽孔的網格。

#### Scenario: 傾斜支撐頭的軸向量測

- **GIVEN** 支撐頭軸線與承載面法線夾角不為零
- **WHEN** 量測局部可用深度
- **THEN** 量得的值 MUST 為沿頭軸的距離，而非垂直壁厚
- **AND** 夾限結果 MUST NOT 較垂直壁厚計算更為保守

#### Scenario: 射線自側緣出射

- **GIVEN** 傾斜的支撐頭，其軸線延伸後自薄板側緣出射而非自上表面出射
- **WHEN** 量測局部可用深度
- **THEN** 量得的值 MUST 為至側緣出射點的軸向距離
- **AND** 夾限後的支撐頭尖端 MUST 留在材料內部

#### Scenario: 方向向量必須正規化

- **GIVEN** 任一支撐頭或錨點
- **WHEN** 執行可用深度量測
- **THEN** 傳入射線查詢的方向向量長度 MUST 為 1
- **AND** 此條件 MUST 在 Release 建置下亦成立，MUST NOT 僅依賴 debug 斷言

#### Scenario: 錨點方向向量正規化

- **GIVEN** `connect_to_model_body()` 由 `endp - hitp` 導出錨點方向
- **WHEN** 該方向被傳入射線查詢或 `add_anchor()`
- **THEN** 其長度 MUST 為 1

#### Scenario: 量測所用網格包含前置加工結果

- **GIVEN** 已啟用中空（hollowing）或已鑽排水孔的模型
- **WHEN** 量測局部可用深度
- **THEN** 量得的值 MUST 反映中空後的實際壁厚
- **AND** 鄰近排水孔的支撐點 MUST 量得縮減後的實際可用深度

---

### Requirement: 射線無命中時切片端必須採 Fail-safe

封閉流形網格上，自表面內側向內射出的射線必定命中。當射線無命中時（破損網格、非流形或自交），切片端 MUST 將該支撐頭的有效 front depth 設為 0。

當本階段有任何支撐頭觸發 fail-safe 時，系統 MUST 輸出彙總警告日誌，內容 MUST 包含觸發的支撐頭數量。每次支撐樹生成 MUST 最多輸出一行彙總，MUST NOT 逐點輸出。

#### Scenario: 破損網格觸發 fail-safe

- **GIVEN** 含破面或非流形區域的模型
- **AND** 某支撐點的入模射線無命中
- **WHEN** 計算該支撐頭的有效 front depth
- **THEN** 有效 front depth MUST 為 0

#### Scenario: Fail-safe 必須可診斷

- **GIVEN** 支撐生成過程中有 N 個支撐頭觸發 fail-safe
- **WHEN** 該次支撐樹生成完成
- **THEN** 系統 MUST 輸出恰好一行包含 N 的警告日誌
- **AND** 系統 MUST NOT 靜默地產出零咬合的支撐

#### Scenario: 無點觸發時不產生雜訊

- **GIVEN** 所有支撐頭的入模射線皆正常命中
- **WHEN** 支撐樹生成完成
- **THEN** 系統 MUST NOT 輸出該警告日誌

---

### Requirement: 夾限必須於角度搜尋完成之後施加

刺入深度的夾限 MUST 僅執行一次，且 MUST 在該支撐頭的所有角度搜尋（optimizer）完成之後、最終物件提交的當下施加。

角度搜尋過程 MUST 全程使用 `configured_front`，MUST NOT 因夾限而改變搜尋的初值、邊界或停止條件。

系統 MUST NOT 於 optimizer 的目標函式內執行可用深度量測射線。

#### Scenario: 搜尋行為不受夾限影響

- **GIVEN** 任一支撐頭或錨點的角度搜尋
- **WHEN** 執行 optimizer
- **THEN** optimizer 的初值、邊界與停止條件 MUST 以 `configured_front` 計算
- **AND** 搜尋所得的角度結果 MUST 與未套用夾限時完全相同

#### Scenario: 每個支撐頭僅量測一次

- **GIVEN** 單一支撐點的支撐頭放置已完成
- **WHEN** 統計該點的可用深度量測射線次數
- **THEN** 次數 MUST 為 1
- **AND** MUST NOT 隨 optimizer 的評估次數增加

---

### Requirement: 四個夾限提交點的語意與時序

夾限的提交點依支撐頭類型與樹種而異，共四處。主頭與錨點兩類提交點的語意不同，MUST NOT 互換。

**主頭（pinhead）**：提交點 MUST 位於 junction 與 pillar 計算之前——Default 樹於 `SupportTreeBuildsteps` 主頭接受區塊，Branching 樹於 `optimize_pinhead_placement()` 接受區塊。夾限 MUST 連帶更新 `fullwidth()` 與 junction 位置，使支撐頭與其 pillar 維持相連。

**錨點（anchor）**：提交點 MUST 位於錨點加入建構器（`add_anchor()`）的當下——Default 樹於 `connect_to_model_body()`，Branching 樹於 `BranchingTreeSLA` 的 `add_anchor()` 呼叫處。此時橋接端點已固定，夾限 MUST NOT 改變任何橋接或拓撲結構。

對於 Branching/Organic 樹，夾限 MUST 於 `add_anchor()` 呼叫處施加，MUST NOT 於讀取 `junction_point()` 建立橋接端點之前施加。

Default 樹錨點的寬度由包含刺入深度的距離導出（`dist = |hitp - endp| + penetration`，`w = dist - 2*r_pin - r_back`），施加夾限時 `w` MUST 依夾限後的值重新計算。

#### Scenario: 主頭夾限後與 pillar 維持相連

- **GIVEN** 主頭的刺入深度被夾限
- **WHEN** 計算其 junction 與 pillar
- **THEN** junction 位置 MUST 依夾限後的 `fullwidth()` 重新計算
- **AND** 支撐頭與 pillar 之間 MUST NOT 存在幾何間隙

#### Scenario: Branching 樹錨點夾限不改變橋接端點

- **GIVEN** Branching/Organic 樹的錨點已完成角度搜尋
- **WHEN** 施加刺入深度夾限
- **THEN** 用於建立橋接端點的 junction 位置 MUST 與未夾限時相同
- **AND** 橋接的可行性檢查結果 MUST 與未夾限時相同

#### Scenario: 錨點網格與橋接端重疊而非脫開

- **GIVEN** 錨點的刺入深度被夾限至小於設定值
- **WHEN** 產生錨點網格
- **THEN** 錨點網格 MUST 涵蓋橋接端點所在位置
- **AND** 錨點與橋接之間 MUST NOT 存在幾何間隙

#### Scenario: Default 樹錨點寬度與夾限後的深度一致

- **GIVEN** Default 樹的錨點寬度由包含刺入深度的距離導出
- **WHEN** 施加夾限
- **THEN** 錨點寬度 MUST 依夾限後的刺入深度重新計算

#### Scenario: 兩種樹皆套用夾限

- **GIVEN** 同一顆極薄模型
- **WHEN** 分別以 Default 樹與 Branching/Organic 樹生成支撐
- **THEN** 兩者的支撐頭與錨點皆 MUST NOT 突出於承載面另一側

---

### Requirement: 可用深度充足時夾限必須完全失效

當局部可用深度大於或等於 `configured_front` 的兩倍時（且 `offset` 為 0），夾限 MUST 不產生任何作用，支撐頭幾何 MUST 與未套用本能力時完全相同。

本需求的適用門檻為 `local_thickness >= 2 * configured_front`。可用深度落於 `(0, 2 * configured_front)` 區間的模型會被夾限影響，此為預期行為而非缺陷。本能力 MUST NOT 被描述為「常態零影響」，MUST 表述為「可用深度大於或等於 2 倍設定值時零影響」。

#### Scenario: 厚壁模型逐點一致

- **GIVEN** 所有支撐點的局部可用深度皆大於或等於 `2 * configured_front` 的模型
- **WHEN** 於變更前後分別執行支撐生成
- **THEN** 支撐網格 MUST 完全相同
- **AND** 每一個支撐頭的 front depth MUST 等於 `configured_front`

#### Scenario: 中間帶可用深度被夾限為預期行為

- **GIVEN** 局部可用深度 0.6 mm 的模型
- **AND** `support_head_penetration = 0.4`，未啟用 Contact Sphere
- **WHEN** 放置支撐頭
- **THEN** 有效 front depth MUST 為 0.3
- **AND** 此結果 MUST 被視為符合規格，而非回歸
