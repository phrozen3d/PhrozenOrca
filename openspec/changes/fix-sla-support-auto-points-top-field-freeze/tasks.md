## 0. 前置條件與實施順序

- [ ] 0.1 確認 `fix-sla-support-preview-geometry-source-semantics`（Change A）第一階段的真值表結論（design.md D4）仍成立，作為本 change 的對齊基準——**不需等待 Change A 的實作完成**，兩者檔案不重疊、可平行進行
- [ ] 0.2 全域搜尋 `SupportPointGeneratorConfig` 的建構/賦值位置（含測試程式碼），列出除 `SLAPrintSteps.cpp::support_points()` 外的其他呼叫點，避免新增欄位遺漏初始化（design.md Risks）

## 1. `SupportPointGeneratorConfig` 擴充欄位

- [ ] 1.1 在 `SupportPointGenerator.hpp` 的 `SupportPointGeneratorConfig` 新增 `head_back_radius_mm`、`head_width_mm`、`head_penetration_mm`、`contact_sphere_radius` 四個欄位，給定合理預設值並註解其單位與語意（見 design.md D1）
- [ ] 1.2 確認新欄位命名與 `SupportPoint.hpp` 對應欄位一致，避免呼叫端混淆

## 2. `SLAPrintSteps.cpp` 讀取 preset 並傳遞

- [ ] 2.1 在 `support_points()`（約 `:862`）組裝 `config` 時，一併讀取 `cfg.support_head_back_diameter`／`support_segment_length`／`support_head_penetration`／`support_contact_type`／`support_contact_diameter`，寫入 `config` 的對應新欄位（見 design.md D1 的具體轉譯規則）
- [ ] 2.2 確認 `support_contact_type` 為 `spSphere` 時才寫入正的 `contact_sphere_radius`，否則寫入 `0.f`（對齊 `point_uses_contact_sphere()` 的語意，見 design.md Risks）
- [ ] 2.3 確認這些讀取只發生一次（迴圈外），不在生成迴圈內重複查詢 config

## 3. `SupportPointGenerator.cpp` 生成點時寫入全部欄位

- [ ] 3.1 修改 `:238` 附近（`near_points.add` 產生 slope 點）：建構或賦值時一併寫入 `head_back_radius_mm`／`head_width_mm`／`head_penetration_mm`／`contact_sphere_radius`
- [ ] 3.2 修改 `:266` 附近（island 產生點，第一處）：同上
- [ ] 3.3 修改 `:294` 附近（island 產生點，第二處）：同上
- [ ] 3.4 確認三處寫入的值與來源一致（都來自同一個 `config` 物件），沒有遺漏或不同步

## 4. 驗證：生成後即凍結

- [ ] 4.1 用 Top 欄位值 A 觸發 Auto-generate，確認每一顆新生成點的五個 Top 欄位皆為 A 對應的具體值（透過 debug 輸出或暫時性檢查，不透過 preview——preview 讀取邏輯屬 Change A）
- [ ] 4.2 將 Process tab 的 Lower Diameter 改為 B，不重新生成，直接觸發完整切片，量測輸出 G-code／3mf 中該批點的支撐頭 Lower 直徑，確認仍是 A
- [ ] 4.3 同 4.2，改測 Segment Length／Penetration／Contact Sphere／Contact Type，逐一確認皆維持 A
- [ ] 4.4 切到 Structure 檢視模式（不觸發完整切片），確認顯示的支撐結構幾何仍反映 A，不是 B
- [ ] 4.5 重新按下 Auto-generate，確認舊點依現行行為（清除重造）被替換，新點的五個欄位皆凍結為 B

## 5. 驗證：與 Change A 交叉確認（若 Change A 已實作）

- [ ] 5.1 若 `fix-sla-support-preview-geometry-source-semantics` 已完成，確認 preview 顯示的 auto 點尺寸與本 change 的切片輸出一致（兩邊都應該是「生成當下凍結值」）
- [ ] 5.2 確認 auto 點與手動點在「建立/生成後即凍結全部欄位」這件事上行為一致，UI 上不再有「有些欄位凍結、有些沒有」的落差

## 6. 驗證：效能與快取

- [ ] 6.1 用一個會生成數千顆 auto 支撐點的模型，比較本 change 前後 Auto-generate 的執行耗時，確認差異在量測誤差範圍內
- [ ] 6.2 確認 `perf-sla-support-points-preview-render` 的 `HeadGeomKey` distinct 數量與 64 筆門檻觸發頻率，本 change 後持平或改善，不劣化

## 7. 驗證：不得回歸

- [ ] 7.1 密度類參數（`support_points_density_relative`／`support_points_minimal_distance`／`support_critical_angle`／`branchingsupport_critical_angle`）變動後仍正確觸發 `slaposSupportPoints` 重新生成，行為不受本 change 影響
- [ ] 7.2 手動點（`manual_add` 類型）的行為不受本 change 影響——本 change 只動生成器對 `island`/`slope` 點的寫入邏輯
- [ ] 7.3 現有 3mf 專案（含舊版程式生成、未含新欄位語意的既有支撐點資料）載入後行為正常，不因新欄位的存在而出錯（新欄位只影響本次程式版本之後新生成的點，不回溯改寫舊資料）
- [ ] 7.4 全域搜尋結果（task 0.2）列出的其他 `SupportPointGeneratorConfig` 呼叫點，逐一確認新欄位已正確初始化或明確不適用

## 8. Follow-up（out of scope）

- preview／picking 的讀取邏輯修正 → `fix-sla-support-preview-geometry-source-semantics`（Change A，平行進行，互不阻擋）
- 手動點的 `head_back_radius_mm` 凍結（`pillar_radius` fallback 移除）→ 屬於 Change A 範圍，非本 change
