## 0. 前置條件與實施順序

- 本 change **無前置**，與 `fix-sla-support-point-issues` 分支既有五項互相獨立，不影響其順序依賴關係

> 全域相依圖與實施順序見 [`openspec/changes/README.md`](../README.md)。

## 1. 根因確認（已於 explore 討論階段完成）

- [x] 1.1 確認 `render_points()`（`GLGizmoSlaSupports.cpp:705-810`）依 `support_point.type == manual_add` 分流 mesh 函式與 shader：manual 點走 `head_mesh_body(head, 45, /*preview=*/true)`（內部呼叫 `pinhead_preview()`）+ `flat_shader`；auto 點走 `head_mesh_body(head, 24, /*preview=*/false)`（內部呼叫 `pinhead()`）+ `gouraud_shader`
- [x] 1.2 確認 `pinhead()`（`SupportTreeMesher.cpp:158-229`）：pin 球 + back 球，兩者以相切圓公式平滑過渡（`phi` 補償角），形成弧形端頭
- [x] 1.3 確認 `pinhead_preview()`（`SupportTreeMesher.cpp:231-291`）：只有 pin 球，後段是直筒 + 平面圓盤收尾，程式碼註解明寫「no back-sphere bulge」
- [x] 1.4 確認 `flat.fs`（`resources/shaders/140/flat.fs`）僅一行 `gl_FragColor = uniform_color;`，不讀法線、無光照計算；`gouraud_light.fs` 讀 vertex shader 算出的 `intensity`（含 specular）疊加 `uniform_color`
- [x] 1.5 確認顏色分流（`GLGizmoSlaSupports.cpp:718-735`）：`manual_add`→CYAN、`island`→ORANGE、其餘→LIGHT_GRAY 三條分支皆包在 `m_editing_mode &&` 底下；非編輯模式一律 `{0.5,0.5,0.5,1.f}`
- [x] 1.6 確認 `m_lock_unique_islands && support_point.is_island() && m_editing_mode` → BLUEISH 這條混合了「點的屬性」與「使用者是否啟用鎖定」兩件事，且已確認鎖定操作只在編輯模式面板上可觸發
- [x] 1.7 全專案掃描確認 `pinhead_preview()` / `get_mesh_preview()` 僅有 `render_points()` 一個呼叫端（`grep` 結果零其他匹配），無切片管線或其他 GUI 依賴
- [x] 1.8 確認 `HeadGeomKey`（`GLGizmoSlaSupports.hpp`）含 `preview` 布林欄位，由 `head_geom_key(head, manual_preview)` 填入，是 `m_head_model_cache` 的 key 組成之一

## 2. 形狀統一

- [ ] 2.1 `render_points()` 移除 `manual_preview` 變數與其分支，改為對所有點呼叫 `sla::head_mesh_body(head, 24, /*preview=*/false)`（steps 統一為 24，見 design.md D2 的理由）
- [ ] 2.2 移除 `kManualPreviewSteps` 常數
- [ ] 2.3 確認 `head_geom_key(head, preview)` 的呼叫改為恆定傳入 `false`

## 3. 光影統一

- [ ] 3.1 `render_points()` 的 `use_shader(...)` 呼叫改為無條件使用 `gouraud_shader`，移除對 `flat_shader` 的選擇分支
- [ ] 3.2 確認 `view_normal_matrix` 等 gouraud 專屬 uniform 設定（原本 `if (active_shader != flat_shader)` 保護的區塊）現在對所有點都會執行，確認無殘留的條件式判斷

## 4. 顏色跨模式

- [ ] 4.1 將 `manual_add` / `island` / 其餘（slope）三條顏色分流移出 `m_editing_mode &&` 限定，使其在編輯模式與非編輯模式下皆生效
- [ ] 4.2 確認 `m_lock_unique_islands && is_island() && m_editing_mode` → BLUEISH 這條**維持不變**，`m_editing_mode` 限定不拿掉
- [ ] 4.3 確認 hover（CYAN）與 selected（REDISH）兩條**維持不變**，`m_editing_mode` 限定不拿掉
- [ ] 4.4 移除非編輯模式原本的 `{0.5,0.5,0.5,1.f}` 固定灰色 else 分支（被 type 分流取代）

## 5. 死碼移除

- [ ] 5.1 移除 `SupportTreeMesher.hpp` 的 `get_mesh_preview()` inline 函式
- [ ] 5.2 移除 `SupportTreeMesher.cpp` 的 `pinhead_preview()` 定義，`SupportTreeMesher.hpp` 的宣告一併移除
- [ ] 5.3 建置確認移除後無編譯錯誤（確認確實無其他呼叫端殘留，任務 1.7 的查證結果生效）
- [ ] 5.4 `head_mesh_body(h, steps, preview)` 的 `preview` 參數**保留不動**（見 design.md D5），僅移除的是 `get_mesh_preview()` 這一層 wrapper 與其唯一呼叫端

## 6. 驗證：視覺呈現

- [ ] 6.1 手動放置一顆支撐點，確認外觀（弧形端頭）與 auto 點一致，不再是簡化的直筒+平底盤
- [ ] 6.2 手動放置的支撐點旋轉視角，確認有光影變化（陰影隨視角改變），不再是固定不變的單一顏色
- [ ] 6.3 非編輯模式（Points 檢視）下，混合 auto island / auto slope / manual 點，確認顏色分別為 ORANGE / LIGHT_GRAY / CYAN，不是統一灰色
- [ ] 6.4 啟用「鎖定孤島」後切到非編輯模式，確認孤島顯示 ORANGE（type 顏色），不是 BLUEISH
- [ ] 6.5 編輯模式下 hover 與 selected 的顏色與修改前一致（CYAN / REDISH）

## 7. 驗證：不得回歸

- [ ] 7.1 切片輸出與修改前完全相同（`SupportTreeBuildsteps` 未被觸碰，只改 preview 渲染路徑）
- [ ] 7.2 Structure 模式仍 early return，不受影響
- [ ] 7.3 Clipping 開啟時逐點 clipped 判定不變
- [ ] 7.4 picking 命中範圍不受影響（本 change 未觸碰 `update_point_raycasters_for_picking_transform()`）
- [ ] 7.5 支撐點的幾何尺寸（per-point vs preset 的解析結果）與修改前一致——本 change 只改變畫法，不改變決定尺寸的規則

## 8. 驗證：效能與快取

- [ ] 8.1 auto 點與尺寸相同的 manual 點確認共用同一個 `HeadGeomKey` 快取項目（可透過中斷點或計數驗證 `init_from()` 呼叫次數）
- [ ] 8.2 500 點情境下旋轉視角，確認流暢度與 `perf-sla-support-points-preview-render` 完成時相同，無因統一畫法產生新的每幀成本
- [ ] 8.3 穩態連續多幀不呼叫 `GLModel::init_from()`，與既有效能特性一致

## 9. Follow-up（out of scope）

- 選定支撐點編輯參數時，其他 auto 點外觀被連動變化（Top 欄位讀值機制被雙重用途共用）→ 另案處理，根因已於 explore 討論查清（`read_preview_top_params_live()` 直接讀 widget 文字，不區分目前顯示的是誰的值）
- auto 點編輯後 `type` 是否該轉換為 `manual_add` → 併入 `fix-sla-support-preview-geometry-source-semantics` 的決策範圍
- 「已編輯」preset 標記在切換分頁後才出現的機制 → 根因尚未查清，暫緩，需要實機偵錯
