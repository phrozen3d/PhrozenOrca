## Why

Points preview 錐體的幾何參數有兩個來源——支撐點自身儲存的 per-point 值，或 Process tab 的 live preset 值。目前決定用哪一個的規則散在兩層，且**沒有任何一份文件定義過完整的預期行為**：

```cpp
// render_points()
const bool point_selected = m_editing_mode ? m_editing_cache[i].selected : false;
const bool use_stored_geometry = m_editing_mode && preview_use_stored_top(support_point, point_selected);

// preview_use_stored_top()
if (point_selected) return true;
return sp.type == manual_add && sp.has_explicit_geometry();
```

展開後的真值表：

| 模式 | 點類型 | 有 explicit geometry | 被選取 | 幾何來源 |
|---|---|---|---|---|
| 編輯 | auto | — | 否 | preset |
| 編輯 | auto | — | **是** | **stored** |
| 編輯 | manual | 否 | 否 | preset |
| 編輯 | manual | 否 | **是** | **stored** |
| 編輯 | manual | 是 | 否 | stored |
| 編輯 | manual | 是 | 是 | stored |
| 非編輯 | 任意 | 任意 | — | **preset（恆定）** |

實測反映出來的症狀：

1. **選取會改變外型。** 選中一顆尚無 explicit geometry 的點時，`point_selected` 使來源由 preset 切換為 stored，錐體外型當場改變。使用者無法判斷哪一個才是「這顆點真正的樣子」。
2. **live 參數只即時影響 auto 點。** 修改 Process tab 的 Top 欄位時，畫面上跟著變的只有 auto 生成的點；已放置的手動點維持建立當下的外型，之後怎麼改都不動。
3. **非編輯模式下手動點全部退回 preset**，與切片端不一致（此項已由 `fix-sla-support-preview-stored-geometry-in-auto-mode` 單獨處理）。
4. **`head_back_radius_mm`（Lower Diameter）未設定時，preview 退回 `pillar_radius`（Pillar Diameter 凍結值），切片端卻退回即時 preset。** 新放置的手動點若未經選取，調整「Lower Diameter」完全不影響其外觀，只有「Pillar Diameter」有效——但這條 `pillar_radius` fallback 只存在於 preview 端（`preview_sla_head_for_point()`），切片端的 `point_head_back_radius_mm()` 沒有這層分支（見 design.md D2b）。preview 與實際切出來的支撐頭尺寸可能不同，且沒有任何提示。
5. **想單純預覽下一顆手動點的外觀，卻連動到既有未選取的 auto 點。** 進入 Manual Editing 模式後，在 Process tab 調整參數是想看接下來手動點下去的點會長什麼樣，但因為未選取的 auto 點恆為 `use_stored_point == false`，會連帶跟著即時變形——即使那些點早就生成好、使用者根本沒打算動它們。這正是「live 參數只即時影響 auto 點」（症狀 2）反過來看的困擾（見 design.md D2c）。

第 1、2 項單獨看都可以解釋成刻意設計——選取時顯示該點的實際幾何、per-point 值優先於 preset——但兩者疊在一起，加上第 3 項，使得同一顆點在「選取 / 未選取 / 切到自動模式」三種狀態下可能顯示三種不同的尺寸。**這不是使用者能推理出來的行為。**

切片端沒有這個問題：`SupportTreeBuildsteps.cpp:693` 無條件套用 `point_*()` 助手的規則（per-point 值 ≥ 0 就用 per-point，否則退回 preset），既不看編輯模式也不看選取狀態。preview 與切片之間因此存在多處落差。

本議題於 `fix-sla-support-top-config-enum-set` 的驗收測試（A-3）期間浮現。當時的結論是「無法確認哪個是對的」——這正是本 change 要解決的：**先定義正確行為，再對齊實作。**

## What Changes

- **第一階段（語意定義，無程式碼變更，已完成）**：產出 preview 幾何來源的完整真值表，涵蓋編輯模式、點類型、是否有 explicit geometry、是否被選取四個維度，並與切片端的規則逐格比對。結論見 design.md D4。
- **第二階段（實作）**：依定義好的語意調整 `preview_use_stored_top()` 與 `render_points()` 的判定，使 preview 在所有狀態下都可預測，且與切片結果一致。
- 選取不應改變幾何來源（Q1 已定案為「否」）：移除 `preview_use_stored_top()` 的 `point_selected` 早退，選取只影響顏色。
- 仲裁粒度改為逐欄位，直接沿用切片端 `point_*()` 助手（Q2 已定案為「是」），preview／picking／切片三處共用同一套解析。
- live 參數編輯的作用對象維持現況（只影響尚無 explicit geometry 的欄位），不額外提供 UI 途徑；多選支撐點時的顯示／編輯語意另行定案，見 design.md D6。
- 手動點放置時，`freeze_process_top_into_point()` 移除 `head_back_radius_mm` 的 `pillar_radius` fallback，改為建立當下直接凍結全部 Top 欄位（見 design.md D2b、D5）。

