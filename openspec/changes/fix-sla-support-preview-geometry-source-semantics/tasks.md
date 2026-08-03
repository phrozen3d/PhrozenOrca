## 0. 前置條件與實施順序

- [x] 0.1 `fix-sla-support-top-config-enum-set` 已完成（2026-08-03 archive，45/45）。**硬前置已滿足**：crash 已修好，選中點編輯 Top 欄位不再使應用程式終止，可進行第 1 節所需的觀察
- [x] 0.2 `fix-sla-support-preview-stored-geometry-in-auto-mode` 已完成（2026-08-03 archive，23/23）。**建議前置已滿足**：「切到自動模式尺寸就變」這個變因已消除（見 design.md D3）

> 全域相依圖與實施順序見 [`openspec/changes/README.md`](../README.md)。

## 1. 語意定義（產品決策，須先完成才進入實作）

- [x] 1.1 已展開現行真值表：編輯模式 × 點類型 × 有無 explicit geometry × 是否選取 → 幾何來源（見 proposal.md）
- [x] 1.2 已確認切片端（`SupportTreeBuildsteps.cpp:693`）無條件套用 `point_*()` 助手，**逐欄位**仲裁，且無「編輯模式」與「選取狀態」概念
- [x] 1.3 已確認 preview 為**整顆點**二選一（`use_stored_point` 單一布林決定全部欄位），與切片端的粒度不同
- [ ] 1.4 **關鍵事實查證**：auto 生成的點其 `sp.head_front_radius` 是否恆等於 preset 值？若否（例如生成器依 island 大小調整），逐欄位方案會改變 auto 點的 preview 尺寸，須納入驗收
- [ ] 1.5 **Q1 決策**：選取是否應改變幾何來源？（design.md D1 傾向「否」）
- [ ] 1.6 **Q2 決策**：仲裁粒度是否改為逐欄位、與切片端共用解析？（design.md D1/D2 傾向「是」）
- [ ] 1.7 **Q3 決策**：live 參數編輯的作用對象是否需要更明確的 UI 途徑？若結論為「行為正確但不明顯」，另案處理，不納入本 change
- [ ] 1.8 **產出書面真值表**並回填至 design.md，逐格標註與切片端的對應關係。未完成前不進入第 2 節

## 2. 實作

- [ ] 2.1 依 1.5 結論處理 `preview_use_stored_top()`（`:213`）的 `point_selected` 早退
- [ ] 2.2 依 1.6 結論調整 `preview_sla_head_for_point()` 的解析。若採逐欄位方案，改為直接套用 `point_*()` 助手，與 `SupportTreeBuildsteps` 共用同一套規則
- [ ] 2.3 若 `use_stored_point` 參數在逐欄位方案下失去意義，一併移除 `preview_use_stored_top()`，避免留下無呼叫者的死碼
- [ ] 2.4 更新 `render_points()`（`:750`）的呼叫端
- [ ] 2.5 同步 `update_point_raycasters_for_picking_transform()` 的 `pick_r` 解析，使其與顯示採用同一組幾何
- [ ] 2.6 確認實作後 preview、picking、切片三處共用單一解析路徑，不留下第二套規則

## 3. 驗證：選取不改變尺寸

- [ ] 3.1 編輯模式下選取一顆尚無 explicit geometry 的 `manual_add` 點，確認錐體尺寸與選取前完全相同，僅顏色改變
- [ ] 3.2 選取一顆自動生成的點，確認尺寸不變
- [ ] 3.3 取消選取，確認尺寸不變
- [ ] 3.4 反覆選取／取消選取數次，確認外型完全穩定、無跳動

## 4. 驗證：與切片一致

- [ ] 4.1 建立一顆只設定過 `head_width_mm` 的手動點，確認錐體長度用該點的值、頭部直徑等其餘尺寸用 preset
- [ ] 4.2 切片後量測該點的實際支撐幾何，確認與 preview 逐欄位相符
- [ ] 4.3 建立一顆完全未設定 per-point 幾何的點，確認全部尺寸用 preset 且與切片一致
- [ ] 4.4 同一顆點分別在編輯模式與非編輯模式下渲染，確認尺寸完全相同
- [ ] 4.5 建立數顆設定欄位組合各異的手動點，逐顆比對 preview 與切片結果

## 5. 驗證：live preset 編輯的作用對象

- [ ] 5.1 修改 `support_head_front_diameter` 並觸發重繪，確認自動生成的點跟著變、已設定該欄位的手動點不變
- [ ] 5.2 對只設定過 `head_width_mm` 的點修改 `support_head_front_diameter`，確認其錐體直徑跟著變、長度不變（逐欄位生效）
- [ ] 5.3 確認該行為與切片端一致

## 6. 驗證：不得回歸

- [ ] 6.1 **auto 點顯示**：依 1.4 的結論，比對修改前後 auto 點的 preview 尺寸。若 `sp.head_front_radius` 恆等於 preset 則應完全相同；若不等則記錄差異並確認新行為才是正確的
- [ ] 6.2 **picking 一致**：hover 一顆 per-point 尺寸與 preset 明顯不同的點，確認命中範圍與可見錐體一致，無「看得到點不到」或「點得到看不到」
- [ ] 6.3 **幾何快取**：確認 `perf-sla-support-points-preview-render` 的 `HeadGeomKey` 分布未爆增、64 筆門檻未被頻繁觸發、穩態每幀 `init_from()` 呼叫次數仍為 0
- [ ] 6.4 Structure 模式仍 early return
- [ ] 6.5 clipping 逐點判定不變
- [ ] 6.6 切片輸出與本 change 之前相同（本 change 只改 preview，不改切片）

## 7. Follow-up（out of scope）

- 若 1.7 結論為「行為正確但不明顯」，live 參數作用範圍的 UI 提示 → 另案
- 非編輯模式下手動點不套用 per-point 幾何 → `fix-sla-support-preview-stored-geometry-in-auto-mode`（本 change 的建議前置）
- per-point Top 欄位顯示失效與其 crash → `fix-sla-support-top-config-enum-set`（本 change 的硬前置）
- `sla_trafo` 變換後前端快取不失效 → `fix-sla-support-points-invalidate-on-trafo-change`
- 支撐點 undo/redo 資料不正確 → `fix-sla-support-points-undo-snapshot`
