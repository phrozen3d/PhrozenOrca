## 0. 前置條件與實施順序

- [x] 0.1 `fix-sla-support-top-config-enum-set` 已完成（2026-08-03 archive，45/45）。**硬前置已滿足**：crash 已修好，選中點編輯 Top 欄位不再使應用程式終止，可進行第 1 節所需的觀察
- [x] 0.2 `fix-sla-support-preview-stored-geometry-in-auto-mode` 已完成（2026-08-03 archive，23/23）。**建議前置已滿足**：「切到自動模式尺寸就變」這個變因已消除（見 design.md D3）

> 全域相依圖與實施順序見 [`openspec/changes/README.md`](../README.md)。

## 1. 語意定義（產品決策，須先完成才進入實作）

- [x] 1.1 已展開現行真值表：編輯模式 × 點類型 × 有無 explicit geometry × 是否選取 → 幾何來源（見 proposal.md）
- [x] 1.2 已確認切片端（`SupportTreeBuildsteps.cpp:693`）無條件套用 `point_*()` 助手，**逐欄位**仲裁，且無「編輯模式」與「選取狀態」概念
- [x] 1.3 已確認 preview 為**整顆點**二選一（`use_stored_point` 單一布林決定全部欄位），與切片端的粒度不同
- [x] 1.4 **關鍵事實查證（已確認）**：auto 生成的點其 `sp.head_front_radius` **不是**「即時綁定 preset」，而是**生成當下 preset 值的快照**——`SupportPointGenerator.cpp:238/266/294` 建立每一顆 auto 點時把 `config.head_diameter/2` 寫死進 `head_front_radius`，之後不會再變。生成演算法沒有依 island 大小調整（design.md 原本的猜測不成立），同批生成的所有 auto 點頭部半徑相同。**但這是唯一在生成當下就寫入具體值的 Top 欄位**——其餘四個欄位（`head_back_radius_mm`／`head_width_mm`／`head_penetration_mm`／`contact_sphere_radius`）維持 `SUPPORT_POINT_USE_PRESET`，會持續追蹤即時 preset 直到下次重新生成。這一段的完整證據鏈與影響見 design.md D4。修復「auto 生成時凍結全部欄位」不在本 change 範圍內，另立 `fix-sla-support-auto-points-top-field-freeze`（Change B，見本文件第 7 節與 `openspec/changes/README.md`）
- [x] 1.5 **Q1 決策（已確認）**：選取不應改變幾何來源。採 design.md D1 選項 A——移除 `preview_use_stored_top()` 的 `point_selected` 早退，選取只影響顏色
- [x] 1.6 **Q2 決策（已確認）**：仲裁粒度改為**逐欄位**，直接沿用切片端 `SupportPoint.hpp` 的 `point_*()` helper，preview／picking／切片共用同一套解析。`use_stored_point` 參數與 `preview_use_stored_top()` 隨之移除
- [x] 1.7 **Q3 決策（已確認，範圍略擴大於原提案）**：多選時面板顯示**最後一個選取點**的值；編輯任一 Top 欄位時，即時同步寫入**全部已選取的點**（不只是顯示中的錨點）。「live 參數作用對象是否需要更明確 UI 途徑」維持傾向「行為正確、另案處理」，不納入本 change
- [x] 1.7a **具體案例佐證**（見 design.md D2a）：已確認編輯 auto 點的 Top 參數後，`sp.type` 不會轉換成 `manual_add`，導致取消選取後 preview 悄悄放棄剛編輯的值（`has_explicit_geometry()` 卡在 `type == manual_add` 這個條件），但切片端不檢查 type、仍會採用該值——preview 與切片自此分歧且無提示。此為 Q2 選逐欄位方案可自動解決的具體案例
- [x] 1.7b **第二個具體案例佐證**（見 design.md D2b）：於驗收 `fix-sla-support-point-cone-picking` 期間發現，新放置手動點的 `head_back_radius_mm` 故意留白、由 `pillar_radius` fallback 驅動（`preview_sla_head_for_point()`），但切片端的 `point_head_back_radius_mm()` 沒有這層 fallback、未設定時直接退回即時 preset——調整「Lower Diameter」對這類點的 preview 完全不生效（只有「Pillar Diameter」有效），且 preview 尺寸與實際切片尺寸可能不同，無提示。同為 Q2 逐欄位方案可自動解決的具體案例
- [x] 1.7c **第三個具體案例佐證**（見 design.md D2c）：於驗收 `fix-sla-support-point-picking-live-refresh` 期間，使用者提出「進入 Manual Editing 後想先調參數預覽下一顆手動點的外觀，但這樣會連動到既有未選取的 auto 點」——本質上就是 Q2：切片端無條件用 `sp.head_front_radius`，採逐欄位方案後未選取 auto 點會自然凍結在產生當下的值，選中點仍可透過 `apply_process_top_option()` 即時編輯，剛好同時滿足「不連動」與「選中仍可編輯」兩個要求，不需要另外發明凍結機制
- [x] 1.7d **第四個具體案例佐證（本輪確認）**：`freeze_process_top_into_point()`（`:1823`）建立手動點時，`head_back_radius_mm` 故意留白、靠 `pillar_radius` fallback 撐出 Lower Diameter 的預覽效果（design.md D2b），但這條 fallback 只存在於 preview，切片端沒有——手動點的 Lower Diameter 因此永遠不會真正凍結。此修復點與本 change 其餘修改同在 `GLGizmoSlaSupports.cpp`，**改列入本 change 範圍**（不需另立 change）
- [x] 1.8 **書面真值表已回填至 design.md D4**，逐格標註與切片端的對應關係，並標註哪些欄位在本 change 完成後即凍結、哪些仍待 Change B
- [x] 1.8a 已不適用——Q2 採逐欄位方案，`apply_process_top_option()` 是否轉換 `sp.type` 的問題自動消失（見 design.md D2a 結論）

