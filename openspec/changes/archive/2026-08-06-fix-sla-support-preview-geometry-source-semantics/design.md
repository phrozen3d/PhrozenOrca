## Context

`preview_use_stored_top()` 的註解宣稱其意圖是對齊切片：

```cpp
// Match slice: manual points use per-point TOP stored at placement/edit;
// live Process Top is only for the next new point.
static bool preview_use_stored_top(const sla::SupportPoint &sp, bool point_selected)
{
    if (point_selected)
        return true;
    return sp.type == sla::SupportPointType::manual_add && sp.has_explicit_geometry();
}
```

但切片端（`SupportTreeBuildsteps.cpp:693`）根本沒有這個開關——它無條件套用 `point_*()` 助手：

```cpp
const double contact_r = point_contact_sphere_radius_mm(sp, m_cfg.contact_sphere_radius_mm);
const double mesh_pen  = point_head_penetration_mesh_mm(sp, m_cfg.head_penetration_mm,
                                                       double(sp.head_front_radius), contact_r);
heads.emplace_back(std::nan(""), sp.head_front_radius, 0., mesh_pen, ...);
```

而 `point_*()` 的規則是「per-point 值 ≥ 0 就用 per-point，否則退回 preset」，逐欄位獨立判斷。

**兩邊的粒度就不同**：切片是**逐欄位**仲裁（一顆點可以 head_width 用自己的、penetration 用 preset），preview 是**整顆點**二選一（`use_stored_point` 一個布林決定全部欄位）。`preview_sla_head_for_point()` 的寫法即為證：

```cpp
const double pin_r = use_stored_point ? double(sp.head_front_radius) : live_upper_r;
```

當 `use_stored_point` 為 false 時，即使 `sp.head_front_radius` 有實值也被忽略——這與切片端無條件取用 `sp.head_front_radius` 直接衝突。

`point_selected` 這個額外維度則完全沒有切片端的對應物，是純 UI 概念混進了幾何解析。

## Goals / Non-Goals

**Goals:**

- 產出一份**完整且書面化**的 preview 幾何來源真值表，涵蓋四個維度（已完成，見 D4）。
- 同一顆支撐點在任何 UI 狀態下顯示的尺寸可預測——特別是選取／取消選取不應無故改變外型。
- preview 顯示的尺寸與該點實際會被切片產生的尺寸一致。
- picking 半徑與顯示同步。
- 手動點放置時全部 Top 欄位（含 Lower Diameter）於建立當下即凍結，不依賴 `pillar_radius` 等切片端沒有的 fallback（見 D2b、D5）。
- 多選支撐點時，面板顯示最後一個選取點的值；編輯任一欄位同步套用到全部已選取的點（見 D6）。

**Non-Goals:**

- 不改變切片端的解析規則或 `point_*()` 助手。
- 不改變 `has_explicit_geometry()` 的定義。
- 不改變選取／hover／拖曳的互動行為。
- 不重新設計 Top 欄位 UI（除非第一階段結論明確要求）。
- **不修改 auto 生成流程**（`SupportPointGenerator.cpp`）——auto 點只有 `head_front_radius` 在生成當下凍結、其餘四個欄位持續追蹤即時 preset 的落差，另立 `fix-sla-support-auto-points-top-field-freeze`（Change B）處理，見 D5。

## Decisions

### D1. 先定義語意，再改實作——第一階段不寫任何程式碼

現況無法直接修，因為「正確行為是什麼」沒有答案。實測時的原話是「這邊無法確認哪個是對的」。

第一階段要回答的三個問題：

**Q1：選取一顆點時，錐體應該改變外型嗎？**

- **選項 A（選取不影響幾何）**：移除 `preview_use_stored_top()` 的 `point_selected` 早退。優點是外型穩定、與切片一致（切片沒有選取概念）；缺點是使用者在未選取時看到的是「解析後」的尺寸，若想確認某顆點自己存了什麼值，得靠 Top 欄位而非畫面。
- **選項 B（選取時顯示該點的 stored 值）**：保留現況。優點是選取即可看見該點的原始設定；缺點是外型會跳動，且該尺寸未必是實際會列印的尺寸——反而更容易誤導。

