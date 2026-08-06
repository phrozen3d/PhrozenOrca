## Context

`SupportPointGenerator.cpp` 建立 `island`/`slope` 類型的 auto 支撐點時（`:238`／`:266`／`:294`），只寫入一個 Top 欄位：

```cpp
SupportPoint{
    Vec3f{...}, /* head_front_radius */ config.head_diameter / 2,
    SupportPointType::slope  // 或 island
}
```

`SupportPoint` 的其餘四個 Top 欄位（`head_back_radius_mm`／`head_width_mm`／`head_penetration_mm`／`contact_sphere_radius`）在建構時保持預設值 `SUPPORT_POINT_USE_PRESET`（`-1.f`，`SupportPoint.hpp:38`），語意是「未設定，切片時退回讀即時 preset」。

切片端 `SupportTreeBuildsteps.cpp:693` 逐欄位呼叫 `point_*()` helper（`SupportPoint.hpp:126-164`），對每個欄位獨立判斷「有具體值就用具體值，否則用傳入的 `preset_mm` 參數」。這個 `preset_mm` 參數在 `SupportTreeBuildsteps` 建構時，是從**當下**的 `SLAPrintObjectConfig`（即 `slaposSupportTree` 這個 step 執行當下的 config）取得的。

而 `SLAPrintSteps.cpp:862` 的 `support_points()`（`slaposSupportPoints` step）只把 `cfg.support_head_front_diameter` 讀進 `SupportPointGeneratorConfig::head_diameter` 傳給生成器，其餘 Top preset 值完全沒有被讀取或傳遞。

`SLAPrint.cpp:1120-1149` 的 step 失效規則顯示：`support_head_back_diameter`／`support_segment_length`／`support_head_penetration`／`support_contact_type`／`support_contact_diameter` 的變動只使 `slaposSupportTree` 失效，不使 `slaposSupportPoints` 失效——代表這些欄位變動不會觸發重新生成點的位置與 `head_front_radius`，但**會**觸發 `slaposSupportTree` 用當下最新的 config 重新解析每一顆既有點的其餘四個欄位。因為這四個欄位在點上是 -1，`point_*()` helper 一律 fallback 到當下即時 preset——這就是使用者觀察到的「auto 點的 Lower Diameter／Segment Length／Penetration／Contact Sphere 會持續追蹤面板即時值，直到重新生成」的根因。

`head_front_radius` 沒有這個問題，純粹是因為它是唯一一個生成器本來就會寫入具體值的欄位——這不是刻意設計的一致性，是歷史上只有這個欄位被實作。

## Goals / Non-Goals

**Goals:**

- Auto 生成 `island`/`slope` 點時，把全部五個 Top 欄位（Upper Diameter／Lower Diameter／Segment Length／Penetration／Contact Sphere）寫入生成當下的 live preset 具體值。
- 生成後的 auto 點在切片端與 preview 端都應完整凍結：除非使用者重新觸發整批生成（Apply/Auto-generate），否則後續調整 Process tab 的 Top 欄位不影響已生成點的實際幾何。
- 生成當下讀取的 preset 值須是**觸發生成那一刻**的即時值（與現有 `head_front_radius` 的行為一致），不引入新的時間點語意。
- 效能：新增的欄位寫入不得造成生成流程可觀測的效能劣化。

**Non-Goals:**

- 不改變 preview／picking 的顯示邏輯（`GLGizmoSlaSupports.cpp`），屬於 `fix-sla-support-preview-geometry-source-semantics`（Change A）範圍。
- 不改變 `point_*()` helper 的仲裁規則本身——本 change 只是讓 auto 點的資料層級「有值可用」，不改變「有值就用值、沒值就用 preset」這條規則。
- 不改變手動點的建立/凍結邏輯（`freeze_process_top_into_point()`），同屬 Change A 範圍。
- 不改變 `slaposSupportPoints`／`slaposSupportTree` 的 step 失效規則（`SLAPrint.cpp:1120-1149`）——現有規則（Top 欄位變動只使 `slaposSupportTree` 失效）在本 change 後依然正確：既然點的欄位已經凍結，`slaposSupportTree` 重跑時對這些點的 `point_*()` 解析會直接讀到凍結值，不再落到 preset fallback，行為自然正確，不需要額外让 Top 欄位變動也去 invalidate `slaposSupportPoints`。
- 不改變 `support_points_density_relative`／`support_points_minimal_distance`／`support_critical_angle` 等密度類參數的失效規則或生成邏輯。

## Decisions

### D1. 在 `SupportPointGeneratorConfig` 新增四個欄位，於 `support_points()` 一次性讀入

在 `SupportPointGenerator.hpp` 的 `SupportPointGeneratorConfig` 新增：

```cpp
float head_back_radius_mm   = 0.f;   // Lower Diameter / 2，生成當下的 preset 值
float head_width_mm         = 0.f;   // Segment Length，生成當下的 preset 值
float head_penetration_mm   = 0.f;   // Penetration，生成當下的 preset 值
float contact_sphere_radius = 0.f;   // Contact Sphere 半徑；0 表示不使用 sphere（對齊 contact_sphere_radius 欄位語意：>0 才代表使用 sphere）
```

`SLAPrintSteps.cpp::support_points()` 在組裝 `config` 時（緊鄰現有 `config.head_diameter = float(cfg.support_head_front_diameter);` 那一行）一併讀入：

```cpp
config.head_back_radius_mm   = float(cfg.support_head_back_diameter) * 0.5f;
config.head_width_mm         = float(cfg.support_segment_length);
config.head_penetration_mm   = float(cfg.support_head_penetration);
config.contact_sphere_radius = (cfg.support_contact_type.value == spSphere)
                                    ? float(cfg.support_contact_diameter) * 0.5f : 0.f;
```

