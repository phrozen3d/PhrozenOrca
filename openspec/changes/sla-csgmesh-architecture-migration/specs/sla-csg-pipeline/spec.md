## ADDED Requirements

### Requirement: Pipeline 第一步組裝 CSG parts
SLA print pipeline SHALL 在 `slaposHollowing` 之前執行 `slaposAssembly` 步驟，以 `csg::model_to_csgmesh()` 將模型所有 model volumes 轉換為 `csg::CSGPart`，存入 `SLAPrintObject::m_mesh_to_slice`（`std::multimap<SLAPrintObjectStep, csg::CSGPart>`）。

#### Scenario: 單一 volume 模型的 CSG parts 建立
- **WHEN** 一個只有單一 model volume 的 SLA 模型觸發 pipeline
- **THEN** `slaposAssembly` 執行後，`m_mesh_to_slice` SHALL 包含至少一個 `CSGType::Union` 的 CSGPart，且 CSGPart 的 mesh 幾何與原始 volume mesh 一致

#### Scenario: 多 volume 模型的 CSG parts 建立
- **WHEN** 一個包含 positive 與 negative volumes 的 SLA 模型觸發 pipeline
- **THEN** `slaposAssembly` 完成後，`m_mesh_to_slice` SHALL 包含 positive volumes（`CSGType::Union`）與 negative volumes（`CSGType::Difference`）的對應 CSGPart

#### Scenario: pipeline 步驟順序
- **WHEN** SLA slicing pipeline 完整執行
- **THEN** 執行順序 SHALL 為 `slaposAssembly` → `slaposHollowing` → `slaposDrillHoles` → `slaposObjectSlice` → 後續步驟，Assembly 必須在 Hollowing 之前完成

### Requirement: get_parts_to_slice 回傳指定步驟前的所有 CSG parts
`SLAPrintObject::get_parts_to_slice()` SHALL 回傳 `m_mesh_to_slice` 中所有已完成步驟的 `csg::CSGPart`，以 shallow copy range 方式傳遞（不複製 mesh 資料）。

#### Scenario: 無排水孔時回傳 Assembly + Hollowing parts
- **WHEN** `slaposDrillHoles` 完成且模型無排水孔，呼叫 `get_parts_to_slice()`
- **THEN** 回傳的 CSGPart 集合 SHALL 包含 `slaposAssembly` 的 parts 以及（若 hollowing 啟用）`slaposHollowing` 的 Difference part

#### Scenario: 含排水孔時回傳所有 parts
- **WHEN** `slaposDrillHoles` 完成且模型有 N 個排水孔，呼叫 `get_parts_to_slice()`
- **THEN** 回傳的 CSGPart 集合 SHALL 包含 Assembly、Hollowing（若有）、以及 N 個 `slaposDrillHoles` Difference parts

### Requirement: 步驟 invalidation 只清除自身 step 的 CSG parts
當某個 SLA 步驟被 invalidate 並重新執行時，系統 SHALL 只清除該步驟對應的 CSG parts（`clear_csg(m_mesh_to_slice, step)`），不影響其他步驟的 parts。

#### Scenario: 修改 hollowing 參數後重新執行
- **WHEN** 使用者修改 hollowing thickness 參數，`slaposHollowing` 被 invalidate
- **THEN** 系統 SHALL 清除 `m_mesh_to_slice` 中 key 為 `slaposHollowing` 的 parts，保留 `slaposAssembly` 的 parts 不變

#### Scenario: 修改排水孔後重新執行
- **WHEN** 使用者新增或刪除排水孔，`slaposDrillHoles` 被 invalidate
- **THEN** 系統 SHALL 只清除 `m_mesh_to_slice` 中 key 為 `slaposDrillHoles` 的 parts，Assembly 與 Hollowing parts 不受影響