**傾向 A**：preview 的職責是「顯示這顆點會被印成什麼樣」，不是「顯示這顆點的資料欄位」。後者是 Top 欄位面板的職責（由 `fix-sla-support-top-config-enum-set` 修好後即可勝任）。

**Q2：仲裁粒度應該是逐欄位還是整顆點？**

切片是逐欄位。preview 目前是整顆點。若要真正對齊，preview 也應改為逐欄位——直接把 `preview_sla_head_for_point()` 改成呼叫 `point_*()` 助手，與切片端共用同一段程式碼。

- **為何逐欄位較正確**：`has_explicit_geometry()` 只要**任一**欄位被設過就回傳 true，於是一顆只改過 head_width 的點，其 head_front_radius 也會被當成「stored」使用——但該欄位可能從未被使用者設定過。逐欄位仲裁沒有這個問題。
- **代價**：`use_stored_point` 這個參數可能整個消失，`preview_use_stored_top()` 隨之退場。變動比預期大，但換來的是 preview 與切片共用同一套解析。

**Q3：live 參數編輯應該影響哪些點？**

現況：只影響尚無對應 per-point 值的點（實測時表現為「只有 auto 點跟著變」）。這其實是 `point_*()` 規則的自然結果，語意上是自洽的——已明確設定過的點不該被 preset 覆蓋。

需要確認的是**使用者是否理解這個行為**，以及是否需要提供明確的「套用至選中點 / 套用至全部」途徑。若答案是「行為正確但不明顯」，那是 UI 提示問題而非幾何解析問題，應另案處理。

### D2. 若採 Q2 的逐欄位方案：與切片端共用解析

最徹底的作法是讓 `preview_sla_head_for_point()` 直接套用與 `SupportTreeBuildsteps` 相同的解析：

```
pin_r      = sp.head_front_radius                          (切片無條件取用)
back_r     = point_head_back_radius_mm(sp, preset)
width      = point_head_width_mm(sp, preset)
contact_r  = point_uses_contact_sphere(sp, preset_sphere)
                 ? point_contact_sphere_radius_mm(sp, preset) : 0
mesh_pen   = point_head_penetration_mesh_mm(sp, preset, pin_r, contact_r)
```

- **為何這是首選**：preview 與切片不可能再漂移——它們會是同一段運算。目前三處（編輯模式 preview、非編輯模式 preview、切片）各有一套解析，本 change 加上 `fix-sla-support-preview-stored-geometry-in-auto-mode` 之後可收斂為一套。
- **需要先確認的事實**：auto 生成的點其 `sp.head_front_radius` 是否一定等於 preset 值？若是，逐欄位方案對 auto 點的顯示零影響；若否（例如生成器會依 island 大小調整），改動會使 auto 點的 preview 尺寸改變，需納入驗收。**這是第一階段必須查清的關鍵事實。**

### D2a. 具體案例佐證：編輯 auto 點會製造一個 preview 與切片分歧的隱藏 bug

於驗收 `fix-sla-support-preview-stored-geometry-in-auto-mode` 期間，透過 explore 討論找到一個具體案例，直接支持 D2 採「逐欄位方案」的理由——不只是「比較優雅」，而是「不採用的話有既存 bug」。

**案例**：編輯模式下選中一顆 auto 生成的點（`type == island` 或 `slope`），修改其 Top 參數。`apply_process_top_option()` 會把新值寫進 `sp.head_front_radius` / `sp.head_width_mm` 等欄位，但**從頭到尾不會碰 `sp.type`**——這顆點的 `type` 永遠維持 `island`/`slope`，不會變成 `manual_add`。

這造成一個選取狀態相關的落差：

```cpp
static bool preview_use_stored_top(const sla::SupportPoint &sp, bool point_selected)
{
    if (point_selected) return true;                                 // 選中時：無條件顯示剛編輯的值 → 看起來正常
    return sp.type == manual_add && sp.has_explicit_geometry();       // 取消選取：type 仍是 island/slope → false → 顯示回 live preset
}
```

