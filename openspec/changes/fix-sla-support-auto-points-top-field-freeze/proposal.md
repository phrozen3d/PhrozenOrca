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
- **不修改**：
  - `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp` 的 preview/picking 顯示邏輯（屬於 `fix-sla-support-preview-geometry-source-semantics` 範圍，兩者互不阻擋、檔案不重疊）
  - `SupportPoint.hpp` 的 `point_*()` helper 仲裁規則本身
  - 手動點的建立/凍結邏輯（`freeze_process_top_into_point()`，同屬 `fix-sla-support-preview-geometry-source-semantics` 範圍）
- 影響切片輸出：auto 點生成後的支撐頭尺寸行為會改變（凍結範圍擴大），屬預期的行為修正，不影響檔案格式或 profile
- 無 public API 變更