### Non-goals

- 不改變切片端的參數解析規則。本 change 是讓 preview 對齊切片，不是反過來。
- 不改變 `point_*()` 助手（`SupportPoint.hpp:126-164`）的仲裁規則本身。
- 不改變 `has_explicit_geometry()` 的定義。
- 不改變支撐點的選取、hover、拖曳等互動行為，僅涉及**渲染時採用哪一組幾何參數**。
- 不重新設計 Top 欄位的 UI 佈局或新增控制項（除非第一階段的結論明確要求）。
- 不處理 per-point Top 欄位顯示失效與其 crash（→ `fix-sla-support-top-config-enum-set`，**為本 change 的前置條件**）。

### 與 `fix-sla-support-preview-stored-geometry-in-auto-mode` 的邊界

該 change 處理真值表的**最後一列**（非編輯模式恆用 preset），是一個根因與修法都已確定的窄修正，可獨立實施。

本 change 處理**其餘各列**——編輯模式下選取狀態造成的來源切換——需要先定義正確行為。

兩者互不衝突但相鄰，**建議先實施該 change**：它會消除「切到自動模式尺寸就變」這個變因，使本 change 第一階段的觀察更乾淨。

### 與 `fix-sla-support-auto-points-top-field-freeze` 的邊界（Change B，建議新增）

第一階段查證發現（design.md D4）：auto 生成點只有 `head_front_radius`（Upper Diameter）在生成當下寫入具體值，其餘四個 Top 欄位（`head_back_radius_mm`／`head_width_mm`／`head_penetration_mm`／`contact_sphere_radius`）維持 `SUPPORT_POINT_USE_PRESET`，會持續追蹤即時 preset 直到重新生成——這是**切片端真實會發生的行為**，不是 preview 顯示問題。

修正這個落差（讓 auto 生成時凍結全部欄位）的根因在 `SupportPointGenerator.cpp`，屬於切片端資料生成邏輯，不在本 change「只改 GUI 顯示」的檔案範圍內。**建議另立 `fix-sla-support-auto-points-top-field-freeze`（Change B）處理**，與本 change 平行、互不阻擋、檔案不重疊。詳細邊界與拆分理由見 design.md D5。

本 change 完成後，preview 會誠實顯示這個不對稱行為（Upper Diameter 凍結、其餘四個追蹤即時 preset），驗收時不得視為本 change 的回歸，見 tasks.md 6.1。

## Capabilities

### New Capabilities

- `sla-support-preview-geometry-source`：Points preview 錐體採用 per-point 儲存幾何或 live preset 幾何的完整判定規則。涵蓋編輯模式、點類型、explicit geometry、選取狀態四個維度的真值表，以及「同一顆點在不同 UI 狀態下顯示的尺寸必須可預測」與「preview 必須與切片結果一致」的不變式。

### Modified Capabilities

<!-- 無。實際的 requirement 增補視第一階段的語意定義結果而定；若結論影響 sla-support-points-preview 的既有 requirement，於第一階段結束後補列。 -->

## Impact

- **Primary**：`src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp`
  - `preview_use_stored_top()`（`:251`）— 移除，`point_selected` 早退不再需要
  - `preview_sla_head_for_point()`（`:287`）— 改為逐欄位呼叫 `point_*()` 助手
  - `render_points()`（`:750`）— `use_stored_geometry` 判定移除，改用逐欄位解析
  - `update_point_raycasters_for_picking_transform()` — picking 半徑須與顯示同步
  - `freeze_process_top_into_point()`（`:1823`）— 移除 `head_back_radius_mm` 的 `pillar_radius` fallback，改為建立當下直接凍結
  - `apply_process_top_option()` — 改為套用到全部已選取的點（多選同步），見 design.md D6
- **Reference（僅比對，預期零修改）**：`src/libslic3r/SLA/SupportTreeBuildsteps.cpp:693`、`src/libslic3r/SLA/SupportPoint.hpp:126-164` — 切片端的規則為對齊基準
- **不修改**：`src/libslic3r/SLA/SupportPointGenerator.cpp`（auto 生成邏輯）——見上方「與 Change B 的邊界」
- 不影響切片輸出、檔案格式或 profile
- 無 public API 變更
