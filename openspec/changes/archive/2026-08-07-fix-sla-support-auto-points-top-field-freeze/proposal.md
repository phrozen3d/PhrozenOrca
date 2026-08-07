## Why

Auto 生成支撐點時（`SupportPointGenerator.cpp:238/266/294`），生成器只把 `head_front_radius`（Upper Diameter）寫入生成當下的 preset 具體值；其餘四個 Top 欄位——`head_back_radius_mm`（Lower Diameter）、`head_width_mm`（Segment Length）、`head_penetration_mm`（Penetration）、`contact_sphere_radius`（Contact Sphere）——維持 `SUPPORT_POINT_USE_PRESET`（-1）sentinel，不會在生成當下被凍結。

切片端 step 失效規則（`SLAPrint.cpp:1120-1149`）證實這是真實的**切片輸出**行為，不只是 GUI 顯示問題：Top 面板這四個欄位的變動（`support_head_back_diameter`／`support_segment_length`／`support_head_penetration`／`support_contact_type`／`support_contact_diameter`）只會使 `slaposSupportTree` 失效，不會使 `slaposSupportPoints` 失效。也就是說，只要這四個欄位在點生成後又被使用者於 Process tab 調整過（哪怕沒有按下「Apply/Auto-generate」重新生成），下一次重新切片（包括只是切到 Structure 檢視模式）時，`slaposSupportTree` 都會用 `point_*()` helper 逐欄位解析——這四個欄位因為仍是 -1，會退回讀「當下即時的 preset」而非「生成當下的 preset」，導致 auto 點的支撐頭 Lower 直徑、連接長度、penetration、contact sphere 尺寸在切片輸出中持續飄動，直到下一次真正重新生成整批點為止。

這與姊妹 change `fix-sla-support-preview-geometry-source-semantics`（Change A，範圍鎖定 `GLGizmoSlaSupports.cpp`，只修 preview/picking 讀取邏輯）發現的問題同源，但根因在切片端的生成邏輯（不同檔案），因此拆成獨立 change。Change A 的 design.md D4/D5 已記錄這個落差與拆分決策。Change A 完成後，preview 會忠實顯示這個不對稱行為（Upper Diameter 凍結、其餘四個追蹤即時 preset），使這個問題首次在 UI 上可被觀察到，但不會修正它——修正必須在本 change 完成。

## What Changes

- Auto 生成流程（`SupportPointGenerator.cpp`）在建立每一顆 auto 點時，一次把全部五個 Top 欄位（不只 `head_front_radius`）寫入生成當下的 live preset 值，使 auto 點在生成後即完整凍結。
- `SLAPrintSteps.cpp` 的 `support_points()` 呼叫點需要擴充：目前只把 `cfg.support_head_front_diameter` 讀進 `SupportPointGeneratorConfig.head_diameter` 傳給生成器，需要一併讀取 `support_head_back_diameter`／`support_segment_length`／`support_head_penetration`／`support_contact_type`／`support_contact_diameter`，並傳遞到生成器用以寫入新產生的每一顆點。
- 除非使用者重新按下 Apply/Auto-generate 整批重造，否則調整 Process tab 的任何 Top 欄位都不應改變已生成 auto 點的實際切片幾何。
- 效能：生成迴圈內每顆點多寫四個 float 為常數項開銷，preset 值於迴圈外讀一次、迴圈內只做賦值；HeadGeomKey 幾何快取的 distinct key 數量不因此增加（同批生成點共用同一組 preset 快照）。

## Capabilities

### New Capabilities

- `sla-support-auto-point-top-field-freeze`：Auto 生成支撐點時，全部 Top 欄位（Upper/Lower Diameter、Segment Length、Penetration、Contact Sphere）於生成當下寫入具體值並凍結，使 auto 點的行為與手動點（`fix-sla-support-preview-geometry-source-semantics` 修復後）一致：建立/生成後即定型，不受後續 Process tab 即時編輯牽動，僅在下一次整批重新生成時才會改變。

### Modified Capabilities

<!-- 無。本 change 不改變 sla-support-param-wiring（該 capability 規範「preset 是否正確傳入演算法並觸發重切」，本 change 規範「已生成的 auto 點是否應該持續讀取即時 preset」，兩者角度不同，不衝突） -->

