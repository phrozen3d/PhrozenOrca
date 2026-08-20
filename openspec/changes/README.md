# Active changes — 相依關係與實施順序

`openspec list` 依修改時間排序，不表達相依關係；OpenSpec 本身也沒有原生的依賴機制。本檔為 **active change 之間先後順序的單一事實來源**。

各 change 的 `tasks.md` 第 0 節記載**自身**的前置條件（穩定的區域事實）；全域圖只維護在這裡，避免多份副本漂移。

新增 change 時請一併更新本檔。change archive 後從表中移除。

---

## 相依圖

```
fix-sla-support-points-undo-snapshot         ← 無前置（範圍待其 task 1.8 決定）
```

**暫緩（不在本分支範圍）**：`fix-sla-trafo-oblique-mirror-decomposition`（原名 `fix-sla-support-points-invalidate-on-trafo-change`）——診斷確認根因在核心引擎 `SLAPrint::sla_trafo()` 本身，非 GUI 前端問題，已超出本分支範圍，待合併回 `resin-dev` 主分支後另外評估。見下方「暫緩項目」。

## SLA 支撐點群組（有順序約束）

| 順序 | Change | 前置 | 說明 |
|---|---|---|---|
| 1 | `fix-sla-support-points-undo-snapshot` | 無 | 第 1 節為契約定義階段。**其 task 1.8（30 秒）會決定本 change 的範圍是否縮小**，建議最先執行該項 |

已完成：
- `fix-sla-support-auto-points-top-field-freeze`（Auto 生成點只有 `head_front_radius` 於生成當下凍結，其餘四個 Top 欄位持續追蹤即時 preset，與切片端另外兩個既有 bug 疊加，導致凍結行為不完整或不可預期；2026-08-07 archive，58/58。生成器改為凍結全部五個 Top 欄位；驗收期間連環發現並修復四個既有問題：①`SupportTreeBuildsteps::filter()` 的 `back_r` 解析繞過共用 helper，只在手動點才讀自身欄位；②柱體「加粗回即時 preset」的結構安全機制（2022 年即存在）被本 change 意外喚醒、只對 auto 點生效，整個機制連同 `allow_widening` 參數一併移除；③Auto 模式 Apply 按鈕的 dirty-tracking 先是漏追蹤 Top 欄位、後又因 wx 事件時序問題而偵測不準，決定整個機制移除、按鈕改為恆為可按；④`auto_generate()` 沒有主動同步面板當下值，依序編輯多欄位只有部分生效，比照既有「放置手動點」路徑補上 `flush_process_top_fields_to_config()`。與 `fix-sla-support-preview-geometry-source-semantics` 的邊界見雙方 design.md D5。額外記錄一個確認為既有、與本 change 無關的純視覺問題——Process tab「已修改」提示偶爾延遲顯示，根因追溯到 2026-06-16 的既有 UI 刷新機制——列入其 tasks.md Follow-up，未處理）
- `fix-sla-support-preview-geometry-source-semantics`（Points preview 幾何來源判定散亂：選取會改變外型、live 參數只影響部分點、手動點 Lower Diameter 靠 `pillar_radius` fallback 卻與切片端不一致；2026-08-06，tasks 第 1-6 節共約 40 項全數通過，僅 6.3／6.6 各留一項需 debug build／舊版對照的精確驗證，記於其 tasks.md 第 8 節，不阻擋結案。改為逐欄位仲裁、直接沿用切片端 `point_*()` helper，preview／picking／切片三處共用同一套解析；新增多選顯示/編輯語意（最後選取點為準，編輯同步套用全部選取點）。實作期間額外發現並修復一個回歸：單獨點擊選取繞過 `select_point()`，導致新增的多選錨點追蹤失效，已修正。與 `fix-sla-support-auto-points-top-field-freeze` 的邊界見其 design.md D5——後者修 auto 點生成端尚未凍結的四個欄位，是平行、非阻擋的獨立範圍）
- `fix-sla-support-top-config-enum-set`（選中支撐點編輯 Top 欄位並失焦即終止應用程式的 crash；2026-08-03 archive，45/45）
- `fix-sla-support-preview-stored-geometry-in-auto-mode`（非編輯模式下手動點不套用 per-point 幾何；2026-08-03 archive，23/23）——`fix-sla-support-preview-geometry-source-semantics` 與 `fix-sla-support-points-invalidate-on-trafo-change` 的建議前置皆已滿足
- `fix-sla-support-preview-visual-parity`（manual 點與 auto 點在 preview 上的形狀／光影／顏色統一；2026-08-04 archive，34/34，與支撐點群組其他 change 獨立無交集）
- `fix-sla-support-point-cone-picking`（Points preview 的 cone/back-sphere raycaster 從未接上，實際只有 pin 端一顆小球能點；2026-08-04 archive，31/31。實作時發現兩處與提案不符：cone 高度應為 `fullwidth()-r_back_mm` 而非 `width_mm`，且 back 球赤道以外需另加一顆 `back_sphere` raycaster 才能涵蓋，皆已修正並記錄於其 design.md）⚠️ 其 `pin_sphere` 球心定位有一個未被驗收發現的既有缺陷（直接用錨點而非真正球心），由 `fix-sla-support-point-picking-live-refresh` 於驗收期間發現並修正
- `fix-sla-support-point-picking-live-refresh`（picking raycaster 只在離散事件才重算，Process tab Top 欄位即時編輯時視覺與可點範圍脫節，改成比照 render_points() 每幀刷新；2026-08-04 archive，24/24。實作時額外發現並修正兩個問題：`pin_sphere` 球心一直沒對準真正的 pin/contact 球心（`fix-sla-support-point-cone-picking` 遺留的既有缺陷）；每幀呼叫本身會覆寫 `render_points()` 剛設定的 clipping inactive 狀態，改為函式自行判斷 clipping，不依賴呼叫順序）
- `fix-sla-support-top-params-live-read-isolation`（選定支撐點編輯 Top 參數時，其他 auto 點的 preview 外觀被連動污染；2026-08-04 archive，19/19。根因是 live 讀值機制不知道 widget 目前被借用來顯示選中點的值，修法是有選取時跳過 widget 讀值、落到既有的 preset fallback。驗收期間再度確認 `fix-sla-support-preview-geometry-source-semantics` D2a 的既有問題（auto 點編輯後 type 不轉換，取消選取後跳回 preset），透過 `support_contact_type` 欄位驗證，已記入該 change）
- `fix-sla-support-top-field-restore-race`（取消選取瞬間到 widget 真正還原之間有時序空窗，`has_selected_support_points()` 守門邏輯在此空窗誤判「沒有選取」而讀到尚未還原的借用值，導致其他未選取點閃現；2026-08-04 archive，21/21。修法是守門條件疊加更精確的 `TabSLAPrint::is_support_point_top_field_active()`，兩者取 OR、不是替換。驗收期間第三度確認 D2a（這次透過 `support_head_front_diameter`，被編輯的 auto 點自己取消選取後跳回未編輯外觀），已記入該 change）
- `fix-sla-undo-redo`（SLA undo/redo 缺口：GLGizmoHollow 的 `on_save/on_load` 為空 stub、GLGizmoDrill 的 slider 變更未 snapshot；2026-08-08 archive。**結案時盤點發現本 change 實際分兩批命運不同**：GLGizmoHollow 半部 `2026-05-07` 落地（commit `be622a039`）後至今仍是現行程式碼，判定有效並併入主 spec `sla-hollow-gizmo-undo-redo`；GLGizmoDrill 半部（`begin_size_change`/`apply_size_change`）雖然也曾落地（commit `2da76fa12`），但 6 天後即被另一獨立 change 的 commit `0f302f003`（對應已 archive 的 `drill-apply-only-undo`）整個取代為 `m_working_holes` pending-apply 模型，現行程式碼已不存在，判定死設計、不併入主 spec、剩餘的 8.1/8.2 自動化回歸不再要求執行。Drill 的 undo/redo 現況與後續演進見 `resin-mode-scoped-undo-redo`（`phrozen-resin-dev-scoped-undo-redo` 分支，進行中；⚠️ 2026-08-09 方向修正：原本要把 Hollow/Drill/SlaSupports 統一納入 scoped sub-stack 機制，Section 7 驗證期間發現與既有單一 stack 架構不相容，改為退回單一 main stack，見其 design.md Decision G）