## 2. 實作

- [x] 2.1 依 1.5 結論移除 `preview_use_stored_top()` 及其 `point_selected` 早退——整個函式已刪除，`render_points()`／`update_point_raycasters_for_picking_transform()` 不再呼叫它
- [x] 2.2 依 1.6 結論調整 `preview_sla_head_for_point()` 的解析：改為逐欄位直接套用 `point_*()` 助手（`point_head_back_radius_mm`／`point_head_width_mm`／`point_uses_contact_sphere`／`point_contact_sphere_radius_mm`／`point_head_penetration_mesh_mm`），`head_front_radius` 因無 sentinel 恆讀 `sp.head_front_radius`，與 `SupportTreeBuildsteps` 共用同一套規則
- [x] 2.3 `use_stored_point` 參數已隨 `preview_use_stored_top()` 一併移除，`PreviewTopParams::upper_r_mm` 亦因不再被讀取而移除，無死碼殘留
- [x] 2.4 `render_points()` 呼叫端已更新為 `preview_sla_head_for_point(support_point, scaled_normal, top_params)`（3 參數），`point_selected` 變數保留（仍用於顏色判斷，不再傳入幾何解析）
- [x] 2.5 `update_point_raycasters_for_picking_transform()` 的 `pick_r` 解析已同步改為呼叫相同的 3 參數 `preview_sla_head_for_point()`
- [x] 2.6 已確認 preview、picking、切片三處共用單一解析路徑（`point_*()` 助手），不留第二套規則
- [x] 2.7 已修復 `freeze_process_top_into_point()`：移除 `head_back_radius_mm = SUPPORT_POINT_USE_PRESET`，改為 `clamp_support_diameter_mm(process_top_float_live("support_head_back_diameter", 1.0f)) * 0.5f`，建立手動點當下直接凍結（見 1.7d、design.md D2b）。**額外發現並修復同根因的第二處**：`apply_process_top_option()` 的 `support_pillar_diameter` 分支原本也會把已選取點的 `head_back_radius_mm` 重置回 `SUPPORT_POINT_USE_PRESET`（同一種「Pillar Diameter 頂替 Lower Diameter」的錯誤耦合），已一併移除，改為 Pillar Diameter 與 Lower Diameter 完全獨立（見 4.7）
- [x] 2.8 `apply_process_top_option()` 原本就已迴圈套用到 `m_editing_cache` 中全部 `selected == true` 的項目（多選同步寫入部分無需修改）。面板顯示邏輯改為讀取新增的 `m_last_selected_index`（`select_point()`／`unselect_point()` 維護，取代原本「取第一個選取點、break」的寫法），實現「顯示最後一個選取點」（見 design.md D6）
- [x] 2.9（新增）建置驗證：`libslic3r_gui.vcxproj`（Release / x64）增量編譯通過，`GLGizmoSlaSupports.cpp` 無編譯錯誤或新增警告

## 3. 驗證：選取不改變尺寸

- [x] 3.1（選測，需舊版 3mf 專案檔，未測——手邊無舊檔案，見說明，不影響結論）
- [x] 3.2（主要驗證，已通過）選取一顆自動生成的點，尺寸不變
- [x] 3.3 取消選取，尺寸不變
- [x] 3.4 反覆選取／取消選取數次，外型完全穩定、無跳動
- [x] 3.5 多選兩顆以上尺寸不同的點，面板正確顯示**最後一個選取點**的值
- [x] 3.6 多選狀態下編輯任一 Top 欄位，全部已選取的點同步套用，未選取的點不受影響
- [x] 3.7 多選狀態下改變選取焦點，面板正確切換，先前編輯不被覆蓋

> 第 3 組實測期間額外發現並修復一個回歸：單獨點擊（非 shift 多選）選取一顆點時面板未更新——`on_mouse()`／`on_start_dragging()` 兩處直接操作 `.selected`、繞過 `select_point()`，導致 `m_last_selected_index`（2.8 新增）沒被同步更新。已在兩處補上 `m_last_selected_index = int(m_hover_id);`，重新編譯通過，複測單選/多選皆正確。

