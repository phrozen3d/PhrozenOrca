## ADDED Requirements

### Requirement: slice_model 以 slice_csgmesh_ex 對 CSG parts 切片
`slice_model()` SHALL 以 `csg::slice_csgmesh_ex(range(po.m_mesh_to_slice), slicegrid, params)` 執行切片，以 2D CSG boolean（Union / Difference）合併各 part 的切片結果。系統 SHALL NOT 使用 `slice_mesh_ex(hollow_mesh_with_holes)` 並額外執行 interior `diff_ex()`。

#### Scenario: 實心模型切片結果幾何等價
- **WHEN** 一個無 hollowing 的實心模型以新路徑切片
- **THEN** 每層輪廓的面積差 SHALL 小於 0.1%，輪廓位移 SHALL 小於 0.01mm，與舊路徑切片結果比較

#### Scenario: Hollow 模型切片包含正確內腔
- **WHEN** hollowing 啟用（thickness = T），以新路徑切片
- **THEN** 每層的切片輪廓 SHALL 呈現內外兩圈，內圈代表 hollow interior，壁厚約為 T，形狀與舊路徑結果幾何等價

#### Scenario: Hollow + 排水孔模型切片包含正確孔洞
- **WHEN** 模型有 hollowing 啟用且有 N 個排水孔，以新路徑切片
- **THEN** 排水孔截面 SHALL 在穿越的每個切片層正確出現，孔洞形狀為圓形（或橢圓，視角度而定），幾何與舊路徑等價

#### Scenario: Z correction 層數與舊路徑相同
- **WHEN** Z correction 啟用（`zcorrection_layers > 0`），以新路徑切片
- **THEN** 修正後的層數 SHALL 與舊路徑完全相同（允許差異 ≤ 1 層）

### Requirement: slicegrid 計算與舊路徑完全一致
`slice_model()` 傳給 `slice_csgmesh_ex()` 的 `slicegrid`（各層 Z 高度向量）SHALL 與舊路徑傳給 `slice_mesh_ex()` 的 slicegrid 完全相同，包含 first layer height、layer height、Z correction 的計算方式。

#### Scenario: 層高設定正確反映在 slicegrid
- **WHEN** layer height = 0.05mm，first layer height = 0.03mm，模型高度 = 10mm
- **THEN** slicegrid 的第一個 Z 值 SHALL 為 0.03mm，後續間隔 SHALL 為 0.05mm，總層數與舊路徑相同

#### Scenario: 複雜幾何不拋出切片例外
- **WHEN** 模型包含 negative volumes 與多個 CSG Difference parts
- **THEN** `slice_csgmesh_ex()` SHALL 正常完成，不拋出例外；若某層的 2D diff 結果為空，該層 ExPolygons 為空（不崩潰）
