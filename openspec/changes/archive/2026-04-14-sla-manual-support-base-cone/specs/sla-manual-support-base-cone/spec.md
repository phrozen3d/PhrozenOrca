## ADDED Requirements

### Requirement: 手動放置的支撐點各自生成獨立底座圓錐
每個手動放置（`SupportPointType::manual_add`）的 SLA 支撐點，在 `classify()` 的 clustering 階段，SHALL NOT 與任何其他支撐點（無論 manual 或 auto）被歸入同一個 cluster。每個手動支撐點 SHALL 各自成為獨立的單一元素 cluster，並在 `routing_to_ground()` 階段各自建立完整的地面柱（Pillar）與底座圓錐（Pedestal）。

#### Scenario: 兩個距離小於底座直徑的手動點各自有底座
- **WHEN** 使用者在 Manual editing 模式放置兩個 XY 水平距離 < `2 × base_radius_mm` 的支撐點
- **THEN** 支撐樹生成後，兩個支撐點均各自擁有獨立的地面柱與底座圓錐

#### Scenario: 手動點與 auto 點不共用 cluster
- **WHEN** 一個手動放置的支撐點與一個自動生成的支撐點水平距離 < `2 × base_radius_mm`
- **THEN** 手動點不被歸入 auto 點的 cluster，手動點建立自己的獨立地面柱與底座

#### Scenario: 多個手動點允許底座重疊
- **WHEN** 多個手動支撐點的底座圓錐在幾何上互相重疊
- **THEN** 重疊的底座正確出現在最終支撐 mesh 中，SLA 切層結果正確（重疊區域合併為一個輪廓）

#### Scenario: Light 重量手動點也生成底座圓錐
- **WHEN** 使用者在 Manual editing 模式以 Light 重量放置支撐點（其柱半徑 < `head_back_radius_mm`）
- **THEN** 支撐樹生成後，該支撐點仍擁有獨立的地面柱與底座圓錐，底座尺寸為標準 `base_radius_mm`

### Requirement: auto-generated 支撐點的 clustering 行為不變
對於 `SupportPointType::automatic` 的支撐點，`classify()` 的 clustering 邏輯 SHALL 保持與修改前完全相同的行為，包含閾值計算與 centroid-sidehead 分配。

#### Scenario: auto 點在閾值內仍共用柱
- **WHEN** 兩個 auto-generated 的支撐點水平距離 < `2 × base_radius_mm`
- **THEN** 它們仍被歸入同一個 cluster，centroid 建立地面柱，sidehead 橋接到 centroid 柱