**為何在 `support_points()` 讀、不在生成器內部讀**：`SupportPointGenerator.cpp` 目前不持有 `SLAPrintObjectConfig`，只吃 `SupportPointGeneratorConfig` 這個精簡結構；沿用既有的「呼叫端負責從 print config 轉譯、生成器只吃轉譯後的純數值」分工，不需要讓生成器多一個對 print config 的依賴。

### D2. 生成點的建構式呼叫改為傳入完整欄位，而非事後賦值

三處建立 `SupportPoint` 的地方（`:238`／`:266`／`:294`）目前用「建構子只給 `head_front_radius`，其餘吃預設值」的寫法。改為建構後立即賦值（比照 `pillar_radius` 在手動點路徑的寫法），或擴充建構子多帶四個參數——兩者皆可，實作時依現有程式碼風格擇一，不影響外部行為。

**為何在建構當下就賦值，不留到迴圈外事後批次填入**：三個生成路徑（`near_points.add`、兩處 `island` 產生）目前是分散呼叫，事後統一 patch 需要額外一次迭代整個 `LayerSupportPoints`／最終 `SupportPoints` 容器，徒增一次 O(n) 掃描；建構當下賦值是零額外開銷的寫法。

### D3. 效能：常數項開銷，不影響演算法複雜度

新增的四個 float 賦值是純記憶體寫入，沒有配置、沒有分支、沒有查表。相對於生成器本身在做的 Voronoi medial axis／island 偵測／KD-tree 最近點搜尋，是可忽略的常數項。四個 preset 值只在 `support_points()` 讀取一次（不在生成迴圈內重複查詢 config），迴圈內只做賦值。

對 `HeadGeomKey` 幾何快取（`perf-sla-support-points-preview-render` 引入）的影響：同一次生成批次內所有 auto 點的五個 Top 欄位完全相同（都是同一組 preset 快照），distinct key 數量與現況（現況只有 `head_front_radius` 相同，其餘欄位靠 preview 端邏輯統一使用 live 值）相比不會增加——本 change 後，「這批點的幾何組合」從「一組固定值 + 四組隨即時 preset 變動的值」收斂成「一組完全固定的值」，key 數量只會持平或減少，不會變多。

### D4. 與 Change A 的邊界重申

本 change 是 D2 的「寫入端」修復，`fix-sla-support-preview-geometry-source-semantics` 是「讀取端」修復。兩者共用同一份判定基礎（`point_*()` helper 的 sentinel 規則），但改的是資料流的不同端點，檔案不重疊：

- 本 change：`SupportPointGenerator.cpp`、`SLAPrintSteps.cpp` —— 讓 auto 點的資料本身「有值可用」
- Change A：`GLGizmoSlaSupports.cpp` —— 讓 preview／picking 正確讀取「不論有沒有值都要讀對」

任一方單獨完成都有獨立價值、可獨立驗收、可獨立 revert，不互相阻擋實作順序。

## Risks / Trade-offs

- **[改變既有 auto 點的實際切片幾何]** → 對「生成後又調過 Top 面板但沒重新生成」的既有專案，重新開啟後 auto 點的支撐頭尺寸解讀會與過去不同（過去讀到的是「重切當下」的即時值，現在讀到的是「生成當下」凍結的值）。這是本 change 明確要修正的行為，但屬於使用者可觀察的輸出變化。緩解：純屬修正既有不一致，凍結後的行為才是使用者一路以來期望、也是 UI 顯示（Change A 完成後）承諾的行為；發布說明應提及此行為修正。
- **[`SupportPointGeneratorConfig` 新增欄位遺漏初始化]** → 若有其他呼叫端（非 `support_points()`）建立 `SupportPointGeneratorConfig` 卻未設定新欄位，會用結構體的預設值（0.f），導致該路徑生成的點 Lower Diameter 等於 0 而非合理的 preset 值。緩解：任務中列出全域搜尋 `SupportPointGeneratorConfig` 的建構位置，逐一確認。
- **[`contact_sphere_radius` 的 0 值語意混淆]** → `SupportPoint::contact_sphere_radius` 的語意是「`>=0` 表示已設定、`>0` 才代表真正使用 sphere；`<0`（即 `SUPPORT_POINT_USE_PRESET`）才是未設定」。若生成器在不使用 sphere 時寫入 `0.f`（而非維持 `-1.f`），會被誤判為「已設定為不使用」而非「未設定」——這其實是我們要的效果（`point_uses_contact_sphere()` 對兩者的判斷結果一致，但語意上「已明確設定為 0」比「未設定」更準確地反映生成當下的 Contact Type 狀態），實作時需確認 `point_contact_sphere_radius_mm()` 與 `point_uses_contact_sphere()` 對 `0.f` 與 `-1.f` 的處理差異不會在其他呼叫路徑產生非預期行為。

## Migration Plan

無資料遷移、無檔案格式或 profile 變更。既有專案重新開啟後，若曾有「生成後又調整 Top 面板」的操作歷史，auto 點的切片幾何會回到「生成當下凍結值」，這是行為修正而非資料損壞——舊 3mf 檔案中儲存的 `SupportPoint` 資料本身不變（新欄位語意只影響本次程式版本之後**新生成**的點）。

回退策略：修改集中在 `SupportPointGenerator.cpp`（三處建構點的賦值）與 `SLAPrintSteps.cpp::support_points()`（`config` 組裝），可獨立 revert，不影響其他切片 step。

## Open Questions

- `SupportPointGeneratorConfig` 是否有除 `support_points()` 以外的其他建構呼叫點（例如測試程式碼）？需要在 tasks 第一步全域搜尋確認，避免遺漏初始化。
