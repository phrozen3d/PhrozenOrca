## ADDED Requirements

### Requirement: 排水孔以 CSG Difference part 加入 pipeline，不執行 3D Boolean
`drill_holes()` SHALL 以 `csg::model_to_csgmesh()` 的 `mpartsDrillHoles` flag 將每個排水孔的幾何加入 `m_mesh_to_slice`，CSGType 為 `Difference`。系統 SHALL NOT 執行 CGAL 3D mesh boolean（`hollow_mesh_and_drill()`）。

#### Scenario: 無排水孔時 drill_holes 不加入任何 part
- **WHEN** `sla_drain_holes` 為空，`slaposDrillHoles` 執行
- **THEN** `m_mesh_to_slice` 中 key 為 `slaposDrillHoles` 的 entries SHALL 為空，函式正常完成不崩潰

#### Scenario: N 個排水孔加入 N 個 Difference parts
- **WHEN** 模型有 N 個排水孔，`slaposDrillHoles` 執行
- **THEN** `m_mesh_to_slice` SHALL 包含 N 個 key 為 `slaposDrillHoles`、CSGType 為 `Difference` 的 CSGPart

#### Scenario: 幾乎相交的排水孔不崩潰
- **WHEN** 兩個排水孔的圓柱體幾何幾乎相交（中心距 < 排水孔直徑）
- **THEN** `drill_holes()` SHALL 正常完成，兩個 Difference parts 各自獨立加入 `m_mesh_to_slice`，不拋出例外

#### Scenario: 排水孔位於薄壁時不崩潰
- **WHEN** 排水孔穿越壁厚小於排水孔直徑的區域
- **THEN** `drill_holes()` SHALL 正常完成，排水孔 CSGPart 正確加入，幾何衝突由 2D slice 層的 `diff_ex()` 處理

### Requirement: drill_holes 執行後移除對 hollow_mesh_with_holes 的依賴
`drill_holes()` 完成後，系統 SHALL 不再需要 `m_hollowing_data->hollow_mesh_with_holes` 供 `slice_model()` 使用；切片所需的所有幾何資訊 SHALL 從 `m_mesh_to_slice` 的 CSG parts 取得。

#### Scenario: hollowing 啟用時 drill_holes 後無需合併 mesh
- **WHEN** hollowing 啟用，`slaposDrillHoles` 完成後
- **THEN** `slice_model()` SHALL 能從 `get_parts_to_slice()` 取得足夠資訊切片，不依賴 `hollow_mesh_with_holes` 欄位

#### Scenario: hollowing 停用時行為不變
- **WHEN** hollowing 停用（`hollowing_enable = false`），`slaposDrillHoles` 執行
- **THEN** 只有排水孔 parts 加入（若有），Assembly parts 維持 Union，切片結果 SHALL 與原始 mesh 相同
