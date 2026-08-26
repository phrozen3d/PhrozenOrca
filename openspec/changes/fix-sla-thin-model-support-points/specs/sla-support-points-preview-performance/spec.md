## MODIFIED Requirements

### Requirement: Pinhead 幾何依參數快取，不隨點數重建

`render_points()` SHALL 以 head 幾何參數（pin 半徑、back 半徑、寬度、contact 球半徑、preview 旗標）為 key 快取已建構的 `GLModel`。幾何參數相同的支撐點 SHALL 共用同一個 `GLModel` 實例。

**`penetration` SHALL NOT 出現在快取 key 中。** 由 `head_mesh_body()` 的幾何推導，本體頂點的 z 為「與 `penetration_mm` 無關的量」加上 `penetration_mm`，接觸球頂點亦然；改變 `penetration_mm` 只造成整個支撐頭網格沿其局部 `+z` 軸的**剛體平移**，不改變形狀。因此快取的 `GLModel` SHALL 以 `penetration_mm = 0` 建構，實際的刺入深度 SHALL 由繪製時的 model matrix 平移承載。

此規則對 preview 與非 preview 兩個分支皆成立（preview 分支的 `z_shift` 具相同結構，其 `segment_len` 僅依賴 width / pin 半徑 / back 半徑）。

由於逐點動態夾限（見 `sla-support-preview-penetration`）會使每個支撐點的 `penetration_mm` 各不相同，將其納入 key 會使快取項數趨近點數而導致失效。移除該欄位 SHALL 使逐點刺入深度差異**完全不造成快取失效**。

穩態下（幾何參數未變動），每幀的 `GLModel::init_from()` 呼叫次數 SHALL NOT 隨 N 成長。

快取 SHALL 有筆數門檻。超過門檻時 SHALL 整份清空後重新填充，且 SHALL NOT 切換到另一條渲染路徑——任何情況下畫面結果皆由同一條快取路徑產生。

#### Scenario: 參數一致的 auto 點共用同一模型

- **GIVEN** 非編輯模式，500 個 auto 點皆使用同一組 live 幾何參數
- **WHEN** 渲染一幀
- **THEN** 快取命中 499 次
- **AND** `GLModel::init_from()` 呼叫 0 或 1 次（視前一幀是否已建構）

#### Scenario: 逐點刺入深度相異時仍共用同一模型

- **GIVEN** 非編輯模式，500 個 auto 點的 pin/back 半徑、寬度、contact 球半徑皆相同
- **AND** 每個點因局部可用深度不同而被夾限為相異的 `penetration_mm`
- **WHEN** 渲染一幀
- **THEN** 快取項數 SHALL 為 1
- **AND** 快取命中 499 次
- **AND** `GLModel::init_from()` 呼叫次數 SHALL NOT 隨相異 `penetration_mm` 的數量成長

#### Scenario: 帶 explicit geometry 的 manual 點

- **GIVEN** 編輯模式，含若干帶 explicit geometry 的 manual 點，其 pin/back 半徑各不相同
- **WHEN** 渲染一幀
- **THEN** 每一組相異參數對應一個快取項
- **AND** 每個點顯示的 cone 尺寸與逐點建構的舊行為相同

#### Scenario: preview 旗標不同的點不共用

- **GIVEN** 同時存在 auto 點（`get_mesh`，steps 24）與 `manual_add` 點（`get_mesh_preview`，steps 45）
- **WHEN** 渲染一幀
- **THEN** 兩者使用不同的快取項
- **AND** 各自的幾何與舊行為一致

#### Scenario: 超過快取門檻

- **GIVEN** 相異幾何參數組數超過快取門檻
- **WHEN** 渲染一幀
- **THEN** 快取整份清空後重新填充
- **AND** 畫面結果與未超過門檻時完全一致

## ADDED Requirements

### Requirement: 刺入深度以 model matrix 平移承載

繪製支撐頭預覽時，刺入深度 SHALL 以 model matrix 的局部平移施加，平移量為 `penetration_mm`、方向為支撐頭的局部 `+z` 軸。

平移 SHALL 施加於擺放旋轉**之後**，使其在世界座標中沿 `-head.dir` 方向（指向模型內部）。`penetration_mm` 越大，支撐頭 SHALL 越深入模型。

平移為純位移，不改變 model matrix 的線性部，故光照法線矩陣（`view_normal_matrix`）SHALL NOT 因本規則而改變其推導方式。

套用本規則後的每個支撐頭，其每一個頂點的世界座標 SHALL 與「將 `penetration_mm` 烘進網格再繪製」的路徑完全一致。

#### Scenario: 平移方向正確

- **GIVEN** 一個 `penetration_mm` 為正的支撐頭
- **WHEN** 繪製該支撐頭
- **THEN** 其相對於 `penetration_mm = 0` 的位移方向 SHALL 為 `-head.dir`
- **AND** 位移量 SHALL 等於 `penetration_mm`

#### Scenario: 與烘進網格的路徑等價

- **GIVEN** 任一組支撐頭參數與任一 instance transform
- **WHEN** 分別以「canonical 網格加 model matrix 平移」與「`penetration_mm` 烘進網格」兩種方式繪製
- **THEN** 兩者的每個頂點世界座標 SHALL 相同

#### Scenario: 光照不因平移改變

- **GIVEN** 套用了刺入深度平移的支撐頭
- **WHEN** 計算 `view_normal_matrix`
- **THEN** 其結果 SHALL 與未套用平移時相同
- **AND** 著色結果 SHALL 無可見差異

---

### Requirement: Picking 碰撞體必須與可見幾何一致

`update_point_raycasters_for_picking_transform()` 所設定的三個碰撞體（pin sphere、cone、back sphere）SHALL 依與 `render_points()` 相同的 `penetration_mm` 定位。

兩個函式 SHALL 於同一幀取得相同的夾限結果，MUST NOT 各自獨立量測而可能得出不同的值。

碰撞體位置由 `head.penetration_mm` 與 `head.fullwidth()` 解析導出，故只要建構 `Head` 的共用函式回傳夾限後的值，三個碰撞體即自動跟隨；本需求 SHALL NOT 被實作為在碰撞體端另行套用一次夾限。

#### Scenario: 夾限後 hover 位置與可見幾何一致

- **GIVEN** 某支撐點的刺入深度被夾限，其可見支撐頭因此上移
- **WHEN** 使用者將滑鼠移至該支撐頭可見頂端球的位置
- **THEN** 該點 SHALL 被 hover 標示
- **AND** 移至夾限前舊位置時 SHALL NOT 被標示

#### Scenario: 兩個函式取得相同夾限值

- **GIVEN** 同一幀中 `render_points()` 與 `update_point_raycasters_for_picking_transform()` 皆被呼叫
- **WHEN** 兩者各自為同一支撐點建構 `Head`
- **THEN** 兩者取得的 `penetration_mm` SHALL 完全相同

#### Scenario: 接觸球放大時的命中一致性

- **GIVEN** 啟用 Contact Sphere 且其直徑大於支撐頭上直徑
- **AND** 該點的刺入深度被夾限
- **WHEN** 使用者於接觸球邊緣 hover
- **THEN** 命中判定 SHALL 與可見接觸球的範圍一致