## Impact

- **Primary**：
  - `src/libslic3r/SLA/SupportPointGenerator.cpp` — 生成 `island`/`slope` 點時（`:238`／`:266`／`:294` 附近）寫入全部五個 Top 欄位的具體值，而非只有 `head_front_radius`
  - `src/libslic3r/SLAPrintSteps.cpp` — `support_points()`（約 `:862`）：`SupportPointGeneratorConfig` 需擴充攜帶其餘四個欄位的 preset 值，從 `po.config()` 讀取後傳給生成器
  - `src/libslic3r/SLA/SupportTreeBuildsteps.cpp` — 兩處**實作驗收期間新發現、併入範圍**的既有 bug：
    - `filter()`（約 `:812-818`，見 design.md D5）：`back_r`（Lower Diameter）解析原本繞過 `point_head_back_radius_mm()`，只在 `sp.type == manual_add` 時才讀該欄位、且殘留 `pillar_radius` fallback，導致本 change 第 1-3 節凍結的 auto 點 Lower Diameter 在切片端完全沒被消費到；改為直接呼叫共用 helper
    - `routing_to_ground()`／`connect_to_ground()`（見 design.md D6）：細長柱體的「加粗回即時 preset」結構安全機制原本只排除手動點，D5 修復後 auto 點的 `back_r` 首次能合法小於即時值，意外喚醒這段沉睡多年的邏輯；改為對所有點一律關閉加粗（`allow_widening = false`，最終整個機制與參數一併移除，見 D6 補述）
  - `src/libslic3r/SLAPrint.hpp` — 新增 public 方法 `invalidate_support_points_for_object()`（見 design.md D8，**實作驗收期間新發現、併入範圍**）：`slaposSupportPoints` 的失效清單刻意不含 Top 欄位（避免被動重切悄悄整批重造），但這代表按下 Apply 若只改過 Top 欄位，`reslice_until_step()` 會發現該 step 早已「完成」而直接跳過，畫面沒有變化；新增明確的強制重跑管道，只在按下 Apply 這個明確動作時使用
  - `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp` — `auto_generate()` 呼叫上述新方法（同見 D8）；另外**移除**了 Auto 模式 Apply 按鈕的 dirty-tracking 機制（`auto_settings_need_apply()`／`mark_auto_settings_applied()`／`m_applied_auto_*`，見 design.md D7 起草、D9 決定整個拿掉）——曾嘗試擴充這個機制去追蹤 Top 欄位（D7），但複測發現追蹤本身有 wx 欄位 commit 時序造成的偵測不準問題，決定不追這個時序 bug，直接讓 Apply 按鈕恆為可按（Apply 本身 idempotent，真正的安全機制是既有的確認對話框，不是按鈕狀態）
  - `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp` — `auto_generate()` 開頭新增 `flush_process_top_fields_to_config()` 呼叫（見 design.md D10，**實作驗收期間新發現、併入範圍**）：依序編輯多個 Top 欄位、每改完一個就按 Apply，只有最後一個以外的欄位沒被吃到——根因是 `auto_generate()` 完全依賴每個欄位各自的 wx commit 事件鏈準時觸發，沒有像「放置新手動點」那條路徑一樣主動 flush；比照既有寫法補上，讓「按下 Apply」本身成為權威同步點
- **不修改**：
  - `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp` 的 preview/picking 顯示邏輯（屬於 `fix-sla-support-preview-geometry-source-semantics` 範圍，兩者互不阻擋、檔案不重疊；D7 動的是 Auto 模式 Apply 按鈕狀態，同檔案但不同函式，不影響此邊界）
  - `SupportPoint.hpp` 的 `point_*()` helper 仲裁規則本身（D5 的修法是讓 `SupportTreeBuildsteps.cpp` 開始正確呼叫既有 helper，不是新增或修改 helper）
  - 手動點的建立/凍結邏輯（`freeze_process_top_into_point()`，同屬 `fix-sla-support-preview-geometry-source-semantics` 範圍）
- 影響切片輸出：auto 點生成後的支撐頭尺寸行為會改變（凍結範圍擴大），屬預期的行為修正，不影響檔案格式或 profile
- 無 public API 變更