選中時因為 `point_selected == true` 提早 return，preview 正確顯示編輯後的值。**取消選取的瞬間**，`has_explicit_geometry()` 的第一個條件 `type == manual_add` 卡住（type 從未被轉換），preview 悄悄放棄剛剛的編輯、退回 live preset 值——使用者毫無察覺。

更嚴重的是：切片端（`point_head_width_mm()` 等助手）完全不檢查 `type`，只檢查欄位本身是否 `>= 0`。所以**該欄位一旦被寫入實值，切片永遠會採用它**——但 preview 一旦取消選取就不再顯示它。preview 與實際列印結果從此分歧，且沒有任何提示。

**這正是 Q2 逐欄位方案能順手解決的問題**：若 `preview_sla_head_for_point()` 改成直接呼叫 `point_*()` 助手（跟切片端一樣，只看欄位 `>= 0`，不看 `type`），這個 bug 自動消失——不需要另外決定「該不該把 auto 點編輯後的 type 轉成 manual_add」，因為判斷邏輯根本不再問這個問題。反之，若 Q2 最終選擇維持現行「整顆點二選一、依 type 判斷」的方案，則**必須額外決定**是否要在 `apply_process_top_option()` 裡把 type 轉換成 `manual_add`，這會是本 change 的第三個決策點，而不只是 Q1/Q2/Q3。

**第二次確認（驗收 `fix-sla-support-top-params-live-read-isolation` 期間）**：使用者選中一顆 auto 點，切換 `support_contact_type`（None ↔ Sphere），選取期間顯示正確；取消選取後畫面又變回球體外觀。追查後是同一條根因鏈，只是這次由 `support_contact_type` 觸發而非 `head_front_diameter`：`apply_process_top_option()` 的 `support_contact_type` 分支同樣只寫 `sp.contact_sphere_radius`（切到 Sphere 寫入正值、切到 None 寫入 `0.f`），同樣不碰 `sp.type`；取消選取後 `has_explicit_geometry()` 卡在 `type == manual_add`，退回 live preset 的 contact type 顯示。這不是本次驗收的 change（`fix-sla-support-top-params-live-read-isolation`，只處理「widget 讀值是否被其他點的顯示污染」）造成的回歸，而是 D2a 這個既有 bug 透過另一個欄位再次被驗證到——`apply_process_top_option()` 裡所有會寫入 per-point 欄位的分支（`support_contact_type`、`support_head_front_diameter`、`support_head_back_diameter`、`support_head_penetration`、`support_pillar_diameter`、`support_segment_length`）都共用同一個 `sp.type` 不轉換的問題，不是單一欄位的個案。

**第三次確認（驗收 `fix-sla-support-top-field-restore-race` 期間）**：使用者選中一顆 auto 點編輯 `support_head_front_diameter`（本 D2a 最初舉的例子），取消選取後**該點自己**（不是其他點）跳回未編輯的外觀，且顏色沒有變成手動編輯後該有的樣子。與 3.1 的驗收目標（其他未選取的點是否被連動）是兩回事——3.1 本身通過（其他點全程未受影響，代表 `fix-sla-support-top-field-restore-race` 的修正有效），使用者觀察到的這個現象是 D2a 本尊的第三次重現，再次確認不是任何一次 read-isolation 相關修正的回歸。

### D2b. 第二個具體案例佐證：`head_back_radius_mm` 的 preview fallback 走了一條切片端沒有的路

於驗收 `fix-sla-support-point-cone-picking` 期間，使用者實測發現：新放置的手動點若未經選取，調整 Process tab 的「Lower Diameter」（`support_head_back_diameter`）完全不影響該點外觀，只有「Pillar Diameter」（`support_pillar_diameter`）有效——即使兩者是不同的欄位。

根因鏈：

