# sla-support-point-placement Specification

## Purpose
SLA 自動支撐點在投影至模型表面時必須落在**朝下的承載面**，而非距離最近的面。確保極薄模型在任何切片網格相位下都能穩定產出支撐點。

## Requirements

### Requirement: 支撐點必須投影至朝下的承載面

自動產生的 SLA 支撐點由取樣層的 z 建立後，於 `move_on_mesh_surface()` 投影至模型實際表面時，系統 SHALL 優先選擇**朝下的面**（命中三角形的幾何面法線 z 分量為負），而非幾何上最接近的面。

判定 MUST 使用命中面的幾何面法線（`IndexedMesh::hit_result::normal()`，來源為 `normal_by_face_id()`），且 MUST NOT 依射線方向翻轉法線——自模型內側向上命中上表面時，其面法線仍為朝上，MUST 被排除。

當向上與向下兩個方向的命中面皆為朝下面時，系統 SHALL 選擇距離較近者。當兩者皆非朝下面（垂直壁或退化幾何）時，系統 SHALL 回退至「選擇距離較近者」的既有行為，以保證既有幾何類別的結果不變。

#### Scenario: 取樣層落在薄板中面之上

- **GIVEN** 一片厚 0.2 mm 的水平薄板，其下表面位於 z=0.00、上表面位於 z=0.20
- **AND** 支撐點建立於取樣層 z=0.125（位於中面 0.10 之上）
- **WHEN** 支撐點投影至模型表面
- **THEN** 支撐點 MUST 落在下表面 z=0.00
- **AND** 支撐點 MUST NOT 落在上表面 z=0.20，即使上表面在幾何距離上較近（0.075 < 0.125）

#### Scenario: 取樣層落在薄板中面之下

- **GIVEN** 同一片厚 0.2 mm 的薄板
- **AND** 支撐點建立於取樣層 z=0.075（位於中面 0.10 之下）
- **WHEN** 支撐點投影至模型表面
- **THEN** 支撐點 MUST 落在下表面 z=0.00

#### Scenario: 兩個方向皆為朝下面時取較近者

- **GIVEN** 向上與向下的命中面法線 z 分量皆為負
- **WHEN** 選擇投影目標面
- **THEN** 系統 MUST 選擇距離較近的命中結果

#### Scenario: 兩個方向皆非朝下面時回退既有行為

- **GIVEN** 向上與向下的命中面法線 z 分量皆不為負（垂直壁或退化幾何）
- **WHEN** 選擇投影目標面
- **THEN** 系統 MUST 選擇距離較近的命中結果
- **AND** 投影結果 MUST 與未套用本需求前的結果一致

---

### Requirement: 支撐點產出必須與切片網格相位無關

同一模型在相同 `layer_height` 下，支撐點的數量與位置 MUST NOT 因切片網格相位改變而改變。切片網格原點由 `minZ = bb.min(Z) - get_elevation()` 決定，故 `support_object_elevation` 的小數部分會改變相位；此相位變化 MUST NOT 影響支撐點產出。

**適用範圍限於有效切片層相位。** 當某相位下切片網格中不存在任何涵蓋模型的層級時（層高相對於模型高度過大），該相位在支撐點產生之前即於 `slice_model()` 階段中止。此類相位 MUST 由本需求的判準中排除，且本需求 MUST NOT 被解讀為要求消除此類無切片層相位。

#### Scenario: elevation 全相位掃描結果一致

- **GIVEN** 厚 0.2 mm 的薄板模型，`layer_height = 0.15`
- **WHEN** `support_object_elevation` 自 5.00 至 5.15 以 0.01 遞增逐一切片
- **THEN** 每一個**產生了有效切片層**的相位 MUST 產出相同數量的支撐點
- **AND** 所有支撐點的 z 座標 MUST 皆為 0.00（下表面）
- **AND** 無有效切片層的相位 MUST 於 `slice_model()` 階段中止，且此結果 MUST NOT 計為本 Scenario 的失敗

