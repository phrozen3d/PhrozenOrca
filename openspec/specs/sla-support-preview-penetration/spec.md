# sla-support-preview-penetration Specification

## Purpose
SLA 支撐 gizmo 的預覽支撐頭不得穿透預覽模型。預覽端獨立量測局部可用深度並套用與切片端相同形式的夾限，且該量測結果不得污染 `SupportPoint` 等持久化資料。

## Requirements

### Requirement: 預覽支撐頭不得穿透預覽模型

`GLGizmoSlaSupports::render_points()` 繪製的支撐頭預覽 MUST NOT 穿透其所依附的模型。預覽端 MUST 獨立量測局部可用深度並施加與切片端相同形式的 front depth 夾限。

夾限式與 `sla-support-head-penetration` 完全相同：

```
offset        = (r_contact > EPSILON && r_contact <= r_pin) ? (r_pin - r_contact) : 0
front_clamped = clamp(min(configured_front, local_thickness * 0.5 - offset), 0, configured_front)
```

換算 MUST 呼叫 `point_head_penetration_mesh_mm()`，與切片端共用同一個函式，MUST NOT 於 GUI 端另行實作換算邏輯。

#### Scenario: 極薄模型的預覽不刺穿

- **GIVEN** 厚 0.2 mm 的薄板模型，`support_head_penetration = 0.3`
- **AND** 中空網格可用
- **WHEN** 於 Points 檢視繪製支撐點預覽
- **THEN** 每個預覽支撐頭的最深點 MUST NOT 超過模型上表面

#### Scenario: 厚壁模型的預覽維持原樣

- **GIVEN** 所有支撐點的局部可用深度皆大於或等於 `2 * configured_front` 的模型
- **WHEN** 繪製支撐點預覽
- **THEN** 每個預覽支撐頭的幾何與位置 MUST 與未套用本能力時完全相同

#### Scenario: 編輯模式與非編輯模式行為一致

- **GIVEN** 同一組支撐點
- **WHEN** 分別於編輯模式與非編輯模式繪製預覽
- **THEN** 兩者套用的夾限結果 MUST 相同

---

### Requirement: 預覽量測所用網格必須包含中空與鑽孔結果

預覽端量測 MUST 使用 `CommonGizmosDataObjects::HollowedMesh` 提供的網格（來源為 `SLAPrintObject::get_mesh_to_print()`），MUST NOT 使用 `CommonGizmosDataObjects::Raycaster` 的原始 ModelVolume 網格。

`HollowedMesh` MUST 被列入 `GLGizmoSlaBase::on_get_requirements()`，使其 `on_update()` 得以執行。

表面法線查詢（`get_closest_point()`）MUST 維持使用既有的 `Raycaster`（原始網格）。外表面在中空前後一致，故法線正確；厚度量測改於中空網格進行，射線出射點才會落在內壁。兩塊網格分工混用為刻意設計。

#### Scenario: 中空模型量得實際壁厚

- **GIVEN** 一顆實心厚度 3 mm、中空後壁厚 0.5 mm 的模型
- **AND** `support_head_penetration = 0.4`
- **WHEN** 繪製支撐點預覽
- **THEN** 量得的局部可用深度 MUST 反映 0.5 mm 的壁厚，而非 3 mm
- **AND** 有效 front depth MUST 被夾限至 0.25

#### Scenario: 排水孔旁的支撐點

- **GIVEN** 已鑽排水孔的模型，某支撐點的入模射線穿過孔洞附近
- **WHEN** 繪製該點的預覽
- **THEN** 量得的可用深度 MUST 反映鑽孔後的實際幾何

#### Scenario: 法線查詢維持原始網格

- **GIVEN** 已中空的模型
- **WHEN** 查詢某支撐點的表面法線
- **THEN** 查詢 MUST 使用原始網格的射線器
- **AND** 所得法線 MUST 與未套用本能力時相同

---

### Requirement: 預覽量測必須在切片座標系進行

支撐點座標（`SupportPoint::pos`）位於 ModelObject 局部座標系，而 `HollowedMesh` 位於 `SLAPrintObject::trafo()` 座標系。量測前 MUST 完成座標映射。

射線起點 MUST 為 `po->trafo() * sp.pos`，與 `SLAPrintObject::transformed_support_points()` 使用相同的變換。