## 4. 驗證：與切片一致

> 4.1/4.3 原文字面上的「只設定過 `head_width_mm` 的手動點」「完全未設定的點」在目前架構下無法透過正常操作產生——`freeze_process_top_into_point()` 建立手動點當下會一次凍結全部欄位，不會有「只設定一個」的手動點；`head_front_radius` 沒有 sentinel，不可能「完全未設定」（同 3.1 的發現，理由見 spec.md 的修正）。改用 **auto 點**：它天生就是「Upper Diameter 自身值、其餘四個追蹤 preset」的狀態，不需特別建構，兩項已合併改寫為 4.1。

- [x] 4.1（改用 auto 點）Upper Diameter 不受面板牽動；Lower Diameter／Segment Length／Penetration／Contact Sphere 皆跟著面板變動
- [x] 4.2 Structure 模式支撐頭實際尺寸與 Points preview 逐欄位相符
- [x] 4.3（已併入 4.1）
- [x] 4.4 編輯模式與非編輯模式渲染尺寸完全相同
- [x] 4.5（auto 點分段編輯）三段（未編輯／編輯 Segment Length／再編輯 Contact Diameter）皆符合逐欄位預期
- [x] 4.6 新建手動點的 Lower Diameter 於建立當下即凍結，之後面板改動不影響該點
- [x] 4.7 調整 Pillar Diameter 不連動已凍結的 Lower Diameter

## 5. 驗證：live preset 編輯的作用對象

- [x] 5.1 修改 Lower Diameter：auto 點跟著變（預期行為）、手動點不變（已凍結）
- [x] 5.1a 修改 Upper Diameter：auto 點與手動點皆不變
- [x] 5.2（auto 點＋選取編輯 Segment Length）Lower Diameter 跟著變、Segment Length／Upper Diameter 不變
- [x] 5.3 與 Structure 模式／切片結果一致

## 6. 驗證：不得回歸

- [x] 6.1 auto 點：Upper Diameter 完全不受面板牽動（本 change 修好）；其餘四欄位持續追蹤即時 preset（預期限制，待 Change B，非本 change 回歸）
- [x] 6.2 picking 命中範圍與可見錐體一致，無「看得到點不到」或「點得到看不到」
- [x] 6.3（僅完成粗略替代判準：大量支撐點操作無明顯卡頓）**精確版（除錯器觀察 `HeadGeomKey`／`init_from()` 呼叫次數）尚未執行**，留待有 debug build 環境時補測，見下方「尚待完成」
- [x] 6.4 Structure 顯示模式正常，無錯誤或崩潰
- [x] 6.5 clipping 剖切逐點顯示/隱藏行為正常
- [x] 6.6 Auto-generate → 完整切片流程正常，無錯誤訊息，支撐結構視覺合理（未做改動前 build 的 G-code diff 對照，見下方「尚待完成」）

## 8. 尚待完成（不阻擋本 change 結案，記錄供之後補測）

- 6.3 精確版：除錯器觀察 `m_head_model_cache.size() >= k_head_model_cache_limit`（`GLGizmoSlaSupports.cpp:813`）觸發頻率與穩態 `init_from()` 呼叫次數，需 debug build 環境
- 6.6 G-code diff 對照：需要本次改動前的舊 build 才能做同模型輸出比對

## 7. Follow-up（out of scope）

- 若 1.7 結論為「行為正確但不明顯」，live 參數作用範圍的 UI 提示 → 另案
- **auto 生成點僅 `head_front_radius` 於生成當下凍結，其餘四個 Top 欄位（Lower Diameter／Segment Length／Penetration／Contact Sphere）持續追蹤即時 preset、未隨生成凍結** → `fix-sla-support-auto-points-top-field-freeze`（Change B，新增，範圍為 `SupportPointGenerator.cpp`／`SLAPrintSteps.cpp`；與本 change 平行、互不阻擋、不重疊檔案；本 change 完成後會讓這個既有行為首次在 preview 上可見，見 6.1）
- 新增手動點在未按 Apply 前僅暫存於 `m_editing_cache`，離開編輯模式未提交即消失（現況；欄位凍結與此暫存機制彼此獨立，本 change 的欄位凍結修復不影響此機制，維持現況）
- 非編輯模式下手動點不套用 per-point 幾何 → `fix-sla-support-preview-stored-geometry-in-auto-mode`（本 change 的建議前置）
- per-point Top 欄位顯示失效與其 crash → `fix-sla-support-top-config-enum-set`（本 change 的硬前置）
- `sla_trafo` 變換後前端快取不失效 → `fix-sla-support-points-invalidate-on-trafo-change`
- 支撐點 undo/redo 資料不正確 → `fix-sla-support-points-undo-snapshot`