1. `freeze_process_top_into_point()`（`GLGizmoSlaSupports.cpp:1823`）建立手動點時，`sp.head_back_radius_mm` **故意**留白為 `SUPPORT_POINT_USE_PRESET`（`:1833-1836`，註解明寫「讓 Pillar Diameter 可以預設驅動 back/base radius」），但 `sp.pillar_radius` 被凍結成建立當下的 Pillar Diameter 值。
2. `has_explicit_geometry()`（`SupportPoint.hpp:110`）判斷「這顆點是否有 explicit geometry」時，`pillar_radius > 0.f` 這一條**單獨**就成立（凍結後恆為正），使 `use_stored_point` 立刻變 true——即使 `head_back_radius_mm` 根本沒被設定過。
3. `preview_sla_head_for_point()`（`:259-267`）在 `use_stored_point == true` 時算 back radius：`head_back_radius_mm >= 0 ? head_back_radius_mm : (pillar_radius > 0 ? pillar_radius : live_lower_r)`——**多了一層 `pillar_radius` fallback，這層在切片端不存在**。

對照切片端的 `point_head_back_radius_mm()`（`SupportPoint.hpp:156-159`）：

```cpp
inline float point_head_back_radius_mm(const SupportPoint &sp, double preset_mm)
{
    return sp.head_back_radius_mm >= 0.f ? sp.head_back_radius_mm : float(preset_mm);
}
```

**沒有 `pillar_radius` 分支**——`head_back_radius_mm` 未設定時直接退回 `preset_mm`（切片當下的即時 preset 值）。這代表：對一顆 `head_back_radius_mm` 未設定的手動點，**preview 顯示的尺寸來自建立當下凍結的 Pillar Diameter，實際切出來的支撐頭尺寸卻來自切片當下的 Lower Diameter preset**——兩者可以是完全不同的數字，preview 與實際列印結果從此分歧，且沒有任何提示。這與 D2a 是同一種病灶（preview 為了處理選取/建立時機而長出切片端沒有的分支），只是發生在不同欄位。

**這也是 Q2 逐欄位方案能順手解決的另一個案例**：若 `preview_sla_head_for_point()` 直接呼叫 `point_head_back_radius_mm()`，這條多出來的 `pillar_radius` fallback 分支就不存在了，preview 自動與切片一致。反之，若最終不採逐欄位方案，這個 `pillar_radius` fallback 是否要保留（讓 Pillar Diameter 繼續能預設驅動 back radius，只是要確保 preview 與切片用同一套規則），會是額外要決定的事。

**第二次確認**：使用者從另一個角度重現同一個根因——在 Process tab 先調整 Lower Diameter，**再點擊產生新的手動點**，新點不會反映剛調的值（只有 Upper Diameter 會即時反映），且切到其他點再切回來也不會「保持」（因為 `head_back_radius_mm` 從頭到尾沒被寫入過，沒有可保持的值）。追查 `freeze_process_top_into_point()`（`GLGizmoSlaSupports.cpp:1823`）確認：`head_front_radius`、`pillar_radius` 等欄位在建立當下都會即時凍結 Process tab 的值，唯獨 `head_back_radius_mm` 被跳過（`:1833-1836` 的故意留白）。與原始案例（「新放置的手動點若未經選取，調 Lower Diameter 完全不影響外觀」）是同一行程式碼、同一根因，只是重現路徑從「先建立、後調整」換成「先調整、後建立」。

### D2c. 第三個具體案例佐證：想讓「調參數預覽下一顆點」不要連動到既有 auto 點，本質上就是在要求 Q2

於驗收 `fix-sla-support-point-picking-live-refresh` 期間，使用者提出一個使用情境：進入 Manual Editing 模式後，想先在 Process tab 調整參數、讓接下來手動點下去的點有不同外觀，但目前這樣做會讓**既有、未選取的 auto 點**也跟著變形——因為 `preview_use_stored_top()` 對未選取的 auto 點恆為 `false`，`preview_sla_head_for_point()` 因此無條件忽略 `sp.head_front_radius`（這個欄位其實一直存在、由產生演算法寫入），改用即時讀到的 `live_upper_r`。