射線方向 MUST 由 `trafo()` 線性部的逆轉置作用於原始表面法線後正規化取得，入模方向 MUST 為其反向。此式對非等比縮放（法線為 covector，逆轉置為其正確變換）與鏡像變換（純鏡像矩陣的逆轉置等於自身）皆成立。

量得的距離即物理毫米，與 `Head` 的各項尺寸同量綱，MUST 直接用於夾限比較，MUST NOT 另行乘上縮放倍率。

#### Scenario: 非等比縮放下的量測

- **GIVEN** 一顆薄板模型套用非等比縮放 (1.0, 1.0, 3.0)
- **WHEN** 量測某支撐點的局部可用深度
- **THEN** 量得的值 MUST 為縮放後的實際厚度
- **AND** 預覽支撐頭 MUST NOT 穿透縮放後的模型

#### Scenario: 鏡像物件下的量測方向

- **GIVEN** 一顆薄板模型套用 X 軸鏡像，`vol->is_left_handed()` 為 true
- **WHEN** 量測某支撐點的局部可用深度
- **THEN** 射線方向 MUST 指向模型內部
- **AND** 量得的值 MUST 為正的實際厚度
- **AND** 預覽支撐頭 MUST NOT 穿透模型

#### Scenario: 鏡像與非等比縮放併用

- **GIVEN** 一顆薄板模型同時套用鏡像與非等比縮放
- **WHEN** 於軸向一致的垂直面上比對 GUI 與切片端量得的可用深度
- **THEN** 兩者 MUST 相等

---

### Requirement: 預覽量測結果不得污染持久化資料

預覽端的量測與夾限結果 MUST 僅存在於 GUI 的暫時性快取中。

系統 MUST NOT 將夾限結果寫入 `SupportPoint::head_penetration_mm` 或 `SupportPoint` 的任何其他成員。

系統 MUST NOT 因量測或夾限而修改 `ModelObject::sla_support_points`、觸發 undo/redo 快照，或使專案標記為已變更。

理由：`SupportPoint::head_penetration_mm` 承載使用者的顯式設定與自動點的凍結值，會被序列化進 3MF 專案檔並參與 `operator==`。寫入夾限結果將使夾限成為單向且不可逆——模型加厚或關閉中空後深度無法回復，且使用者在面板上看到的數值會被靜默取代。

#### Scenario: 夾限不改變面板顯示值

- **GIVEN** 使用者於 Top 面板為某手動支撐點設定 `head_penetration_mm = 0.4`
- **AND** 該點所在位置的局部可用深度僅 0.2 mm，預覽夾限為 0.1
- **WHEN** 繪製預覽並重新開啟面板
- **THEN** 面板顯示的值 MUST 仍為 0.4

#### Scenario: 夾限不進入專案檔

- **GIVEN** 某支撐點的預覽深度被夾限
- **WHEN** 儲存 3MF 專案並重新載入
- **THEN** 該點序列化的 `head_penetration_mm` MUST 為使用者設定值或哨兵值，MUST NOT 為夾限後的值

#### Scenario: 夾限不觸發未儲存變更

- **GIVEN** 專案處於已儲存狀態
- **WHEN** 進入支撐 gizmo 並繪製含夾限的預覽
- **THEN** 專案 MUST NOT 被標記為已變更
- **AND** undo 堆疊 MUST NOT 增加項目

---

### Requirement: 預覽無法量測時必須樂觀降級

當預覽端無法取得可用深度時，系統 MUST 以**未夾限的設定深度**繪製支撐頭。

適用情形：

- `HollowedMesh` 不可用（`slaposDrillHoles` 尚未完成）。
- 入模射線無命中（破損網格、非流形或自交）。

**本降級行為與切片端的 fail-safe 政策刻意不同。** 切片端無命中時取 0（保證實際輸出不穿透）；預覽端無命中時取設定值（避免將破面模型的所有支撐頭畫成貼在表面上、看似沒有支撐）。此差異 MUST 被視為符合規格，MUST NOT 被判定為兩端不一致的缺陷。

#### Scenario: 尚未切片時的預覽

- **GIVEN** 剛載入含支撐點的專案，`slaposDrillHoles` 尚未完成
- **WHEN** 繪製支撐點預覽
- **THEN** 支撐頭 MUST 以未夾限的設定深度繪製
- **AND** MUST NOT 因無法量測而不繪製該點

#### Scenario: 切片完成後自動對齊