### 硬前置 vs 建議前置

- **硬前置**：未完成則後者無法進行或無法驗證。
- **建議前置**：反序可行，但會增加干擾變因或重工。

## 其他 active change（無順序約束）

（無）

已完成：
- `fix-sla-preview-slice-z-alignment`（SLA Preview 粉紅截面與灰色 3D 模型 Z 高度錯位；2026-08-14 archive，8/8。根因：`m_sla_layers_z` 以 `material_config.initial_layer_height` 為 layer 0 的 Z 偏移，但切片格柵 `ilhd` 取自 `object_config.layer_height`，Phrozen Speed Plus - Black 與 Tough ABS-like+ 兩支材料 `initial_layer_height=0.1mm`、`layer_height=0.05mm`，造成固定 1 層（0.05 mm）偏移，在支撐頂端交界處視覺明顯。修正：`m_sla_layers_z` 改為 `rec.print_level() × SCALING_FACTOR + elevation`；`_render_sla_slices()` key_high/key_low 改為 `(clip_z ∓ elevation) / SCALING_FACTOR`，統一座標基準）

## 待決事項（阻擋對應 change 進入實作）

| Change | 待決 | 決定什麼 |
|---|---|---|
| `fix-sla-support-points-undo-snapshot` | tasks 1.5 的語意決策 | S1 結果導向 / S2 設定導向 / S3 不可還原 |
| `fix-sla-support-points-undo-snapshot` | tasks 1.3 的量測 | 寫入 `mo->sla_support_points` 是否必然中斷背景運算，決定 S1 是否可行 |

## 暫緩項目（不在本分支處理，待合併回 resin-dev 後評估）

| Change | 診斷狀態 | 暫緩原因 |
|---|---|---|
| `fix-sla-trafo-oblique-mirror-decomposition`（原名 `fix-sla-support-points-invalidate-on-trafo-change`） | **診斷已完成**：根因為 `SLAPrint::sla_trafo()`（`SLAPrint.cpp:235-269`）矩陣分解-重組邏輯，在非軸對齊旋轉 + 奇數次鏡射時因 Eigen `computeRotationScaling()` 產生的 skew 未被檢測而算錯，與旋轉軸/鏡射軸皆無關，只取決於是否同時滿足這兩個條件（見其 design.md D1） | 根因在核心引擎程式碼（`SLAPrint.cpp`），不是 GUI 前端問題；且可能影響實際切片輸出、不只是 preview（尚待實測確認嚴重度，見其 proposal.md），修正風險與測試範圍比本分支其餘 GUI 修正大，不適合混在一起處理。`fix-sla-support-point-cone-picking` 的 6.12a/6.12b/6.12c 隨此一併暫緩 |