**這個訴求與 Q2 是同一件事，不是新的第五個維度**：切片端無條件使用 `sp.head_front_radius`（見 Context），若 preview 改採 Q2 的逐欄位方案，未選取的 auto 點會自然「凍結」在它產生當下的 `sp.head_front_radius`，不再被 Process tab 的即時編輯帶動；而被選取、正在編輯的點則透過 `apply_process_top_option()` 直接寫回 `sp.head_front_radius`，preview 仍會即時反映——**兩個各自成立的既有機制組合起來，剛好就是使用者要的行為**，不需要另外發明一套「凍結／解凍」的新機制。

**額外影響（Q2 決策時需一併確認）**：

- 目前使用者能透過「調參數觀察 auto 點跟著變」間接預覽下一顆手動點會長什麼樣；採用逐欄位方案後 auto 點不再跟著動，這個非正式的預覽管道會消失。是否需要另外提供「下一顆點的預覽」機制，若需要，屬於另一個獨立的 UI 功能，不在本 change 範圍內。
- task 1.4 待查的事實（auto 生成的 `sp.head_front_radius` 是否恆等於 preset 值）現在多了一層意義：它同時決定「未選取 auto 點凍結後，畫面上看到的是什麼」——若恆等於 preset，凍結後看到的就是「產生當下 Process tab 的值」，符合直覺；若生成演算法會依 island 大小調整，凍結後每顆點尺寸可能略有差異，需要在驗收時確認這是預期行為而非回歸。

### D3. 與相鄰 change 的實施順序

`fix-sla-support-preview-stored-geometry-in-auto-mode` 處理真值表最後一列，根因與修法已確定，可獨立實施。

**建議先實施該 change**，理由：

1. 它消除「切到自動模式尺寸就變」這個變因，使本 change 第一階段的觀察不受干擾。
2. 若本 change 最終採 D2 的逐欄位方案，該 change 的修改會被自然吸收（`m_editing_mode &&` 前綴在逐欄位方案下不再有意義），不會白做——它先讓非編輯模式進入正確狀態，本 change 再統一解析路徑。

`fix-sla-support-top-config-enum-set` 是**硬前置條件**：crash 未修好前，選中點編輯 Top 欄位會使應用程式終止，無法完成本 change 第一階段所需的觀察。

### D4. 定案後的完整真值表（回應 task 1.8）

第一階段結論：Q1＝選取不影響幾何，Q2＝逐欄位、與切片端共用解析。合併後，preview 的幾何來源判定規則收斂成單一規則，不再有「編輯模式」「選取狀態」「點類型」這幾個維度：

```
resolved(field) =
    該欄位有自身設定值（sentinel 判斷同 point_*() helper，contact_sphere_radius >= 0 視為已設定）
        ? 該點自身儲存值
        : 即時 Process tab Top 欄位值（每幀讀取）
```

`head_front_radius`（Upper Diameter）是唯一例外：該欄位沒有 `SUPPORT_POINT_USE_PRESET` sentinel 設計，每一顆點永遠存著具體值（auto 生成時由生成器寫入、手動點由 `freeze_process_top_into_point()` 寫入、選取編輯時由 `apply_process_top_option()` 覆寫），因此其 resolved 值**恆為該點自身的 `head_front_radius`**，面板對它完全沒有直接影響力（除非透過上述三個會實際寫入該欄位的動作）。

逐欄位真值表：

| 欄位 | 該點是否有自身設定值 | 幾何來源 |
|---|---|---|
| Upper Diameter (`head_front_radius`) | — （無 sentinel，恆有值） | 恆為該點自身值 |
| Lower Diameter (`head_back_radius_mm`) | 是（`>= 0`） | 該點自身值 |
| Lower Diameter | 否（`< 0`） | 即時 preset |
| Segment Length (`head_width_mm`) | 是 | 該點自身值 |
| Segment Length | 否 | 即時 preset |
| Penetration (`head_penetration_mm`) | 是 | 該點自身值 |
| Penetration | 否 | 即時 preset |
| Contact Sphere (`contact_sphere_radius`) | 是 | 該點自身值 |
| Contact Sphere | 否 | 即時 preset |

