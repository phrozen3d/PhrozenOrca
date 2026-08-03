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

- 產出一份**完整且書面化**的 preview 幾何來源真值表，涵蓋四個維度。
- 同一顆支撐點在任何 UI 狀態下顯示的尺寸可預測——特別是選取／取消選取不應無故改變外型。
- preview 顯示的尺寸與該點實際會被切片產生的尺寸一致。
- picking 半徑與顯示同步。

**Non-Goals:**

- 不改變切片端的解析規則或 `point_*()` 助手。
- 不改變 `has_explicit_geometry()` 的定義。
- 不改變選取／hover／拖曳的互動行為。
- 不重新設計 Top 欄位 UI（除非第一階段結論明確要求）。

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

### D3. 與相鄰 change 的實施順序

`fix-sla-support-preview-stored-geometry-in-auto-mode` 處理真值表最後一列，根因與修法已確定，可獨立實施。

**建議先實施該 change**，理由：

1. 它消除「切到自動模式尺寸就變」這個變因，使本 change 第一階段的觀察不受干擾。
2. 若本 change 最終採 D2 的逐欄位方案，該 change 的修改會被自然吸收（`m_editing_mode &&` 前綴在逐欄位方案下不再有意義），不會白做——它先讓非編輯模式進入正確狀態，本 change 再統一解析路徑。

`fix-sla-support-top-config-enum-set` 是**硬前置條件**：crash 未修好前，選中點編輯 Top 欄位會使應用程式終止，無法完成本 change 第一階段所需的觀察。

## Risks / Trade-offs

- **[未定語意就實作，改完仍不可預測]** → 本 change 最主要的風險。緩解：D1 明列三個待答問題與各自的選項傾向，tasks 第 1 節為獨立的定義階段，須產出書面真值表才進入實作。

- **[逐欄位方案改變 auto 點的顯示]** → 若 auto 生成的 `sp.head_front_radius` 不等於 preset，改動會讓大量 auto 點的 preview 尺寸改變，看起來像回歸。緩解：D2 末段列為第一階段必查事實；驗收需明確比對 auto 點改動前後的尺寸。

- **[移除 `point_selected` 早退後使用者失去確認途徑]** → 若採選項 A，使用者無法從畫面分辨某顆點存了什麼值。緩解：該職責由 Top 欄位面板承擔，而該面板將由 `fix-sla-support-top-config-enum-set` 修好；本 change 的驗收應確認兩者合起來足以讓使用者掌握每顆點的狀態。

- **[picking 與顯示脫節]** → 幾何解析改變後，`update_point_raycasters_for_picking_transform()` 若未同步會出現「看得到點不到」。緩解：驗收明列 hover 命中範圍須與可見錐體一致。

- **[與 perf change 的幾何快取互動]** → 解析規則改變會改變 `HeadGeomKey` 的分布。逐欄位方案下 key 數上界仍為「相異參數組合數」，不會爆增，但驗收應確認 64 筆門檻未被頻繁觸發。

## Migration Plan

無資料遷移、無檔案格式或 profile 變更。純顯示語意修正。

回退策略：第一階段無程式碼變更；第二階段的修改集中於 `preview_sla_head_for_point()` 與 `preview_use_stored_top()`，可獨立 revert。

## Open Questions

- **Q1：選取是否應改變幾何來源？** 傾向「否」，待確認。
- **Q2：仲裁粒度改為逐欄位、與切片端共用解析？** 傾向「是」，但需先查清 D2 末段的事實。
- **Q3：live 參數編輯的預期對象是否需要更明確的 UI 途徑？** 若結論為「行為正確但不明顯」，另案處理。
- **auto 生成的點其 `sp.head_front_radius` 是否恆等於 preset 值？** 決定 D2 方案對 auto 點顯示的影響範圍。