- **GIVEN** 預覽先以樂觀深度繪製
- **WHEN** `slaposDrillHoles` 完成、`HollowedMesh` 變為可用
- **THEN** 下一次繪製 MUST 套用夾限
- **AND** MUST NOT 需要使用者手動觸發重繪以外的操作

#### Scenario: 破面模型的預覽降級

- **GIVEN** 含破面的模型，某支撐點的入模射線無命中
- **WHEN** 繪製該點的預覽
- **THEN** 該點 MUST 以未夾限的設定深度繪製
- **AND** MUST NOT 以深度 0 繪製

---

### Requirement: 預覽與切片端的一致性界限

本能力保證的是「預覽支撐頭不穿透預覽模型」，MUST NOT 被解讀為「預覽的夾限值與切片端逐位元相同」。

兩端量測軸向不同：預覽端沿其自身繪製軸向（表面法線）量測；切片端沿 optimizer 調整後的支撐頭軸向 `nn` 量測。因此夾限值可存在殘差。

此軸向落差在本能力生效之前即已存在於預覽的繪製方向，屬獨立議題，本能力 MUST NOT 修正它。

系統 MUST NOT 為求數值一致而建立跨切片管線的計算結果回寫通道。理由：本 fork 未定義 `SUPPORT_BACKGROUND_PROCESSING`，切片管線會被 `to_object_step` 硬性截斷；支撐 gizmo 的主要工作流僅執行至 `slaposSupportPoints`，夾限所在的 `slaposSupportTree` 不執行，該通道在主要工作流下恆為空。

#### Scenario: 垂直承載面上兩端一致

- **GIVEN** 支撐點位於水平承載面，其表面法線與支撐頭軸向一致
- **WHEN** 分別於預覽端與切片端量測可用深度
- **THEN** 兩者量得的值 MUST 相等

#### Scenario: 傾斜承載面上允許殘差

- **GIVEN** 支撐點位於傾斜承載面，optimizer 調整後的頭軸與表面法線不重合
- **WHEN** 分別於預覽端與切片端量測可用深度
- **THEN** 兩者的值可不相等
- **AND** 兩者各自的幾何 MUST NOT 穿透各自所依附的模型
- **AND** 此差異 MUST NOT 被判定為缺陷

#### Scenario: 主要工作流下預覽仍生效

- **GIVEN** 使用者於 Points 檢視按下「自動產生支撐點」，切片僅執行至 `slaposSupportPoints`
- **WHEN** 繪製支撐點預覽
- **THEN** 預覽 MUST 已套用夾限
- **AND** MUST NOT 因 `slaposSupportTree` 未執行而退回未夾限的繪製

---

### Requirement: 預覽量測必須惰性且可快取

預覽端的可用深度量測 MUST 採惰性填充，每個支撐點在其快取有效期間內 MUST 最多執行一次射線查詢。

射線加速結構（AABB）MUST 依網格身分快取，MUST NOT 每幀重建。

快取 MUST 於下列時機失效：支撐點集被取代、量測網格改變、物件變換改變。編輯模式下單點被移動時 MUST 僅失效該點。

設定值（如 `support_head_penetration`）改變 MUST NOT 使厚度快取失效——可用深度與設定無關，僅需重新計算夾限式。

#### Scenario: 連續多幀不重複量測

- **GIVEN** 非編輯模式，500 個支撐點，厚度快取已填充
- **WHEN** 使用者拖曳旋轉視角 60 幀
- **THEN** 這 60 幀中的可用深度射線查詢總次數 MUST 為 0

#### Scenario: 加速結構不每幀重建

- **GIVEN** 量測網格未改變
- **WHEN** 連續渲染多幀
- **THEN** AABB 加速結構 MUST 僅建構一次

#### Scenario: 設定值改變不重打射線

- **GIVEN** 厚度快取已填充
- **WHEN** 使用者於 Process 分頁調整 `support_head_penetration`
- **THEN** 可用深度射線查詢次數 MUST 為 0
- **AND** 繪製結果 MUST 反映新設定值下的夾限

#### Scenario: 拖曳單點僅失效該點

- **GIVEN** 編輯模式，厚度快取已填充
- **WHEN** 使用者拖曳其中一個支撐點
- **THEN** 僅該點的厚度快取 MUST 失效
- **AND** 其餘點 MUST NOT 重新量測