與切片端 `SupportTreeBuildsteps.cpp:693` 的逐欄位規則完全相同，preview／picking／切片三處共用同一套判定。選取狀態只影響顏色，不出現在此表中。

**這張表描述的是「解析規則」本身，不是「欄位何時被寫入具體值」**——後者取決於各觸發點：

| 觸發點 | 會寫入具體值的欄位 |
|---|---|
| auto 生成（`SupportPointGenerator.cpp`，現況） | 僅 `head_front_radius`；其餘四個欄位維持 sentinel，**本 change 不修改此觸發點**，見 D5 |
| 手動點放置（`freeze_process_top_into_point()`，本 change 修復後） | 全部欄位 |
| 選取編輯（`apply_process_top_option()`，本 change 擴充後） | 使用者實際修改的欄位，套用到全部已選取的點 |

這代表本 change 生效後，**auto 點只有 Upper Diameter 一個欄位免受面板牽動**，其餘四個欄位仍會持續追蹤即時 preset，直到 auto 生成觸發點也被修復（Change B）。這不是本 change 的 bug，而是如實反映現有資料層級的落差——見 D5。

### D5. 與 `fix-sla-support-auto-points-top-field-freeze`（Change B）的邊界

D4 揭露的落差──auto 生成點只有 `head_front_radius` 在生成當下凍結，其餘四個欄位持續追蹤即時 preset──根因在 `SupportPointGenerator.cpp` 只對這一個欄位寫入具體值，而非 preview／picking 的判定邏輯問題。修正這個根因需要讓生成器對每一顆 auto 點寫入全部 Top 欄位的具體值，這是**切片端資料生成的行為變更**，不是 GUI 顯示邏輯的變更，因此不落在本 change 鎖定的 `GLGizmoSlaSupports.cpp` 範圍內。

拆分理由：

- **檔案完全不重疊**：本 change 動 `GLGizmoSlaSupports.cpp`；Change B 動 `SupportPointGenerator.cpp`（可能連動 `SLAPrintSteps.cpp` 讀取 preset 值傳給生成器的部分）。兩者互不依賴，可平行進行、獨立驗收、獨立 revert。
- **本 change 的價值不因此打折**：本 change 的核心承諾是「preview 忠實反映當下實際會被切出來的結果」，這個承諾在缺少 Change B 的情況下依然完整成立——只是「當下實際會被切出來的結果」本身（對 auto 點而言）目前就是「Upper Diameter 凍結、其餘四個欄位追蹤即時 preset」這個不對稱狀態。preview 誠實顯示這個不對稱，本身就是正確行為。
- **先做本 change 有額外好處**：現行 `preview_use_stored_top()` 對未選取 auto 點一律回傳 `use_stored_geometry = false`，五個欄位全部顯示成追蹤即時 preset——這個「整齊劃一」的錯誤掩蓋了 Upper Diameter 其實早就凍結的事實。本 change上線後，Upper Diameter 開始正確顯示凍結，其餘四個欄位維持追蹤，兩者的落差第一次在 UI 上變得可觀察、可驗收，讓 Change B 的問題重現與驗收更容易。

**驗收邊界**：本 change 驗收 auto 點時，只能要求 Upper Diameter 欄位「調面板不影響已生成的點」；其餘四個欄位維持追蹤即時 preset 是**預期行為，不是回歸**（見 tasks.md 6.1）。手動點不受此邊界限制——手動點的全部五個欄位都在本 change 範圍內凍結（`freeze_process_top_into_point()` 的修復不依賴 Change B）。

### D6. 多選編輯語意（Q3 決策的延伸）

原提案的 Q3 只問「live 參數編輯的作用對象是否需要更明確的 UI 途徑」，範圍是單點。討論過程中額外定案了多選情境的具體規則，補充如下：

