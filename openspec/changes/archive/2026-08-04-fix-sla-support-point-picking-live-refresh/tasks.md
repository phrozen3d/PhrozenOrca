## 0. 前置條件與實施順序

- 本 change **無硬前置**。建立在 `fix-sla-support-point-cone-picking`（2026-08-04 archive，31/31）之上——沿用其建立的 `pin_sphere`/`cone`/`back_sphere` 三個 raycaster，不重新推導幾何公式，只改變 `update_point_raycasters_for_picking_transform()` 的呼叫時機
- 與 `fix-sla-support-top-params-live-read-isolation`、`fix-sla-support-preview-geometry-source-semantics` 獨立，三者處理的是同一個「Top 欄位即時性」主題下的不同軸線（見各自 proposal.md 的邊界說明），互不阻擋

> 全域相依圖與實施順序見 [`openspec/changes/README.md`](../README.md)。

## 1. 根因確認（已於提案階段完成）

- [x] 1.1 確認 `render_points()`（`GLGizmoSlaSupports.cpp:698`）每幀呼叫 `read_preview_top_params_live()`，直接讀取 wx 欄位當下值（`process_top_float_live()`，`:130`，經 `Field::get_value()`），不快取，畫面即時反映欄位變更
- [x] 1.2 確認 `update_point_raycasters_for_picking_transform()`（`:2485`）不在 `on_render()`（`:557-603`）的每幀呼叫路徑上，只被以下離散事件呼叫：`register_point_raycasters_for_picking()`（`:2458`）、`data_changed()`（`:461`）、支撐點拖曳過程中的更新
- [x] 1.3 確認沒有任何路徑把「Process tab Top 欄位變更」接到 `update_point_raycasters_for_picking_transform()` 的重新呼叫
- [x] 1.4 確認 `update_point_raycasters_for_picking_transform()` 內部的法向量查詢（`get_closest_point()`）被 `if (m_editing_cache[i].normal == Vec3f::Zero())` 擋住，快取後的幀不會重複觸發 raycast，其餘為純算術與 `Transform3d` 矩陣運算——每幀重算的成本量級與 `render_points()` 既有的每幀迴圈相當
- [x] 1.5 確認此現象在未選取的 auto 點（`island`/`slope`）上最明顯——其視覺幾何本來就是每幀 live 讀取；手動點多半在建立當下凍結幾何（`fix-sla-support-preview-geometry-source-semantics` 的範圍），不易呈現「長大但點不到」的症狀

## 2. 實作

- [x] 2.1 於 `on_render()`（`:595` `render_points(selection);` 之後）新增 `if (m_editing_mode) update_point_raycasters_for_picking_transform();`（見 design.md D2）
- [x] 2.2 確認非編輯模式下不會誤觸發（`m_editing_mode` 守衛 + 函式內部既有的 `m_point_raycasters.empty()` 防線，雙重保險）
- [x] 2.3 建置確認：0 errors 0 warnings

## 3. 驗證：即時性

- [x] 3.1 Manual Editing 模式下，選一顆未選取的 auto 生成點，調高 Upper Diameter，確認**不需要**選取/拖曳該點，hover 命中範圍就跟著視覺錐體一起變大
- [x] 3.2 同上，改調高 Lower Diameter，確認 back 球的命中範圍跟著視覺一起往外移、變大，舊位置不再殘留可點的死區
- [x] 3.3 反覆來回調整多個 Top 欄位（不選取、不拖曳任何點），確認任何時間點 hover 都與當下畫面一致，沒有累積的殘留命中區——**實測發現兩個問題，皆已修正**：(a) 單獨調高 Contact Diameter 後 pin/contact 球的命中範圍與視覺偏移（`pin_sphere` 球心原本就沒對準真正球心，見 design.md D3，屬 `fix-sla-support-point-cone-picking` 遺留的既有缺陷，經本 change 每幀呼叫後才被放大到可感知）；(b) clipping 隱藏的點仍會攔截其他點的 hover（本 change 每幀呼叫覆寫了 `render_points()` 剛設定的 inactive 狀態，見 design.md D4，屬本 change 自己引入的 bug）。兩者皆已於 3a、3b 修正並重新建置
- [x] 3.4 調整欄位後，再對該點做 hover → 選取 → 拖曳，確認整套互動與修改前行為一致，新增的每幀刷新沒有跟拖曳自身的 transform 更新互相干擾

## 3a. 實作時修正一：`pin_sphere` 球心對齊（見 design.md D3）

- [x] 3a.1 推導 `pinhead()` + `head_mesh_body()` 的真實局部座標，確認 pin/contact 球心的世界偏移量為 `(r_pin_mm - penetration_mm) * head.dir`，不是 `scaled_pos`（偏移量 0）
- [x] 3a.2 `update_point_raycasters_for_picking_transform()` 的 `pin_sphere` 平移改為 `scaled_pos + (head.r_pin_mm - head.penetration_mm) * head.dir`
- [x] 3a.3 確認 `cone`/`back_sphere` 的公式不受影響、不需要跟著調整（其推導從一開始就基於真實局部座標）

## 3b. 實作時修正二：`update_point_raycasters_for_picking_transform()` 自行處理 clipping（見 design.md D4）

- [x] 3b.1 確認根因：本 change 新增的每幀呼叫排在 `render_points()` 之後，把後者剛設好的 `set_active(!clipped)` 又覆寫回無條件 `true`
- [x] 3b.2 `update_point_raycasters_for_picking_transform()` 內對每個點呼叫 `is_mesh_point_clipped(sp.pos.cast<double>())`，`pin_sphere`/`cone`/`back_sphere` 三者的 `set_active` 改為 `!clipped`，不再依賴呼叫順序
- [x] 3b.3 建置確認：0 errors 0 warnings

## 4. 驗證：不得回歸

- [x] 4.1 `m_hover_id`、選取、右鍵刪除行為與修改前一致
- [x] 4.2 拖曳手感（流暢度、跟手程度）與修改前一致，未因新增的每幀呼叫產生卡頓
- [x] 4.3 500 點量級情境下旋轉視角/拖曳，流暢度與修改前一致，無因每幀重算 picking transform 產生新的可感知效能落差
- [x] 4.4 非編輯模式（Points 檢視）下 picking 行為不變（本來就不會走到新增的呼叫）
- [x] 4.5 Clipping 啟用時的 active 狀態管理不受影響——**重新驗證通過**：3b 修正後，clipped 點的 `pin_sphere`/`cone`/`back_sphere` 三者皆為 inactive、不再攔截其他點的 hover
- [x] 4.6 切片輸出不變（本 change 純渲染/picking 路徑，不觸碰幾何解析或切片管線）

## 5. Follow-up（out of scope）

- Top 欄位讀值本身的正確性（widget 被挪用顯示選中點的值導致讀到錯誤數值）→ `fix-sla-support-top-params-live-read-isolation`
- 手動點建立當下幾何被凍結、之後不再跟隨 live 參數 → `fix-sla-support-preview-geometry-source-semantics`
- 若 4.3 實測發現每幀重算在極端點數下有可感知的效能落差，評估節流方案（例如每 N 幀一次）——目前預期不需要，見 design.md D1 的成本論證
