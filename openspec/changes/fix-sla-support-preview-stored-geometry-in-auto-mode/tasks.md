## 0. 前置條件與實施順序

- 本 change **無硬前置**，根因與修法皆已從程式碼確定，可直接進入第 2 節
- 但 `fix-sla-support-top-config-enum-set` 是 crash，**應先處理**；本 change 的驗收會反覆選取與編輯支撐點，crash 未修時難以完整測試
- 本 change 是 `fix-sla-support-preview-geometry-source-semantics` 與 `fix-sla-support-points-invalidate-on-trafo-change` 的**建議前置**：它處理幾何來源真值表的最後一列（非編輯模式恆用 preset），先完成可消除「切到自動模式尺寸就變」這個變因

> 全域相依圖與實施順序見 [`openspec/changes/README.md`](../README.md)。

## 1. 根因確認（已於提案階段完成）

- [x] 1.1 確認 `render_points()`（`GLGizmoSlaSupports.cpp:750`）的 `use_stored_geometry = m_editing_mode && preview_use_stored_top(...)` 中，`m_editing_mode &&` 前綴使非編輯模式下恆為 false
- [x] 1.2 確認 `preview_sla_head_for_point()` 因此在非編輯模式走 `pin_r = live_upper_r` 等 preset 路徑，忽略 per-point 值
- [x] 1.3 確認切片端 `SupportTreeBuildsteps.cpp:693-705` 無條件取用 `sp.head_front_radius` 與 `point_*()` 助手，沒有「編輯模式」概念
- [x] 1.4 確認 `point_*()` 助手（`SupportPoint.hpp:126-164`）的規則為「per-point 值 ≥ 0 就用 per-point，否則退回 preset」，preview 與切片共用
- [x] 1.5 確認 `has_explicit_geometry()`（`SupportPoint.hpp:110`）第一個條件即 `type == manual_add`，故 auto 點不受本修正影響
- [x] 1.6 確認 `point_selected` 在非編輯模式已由上游強制為 false，移除前綴後規則自動退化為「`manual_add` 且有 explicit geometry」

## 2. 修正

- [x] 2.1 將 `render_points()` 的判定改為 `const bool use_stored_geometry = preview_use_stored_top(support_point, point_selected);`
- [x] 2.2 更新該處註解，說明規則與切片端一致且不依編輯模式而異
- [x] 2.3 確認 `update_point_raycasters_for_picking_transform()`（`:2511`）內的 `preview_use_stored_top(sp, m_editing_cache[i].selected)` **維持現狀**——查證該處本來就沒有 `m_editing_mode &&` 前綴，零 diff
- [x] 2.4 確認未引入第二條參數解析路徑：`preview_use_stored_top()` 全檔案僅兩處呼叫（`:762` render_points、`:2511` picking），皆直接呼叫同一函式，未複製或改寫任何判定邏輯；切片端 `SupportTreeBuildsteps.cpp` 共用同一組 `point_*()` 助手，未觸碰

## 3. 驗證：目標行為

- [x] 3.1 手動模式放置三顆 `support_head_front_diameter` 不同的支撐點，退出編輯模式後於 Points 視圖確認三顆錐體尺寸各自維持建立時的大小 — **Pass**：手動建完支撐點回到自動模式後，該支撐點顯示的大小保持一致
- [x] 3.2 切片後比對支撐樹的實際 head 直徑、head width、penetration、contact sphere 半徑與非編輯模式 preview 一致 — **Pass**
- [x] 3.3 手動點的 `support_head_back_diameter`、`support_segment_length`、`support_head_penetration`、`support_contact_diameter` 各自設不同值，確認四項在非編輯模式皆正確反映 — **Pass**

## 4. 驗證：不得回歸

- [x] 4.1 **auto 點顯示不變**：對同一組 auto 生成的點，比對修改前後 preview 錐體尺寸完全相同 — **Pass**
- [x] 4.2 **編輯模式行為不變**：混合 auto 與 manual 點，比對修改前後編輯模式下每顆錐體的幾何完全相同 — **Pass**
- [x] 4.3 **選中點仍用自身幾何**：編輯模式選中一顆點，確認其錐體仍依 `point_selected == true` 的既有規則解析 — **Pass**：再次進入手動模式後點選剛才新增的點，Top 欄位有正確切換成該點的 preset 顯示（`fix-sla-support-top-config-enum-set` 的 per-point Top 顯示功能在本次修改後仍正常運作，間接證明選中點的幾何解析路徑未受影響）
- [x] 4.4 **live preset 仍驅動無 explicit geometry 的點**：修改 preset 的 `support_head_front_diameter` 後重繪，確認 auto 點跟著變、有 explicit geometry 的 manual 點不變 — **Pass**
- [x] 4.5 Structure 模式仍 early return，不受影響 — **Pass**
- [x] 4.6 Clipping 開啟時逐點 clipped 判定不變 — **Pass**

## 5. 驗證：picking 與效能

- [x] 5.1 編輯模式下 hover 一顆 explicit geometry 與 preset 差異明顯的 manual 點，確認命中範圍與可見錐體一致，無「看得到點不到」或「點得到看不到」 — **Pass**
- [x] 5.2 確認 `perf-sla-support-points-preview-render` 建立的 `m_head_model_cache` 在非編輯模式下 key 數為「1 + 相異手動幾何組數」，且穩態每幀 `init_from()` 呼叫次數仍為 0 — **Pass**，未引發效能問題
- [x] 5.3 500 點 auto + 若干手動點的情境下旋轉視角，確認流暢度與該 change 完成時相同（無因 key 增加而退化）— **Pass**
- [x] 5.4 刻意建立超過 64 組相異手動幾何，確認觸發整份 `clear()` 後重新填充，畫面結果仍正確 — **Pass**

## 6. Follow-up（out of scope）

- 編輯模式下選取狀態造成幾何來源切換、以及仲裁粒度與切片端不同（整顆點 vs 逐欄位）→ `fix-sla-support-preview-geometry-source-semantics`。本 change 處理真值表的最後一列（非編輯模式恆用 preset），該 change 處理其餘各列；**建議本 change 先實施**，以消除「切到自動模式尺寸就變」這個變因
- `sla_trafo` 改變後前端支撐點快取不失效 → `fix-sla-support-points-invalidate-on-trafo-change`
- 支撐點 undo/redo 資料不正確 → `fix-sla-support-points-undo-snapshot`
- `cfg.set()` 對 `coEnum` 誤用導致 per-point Top 欄位顯示失效 → `fix-sla-support-top-config-enum-set`