- **顯示**：多選時面板顯示**最後一個選取點**的值（清單順序中最後被加入選取集合的點），不是任意一顆、不是平均值。
- **編輯**：面板編輯任一 Top 欄位時，即時同步寫入**全部已選取的點**的對應欄位——不只是顯示中的錨點。這與單選時「編輯即寫入該點」是同一條規則的自然延伸（`n=1` 時行為不變）。
- **切換錨點**：多選狀態下若選取集合的「最後一個」改變（例如以 shift 擴增/縮減選取範圍），面板顯示切換為新錨點的值，但先前已套用到其他點的編輯不會被覆蓋或還原。

實作影響：`apply_process_top_option()` 需要從「寫入單一選取點」改為「迴圈寫入 `m_editing_cache` 中所有 `selected == true` 的項目」；面板顯示邏輯（讀取哪個點的值來顯示）維持讀取「最後一個選取點」不變。

## Risks / Trade-offs

- **[未定語意就實作，改完仍不可預測]** → 本 change 最主要的風險。緩解：D1 明列三個待答問題與各自的選項傾向，tasks 第 1 節為獨立的定義階段，須產出書面真值表才進入實作。

- **[逐欄位方案改變 auto 點的顯示]** → 若 auto 生成的 `sp.head_front_radius` 不等於 preset，改動會讓大量 auto 點的 preview 尺寸改變，看起來像回歸。緩解：D2 末段列為第一階段必查事實；驗收需明確比對 auto 點改動前後的尺寸。

- **[移除 `point_selected` 早退後使用者失去確認途徑]** → 若採選項 A，使用者無法從畫面分辨某顆點存了什麼值。緩解：該職責由 Top 欄位面板承擔，而該面板將由 `fix-sla-support-top-config-enum-set` 修好；本 change 的驗收應確認兩者合起來足以讓使用者掌握每顆點的狀態。

- **[picking 與顯示脫節]** → 幾何解析改變後，`update_point_raycasters_for_picking_transform()` 若未同步會出現「看得到點不到」。緩解：驗收明列 hover 命中範圍須與可見錐體一致。

- **[與 perf change 的幾何快取互動]** → 解析規則改變會改變 `HeadGeomKey` 的分布。逐欄位方案下 key 數上界仍為「相異參數組合數」，不會爆增，但驗收應確認 64 筆門檻未被頻繁觸發。

## Migration Plan

無資料遷移、無檔案格式或 profile 變更。純顯示語意修正。

回退策略：第一階段無程式碼變更；第二階段的修改集中於 `preview_sla_head_for_point()` 與 `preview_use_stored_top()`，可獨立 revert。

## Open Questions（已全數定案，保留紀錄）

- **Q1：選取是否應改變幾何來源？** ✅ **否**。移除 `preview_use_stored_top()` 的 `point_selected` 早退，選取只影響顏色。
- **Q2：仲裁粒度改為逐欄位、與切片端共用解析？** ✅ **是**。D2a、D2b、D2c 三個具體案例（auto 點編輯後 type 不轉換、`pillar_radius` fallback、未選取 auto 點被連動）全部靠這個決定一次解決，不需要逐一處理。
- **Q3：live 參數編輯的預期對象是否需要更明確的 UI 途徑？** ✅ 維持現況（欄位未設定值才受面板牽動），不額外做 UI 提示，另案處理。多選情境的顯示／編輯語意已定案，見 D6。
- **auto 生成的點其 `sp.head_front_radius` 是否恆等於 preset 值？** ✅ **不是恆等於，是生成當下的快照**（見 D4、tasks 1.4）。生成器只對這一個欄位寫入具體值，其餘四個欄位維持追蹤即時 preset，本 change 不處理這個落差，另立 Change B（見 D5）。
- **若 Q2 最終不採逐欄位方案：`apply_process_top_option()` 編輯 auto 點時，`sp.type` 該不該轉換成 `manual_add`？** ✅ 已不適用——Q2 採逐欄位方案，此問題自動消失。