#### Scenario: 模型僅橫跨單一切片層

- **GIVEN** `m_model_height_levels.size() == 1`
- **WHEN** 執行支撐點產生
- **THEN** 系統 MUST 產出與 `size() >= 2` 時相同的支撐點集合
- **AND** MUST NOT 產出零個支撐點

#### Scenario: 多種層高下皆產出支撐點

- **GIVEN** 厚 0.2 mm 的薄板模型
- **WHEN** 分別以 `layer_height` 0.05、0.10、0.15 切片
- **THEN** 每一種層高 MUST 產出支撐點且數量不為零

---

### Requirement: 投影位移上限維持現行取值

`move_on_mesh_surface()` 所使用的位移上限（`allowed_move`）MUST 由已初始化的資料導出，MUST NOT 讀取容器邊界之外的元素。

當 `m_model_height_levels.size() > 1` 時，`allowed_move` MUST 為 `m_model_height_levels[1] - m_model_height_levels[0]` 加上既有的 epsilon 項。當元素數量不足以計算層間距時，系統 MUST 使用 `support_head_front_diameter` 作為替代值。

本需求**明確要求維持現行取值不變**。替代值 MUST NOT 改為 `layer_height`：切片網格自 `bb.min(Z) - get_elevation()` 起算、步長為 `layer_height`，故第一個涵蓋模型的層距離模型底面的高度嚴格小於一個層高；`support_head_front_diameter` 通常大於 `layer_height`，使投影更不易落入未套用方向性約束的 `squared_distance` 回退分支。

#### Scenario: 層級數量充足時的計算

- **GIVEN** `m_model_height_levels.size() >= 2`
- **WHEN** 計算 `allowed_move`
- **THEN** 結果 MUST 等於 `m_model_height_levels[1] - m_model_height_levels[0]` 加上既有的 epsilon 項

#### Scenario: 層級數量不足時使用支撐頭直徑

- **GIVEN** `m_model_height_levels.size() <= 1`
- **WHEN** 計算 `allowed_move`
- **THEN** 系統 MUST 使用 `support_head_front_diameter` 的值
- **AND** MUST NOT 存取 `m_model_height_levels[1]`

#### Scenario: 射線分支必須在薄板全相位下可達

- **GIVEN** 厚 0.2 mm 的薄板模型，`layer_height` 取 0.05 / 0.10 / 0.15
- **WHEN** `support_object_elevation` 自 5.00 至 5.15 以 0.01 遞增逐一掃描
- **THEN** 每一個產生了有效切片層的相位，其選定命中結果的 `distance()` MUST 小於或等於 `allowed_move`
- **AND** 投影 MUST NOT 進入 `squared_distance` 回退分支

---

### Requirement: 常態模型的支撐點產出必須維持不變

對於厚度除以 `layer_height` 大於或等於 2 的模型，本能力的所有變更 MUST NOT 改變支撐點的數量與位置。

判定準則為逐點比對：支撐點總數相同，且每一個支撐點的三維座標相同。任何數量或座標的變動皆 MUST 判定為回歸。

#### Scenario: 常態厚度模型逐點一致

- **GIVEN** 厚度除以 `layer_height` 大於或等於 2 的既有模型
- **WHEN** 於變更前後分別執行支撐點產生
- **THEN** 支撐點數量 MUST 完全相同
- **AND** 每一個支撐點的座標 MUST 完全相同

#### Scenario: 回退分支被頻繁觸發時視為設計失效

- **GIVEN** 一組涵蓋常態模型的回歸樣本
- **WHEN** 統計「兩個方向皆非朝下面」的回退分支觸發次數
- **THEN** 觸發次數 MUST 為可解釋的少數（垂直壁或退化幾何）
- **AND** 若觸發次數顯著偏高，MUST 判定為存在未預期的幾何類別，需回到設計文件重新決議
