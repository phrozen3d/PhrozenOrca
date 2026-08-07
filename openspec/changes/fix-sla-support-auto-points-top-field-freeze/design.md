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

> **修正（實作驗收期間發現）**：上一段引用的「`SupportTreeBuildsteps.cpp:693` 逐欄位呼叫 `point_*()` helper」只對 `contact_r`／`mesh_pen`（進而 `head_front_radius`／`contact_sphere_radius`／`head_penetration_mm`）成立，**不適用於 `back_r`（Lower Diameter）**。實測 4.2 verification 期間發現 `SupportTreeBuildsteps::filter()`（同檔案 `:812-818`）對 `back_r` 有一套完全獨立、沒有呼叫 `point_head_back_radius_mm()` 的手刻邏輯，且僅在 `sp.type == manual_add` 時才檢查 `sp.head_back_radius_mm`，對 auto 點一律使用 `m_cfg.head_back_radius_mm`（當下即時 preset），並殘留一條 `pillar_radius` fallback——與 Change A 在 GUI 端修掉的 D2a（type 判斷）／D2b（`pillar_radius` fallback）是同一種病灶，但長在切片引擎本體。這代表本 change 完成 1-3 節（生成器凍結全部欄位）後，**Lower Diameter 這個欄位在切片輸出仍然不會凍結**，因為消費端根本沒去讀生成器寫入的值。詳見 D5。
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

### D5. 實測發現的既有 bug：`SupportTreeBuildsteps::filter()` 的 `back_r` 解析未使用共用 helper（併入本 change 範圍）

**案例**：完成 1-3 節後手動驗收 task 4.2，流程為：Lower Diameter=1 生成一批 auto 點 → preview 正確顯示凍結 → 改 Lower Diameter=2 → 進 Manual 模式新增手動點（正確凍結為 2，與舊 auto 點視覺上明顯不同，證明 preview／`m_editing_cache` 的凍結值正確）→ Apply → 回到 Structure 顯示模式 → **所有既有 auto 點的支撐頭下直徑全部變成 2**，與生成當下的 1 不符。

**根因鏈**：

1. `SupportTreeBuildsteps.cpp:812-818`（`filter()` 內，`ccr::for_each` 迴圈組裝每顆點的 `back_r`）：

   ```cpp
   double back_r = m_cfg.head_back_radius_mm;                 // 預設：當下即時 preset
   if (sp.type == sla::SupportPointType::manual_add) {         // 只有手動點才檢查！
       if (sp.head_back_radius_mm >= 0.f)
           back_r = double(sp.head_back_radius_mm);
       else if (sp.pillar_radius > 0.f)
           back_r = double(sp.pillar_radius);                  // pillar_radius fallback，同 D2b 病灶
   }
   ```

   對 `type == island/slope`（auto 點）而言，`sp.type == manual_add` 恆為 false，**這段程式碼永遠不會讀 `sp.head_back_radius_mm`**——不管本 change 第 1-3 節把這個欄位凍結得多正確，這個消費端根本沒去讀它，`back_r` 永遠是當下即時 preset。

2. 同一個 `filter()` 函式裡，`head_width_mm`（`:744`，`point_head_width_mm(sp, m_cfg.head_width_mm)`）與 `contact_r`／`mesh_pen`（`:695-697`，`point_contact_sphere_radius_mm()`／`point_head_penetration_mesh_mm()`）都正確呼叫了 `SupportPoint.hpp` 的共用 `point_*()` helper，不看 `type`。**只有 `back_r` 是例外**，這也是為什麼 Change A／本 change 原本引用的「`SupportTreeBuildsteps.cpp:693` 無條件逐欄位仲裁」這個對齊基準，對其餘欄位成立、唯獨對 Lower Diameter 不成立——兩個 change 撰寫 design.md 時都只看了 `:693` 附近的片段，沒有追到 `filter()` 內 `back_r` 另外走的這條路。

**修法**：把 `:812-818` 的手刻邏輯改成直接呼叫共用 helper，與 `head_width_mm` 的寫法一致：

```cpp
double back_r = point_head_back_radius_mm(sp, m_cfg.head_back_radius_mm);
```

九行改一行，`type` 判斷與 `pillar_radius` fallback 一併移除，auto 點與手動點套用同一條規則。

**為何併入本 change 範圍，不另開 change**：這個 bug 直接卡住本 change 自己的驗收項目（4.2/4.3）——沒有這個修復，本 change 第 1-3 節做的「生成器凍結全部欄位」對 Lower Diameter 完全沒有實際效果（消費端不讀）。病灶與 D2a/D2b 同源、範圍是同一個檔案裡的個位數行數修改，風險與規模都不足以獨立成一個 change。

**驗收影響**：4.2 的驗收判準不變（切片輸出的 Lower Diameter 應維持生成當下的值），但需要在此修復落地後才會通過；4.2 之前的失敗是這個既有 bug 造成的，不是第 1-3 節實作有誤。

### D6. 第二個實測發現的既有 bug：柱體「加粗」機制被 D5 意外喚醒（併入本 change 範圍）

D5 修復後複測 4.2，仍觀察到**部分**（非全部）auto 點的支撐柱下端變粗到即時 preset 值，且必須先進 Manual 模式放過手動點、按 Apply 之後才會發生，單純改面板+切 Structure 不會。追查如下。

**根因**：`SupportTreeBuildsteps.cpp`（`routing_to_ground()`／`connect_to_ground()`）裡有一段柱體加粗機制，對「細又高」的柱子會從某個高度開始把半徑加粗到 `m_cfg.head_back_radius_mm`（當下即時 preset），觸發條件三個都要成立：

```cpp
if (allow_widening && radius < m_cfg.head_back_radius_mm && jp.z() - gndlvl > 20 * radius)
    // ... 加粗到 m_cfg.head_back_radius_mm
```

**這個機制本身是 2022-07-15（`1555904be`，「Add the full source of BambuStudio」）就存在的原始碼，屬於上游繼承下來的既有結構安全設計**（意圖：細長柱子容易脆弱，加粗補強），不是本 change 新增的。它原本只對手動點生效——`fix-sla-support-auto-points-top-field-freeze` 之前，auto 點的 `radius` 恆等於 `m_cfg.head_back_radius_mm`（因為 `head_back_radius_mm` 恆為 -1、`point_head_back_radius_mm()` 永遠退回 preset），`radius < m_cfg.head_back_radius_mm` 這個條件對 auto 點從未成立過，這段加粗邏輯對 auto 點形同虛設。

`2026-04-29`（`075881d38`，`sla-manual-support-per-point-sizing`）曾經在手動點這邊踩過同一個問題一次——手動 Light/Medium 權重點的 `pillar_radius` 明確設小時，這段機制會把它加粗回標準尺寸、蓋掉使用者選的權重——當時的修法是加一個 `sp.type == manual_add && sp.pillar_radius < m_cfg.head_back_radius_mm` 的判斷關掉加粗，只覆蓋手動點。

D5 讓 auto 點的 `radius` 也開始能合法小於當下即時值（生成當下凍結、之後面板又調大），**第一次讓 `radius < m_cfg.head_back_radius_mm` 對 auto 點成立**，這段沉睡多年的加粗邏輯因此被意外喚醒，且只對 auto 點生效（因為既有判斷式只排除手動點）。

「部分而非全部」的原因：不是每顆點都會走到這段加粗程式碼——支撐點會先分群，每群只有一個「群中心」點呼叫 `create_ground_pillar()`（會經過加粗判斷），其餘點呼叫 `connect_to_nearpillar()` 直接橋接到群中心已蓋好的柱子（`:402`，`r = std::min(head.r_back_mm, nearpillar().r_start)`，完全不經過加粗邏輯）。分群結果受**整批點的組合**影響，多加幾顆手動點會讓分群拓樸重新洗牌，可能讓原本只是「鄰居」的 auto 點這次被分配成「群中心」，才第一次踩進加粗判斷式——這解釋了為何純改面板不觸發（分群沒變），但放過手動點、Apply 之後才會（分群重跑）。**這個機制本質上不可預期**：同一顆 auto 點會不會被加粗，取決於模型上其他點如何排列組合，使用者無法從自己那顆點的設定推斷。

**修法（選項 A，已採用）**：把「凍結值存在就不加粗」的邏輯從手動點專屬，改成對所有點都成立——因為 D5 之後，**每一顆點（auto 或手動）的 `back_r` 都是刻意 resolve 出來的值，不該被靜默墊大**。三處呼叫 `create_ground_pillar()` 的 `allow_widening` 參數統一改成 `false`：

- `routing_to_ground()` 群中心路徑（原本的 `widen` 判斷式整段移除）
- `routing_to_ground()` sidehead fallback 路徑（原本硬編碼 `sw = true`）
- `connect_to_ground()`（原本的 `widen_ctg = !is_manual_small`）

`connect_to_ground()` 裡另一處用同一個旗標（`is_manual_small`）控制的 bridge 搜尋距離縮放（`:1012-1013`），也一併改成型別無關的 `radius < m_cfg.head_back_radius_mm` 判斷——同一顆點的「半徑比即時值小」這個事實，不該只對手動點成立、對 auto 點卻視而不見。

**為何直接拿掉整個加粗機制、不做更保守的折衷**：拿掉之後，`radius < m_cfg.head_back_radius_mm` 這個觸發條件本身仍然只在「這顆點真的有自己 resolve 出來的較小值」時成立（未設定的欄位透過 `point_*()` helper 退回 preset，恆等於即時值，不會小於它）——`allow_widening = false` 只是讓這個既有條件成立時「不要覆蓋」，不影響其他任何判斷。細長柱子的結構考量（原始設計意圖）目前沒有其他替代的安全網，若之後有實際列印失敗案例回報，應該另開 change 針對「刻意設定的細柱」重新設計結構補強機制，而不是靠這個會被使用者不可控的模型/點位組合觸發的隱性加粗。

**驗收影響**：新增到 tasks 第 3b 節；4.2/4.4 的驗收現在應該對「群中心」與「橋接鄰居」兩種角色的 auto 點都成立，不再只是部分正確。

**後續清理（同一輪追問中發現並一併處理）**：把三處呼叫都改成固定傳 `false` 之後，`allow_widening` 這個參數在 `create_ground_pillar()` 內已經沒有任何呼叫者會傳 `true`，形同死碼——但拆掉前發現它其實**身兼二職**：除了控制加粗，還在另一段完全無關的邏輯（`:520-536`，「headless／synthetic 橋接接點要不要強制加底座」，處理 `head_id == ID_UNSET` 找不到原始支撐點可查的情況）裡被當作「這是不是手動點」的間接判斷依據（`is_manual = !allow_widening`）——這個判斷式原本的前提是「只有手動點的呼叫才會傳 `false`」，若不處理，單純把三處呼叫改成固定傳 `false` 會讓這段邏輯把所有 auto 點衍生出的柱子都誤判成手動點。

修法：先把這個 Case B 判斷改成保守預設 `false`（不再依賴 `allow_widening`，找不到原始支撐點就不強制加底座——跟改動前相比，真正的手動點鏈路仍會在鏈上其他有效 `head_id` 的節點拿到底座，這裡只是少了一個邊緣情況的重複保險），確認這條路徑真正獨立、不再依賴這個旗標之後，才把 `allow_widening` 參數、內部的加粗 `if` 分支、以及因此變成無呼叫者的 `search_widening_path()` 一併從 `.cpp`／`.hpp` 移除，不留下一個「所有呼叫端都固定傳同一個值」的死參數。

### D7. 第三個實測發現的問題：Auto 模式的「Apply」按鈕不會因為 Top 欄位變動而重新啟用（併入本 change 範圍）

> **後續更新（見 D9）**：本節記錄的擴充比對機制，複測時又發現新的時序 bug（wx 欄位 commit 時機不同步，導致「改第一個欄位不會被偵測到，要改第二個才會一併偵測到」）。與其繼續追這個時序問題，D9 決定把整個 dirty-tracking 機制（本節 + D7 之前就存在的密度/權重/角度比對）**整個移除**，Apply 按鈕改為恆為可按。本節保留作為問題發現過程的記錄，`auto_settings_need_apply()`／`mark_auto_settings_applied()`／`m_applied_auto_*` 這些名稱在最終程式碼中已不存在。

複測 task 4.5（重新按 Auto-generate 確認舊點被新值取代）時，發現 Auto-generate 一次之後，單純在面板改 Top 欄位（Upper／Lower Diameter 等），Apply 按鈕**不會**重新變成可按——必須離開 gizmo 模式、做一次旋轉之類的操作再回來，才會看到 Apply 重新啟用。這直接卡住 4.5 的驗收流程，沒有 workaround 就無法在 UI 上真正觸發「用新 Top 值整批重造」。

**根因**：`auto_settings_need_apply()`（`GLGizmoSlaSupports.cpp:1548`）判斷 Apply 該不該啟用時，只比對三項：`m_new_point_weight`（權重）、`support_points_density_relative`（密度）、`support_critical_angle`（角度）——三者都是 `mark_auto_settings_applied()` 在上次 Apply 當下存的基準值。**完全沒有比對任何 Top 欄位**，所以 Top 欄位變動對這個「該不該重新啟用 Apply」的判斷完全不可見。

這個判斷式本身在本 change 之前就存在、範圍也只涵蓋密度類參數（合理，因為當時 Top 欄位根本不影響「要不要重新生成」這件事——欄位沒被凍結，改了也是切片時才生效）。但本 change 讓 Top 欄位開始決定「生成當下凍結進每一顆點的值」，變成跟密度／權重同等重要的「會不會需要重新生成」訊號，這個判斷式沒有跟著擴充,是本 change 完成後才會浮現的落差。

**修法**：`auto_settings_need_apply()`／`mark_auto_settings_applied()` 一併擴充比對六個 Top 欄位（`support_head_front_diameter`／`support_head_back_diameter`／`support_segment_length`／`support_head_penetration`／`support_contact_type`／`support_contact_diameter`——跟 `SupportPointGeneratorConfig` 讀的是同一組欄位，contact type/diameter 合起來對應 `contact_sphere_radius`），新增對應的 `m_applied_auto_*` 基準值成員變數，讀取方式沿用既有的 `get_config_options()`（讀已提交的 preset，跟 `auto_generate()` 實際觸發生成時讀到的值同源），不用另一套 live-widget 讀值路徑。

### D8. 第四個實測發現的問題：Apply 按鈕啟用後按下去沒有實際生效（併入本 change 範圍）

D7 修好之後複測，Apply 按鈕確實會因為改 Top 欄位而重新啟用，但按下去畫面沒有變化、且「瞬間完成」——懷疑根本沒有真的重新生成。

**根因**：`slaposSupportPoints`（實際執行生成演算法的切片 step）只有在下列情況才會被標記為「需要重新計算」：(a) 密度類參數（`support_points_density_relative`／`support_points_minimal_distance`／`support_critical_angle`／`branchingsupport_critical_angle`）變動、(b) 上游 `slaposObjectSlice`（幾何/mesh）變動、(c) `sla_points_status` 在 `UserModified` 與其他狀態之間切換且點資料有異動（`SLAPrint.cpp:592-600`）。**Top 欄位變動完全不在這個清單裡**——這是刻意的：如果 Top 欄位變動會讓 `slaposSupportPoints` 自動失效，那麼任何被動的重切觸發（例如切到 Structure 檢視、開啟 Preview）都會在使用者毫無操作的情況下悄悄用當下即時值整批重造所有 auto 點，直接推翻本 change／D5／D6 好不容易建立起來的「凍結直到明確重新生成」不變式（且會讓已經通過的 task 4.1-4.4 重新失敗）。

`GLGizmoSlaSupports::auto_generate()`（按下 Apply 時執行）呼叫 `reslice_until_step(slaposSupportPoints)`，但這個呼叫只會在該 step **已經被標記為失效**時才真的重新計算——若使用者這次只改了 Top 欄位（沒有動密度/權重/角度），`slaposSupportPoints` 仍然是「已完成」狀態，`reslice_until_step()` 因此什麼也不用做，瞬間回傳「已完成」，畫面自然沒有變化。

**這不是本 change 引入的新洞，而是既有洞第一次被看見**：在本 change之前，Top 欄位從未被 `slaposSupportPoints` 消費（只有生成當下的 `head_diameter` 例外），所以「這個 step 有沒有因為 Top 欄位變動而重新跑」根本無關緊要——就算沒重跑，下游的 `slaposSupportTree` 一樣會用即時 preset 重新解析這些欄位，畫面看起來仍然正確。本 change 把 Top 欄位「烘焙」進生成當下的點資料後，這個原本無關緊要的差異第一次變得可觀察。

**修法**：`GLGizmoSlaSupports::auto_generate()`（明確的使用者動作）需要一個不透過一般 config-diff 機制的強制重跑管道。`SLAPrintObject::invalidate_step()` 是 `protected`（`friend class SLAPrint`），GUI 端無法直接呼叫；改為在 `SLAPrint`（`SLAPrint.hpp`）新增一個小型 public 方法 `invalidate_support_points_for_object(ObjectID)`，內部呼叫既有的 protected `invalidate_step(slaposSupportPoints)`（藉由既有的 friend 關係），`auto_generate()` 在呼叫 `reslice_until_step()` 之前先呼叫這個新方法，確保**只有明確按下 Apply/Auto-generate 才會強制重跑**，被動的重切觸發依然不受影響——與 D5/D6 建立的凍結不變式並不衝突，兩者其實是同一個設計原則的兩面：資料不會被動漂移，但有一個明確、使用者主動觸發的管道可以重新生成。

**風險評估**：`invalidate_step()` 內部透過 `state_mutex()` 自行處理鎖定（沿用 `PrintBaseWithState` 既有機制），直接呼叫是執行緒安全的；新方法標成 `const`（不修改 `SLAPrint` 自身的 `m_objects` 容器，只透過其中已持有的 `SLAPrintObject*` 呼叫其 mutating 方法），對齊 GUI 端唯一持有的 `const SLAPrint*`（`GLCanvas3D::sla_print()`）。

### D9. 第五個實測發現的問題：Apply 按鈕的 dirty-tracking 對某些欄位編輯順序失準，決定整個機制移除（併入本 change 範圍）

D7 修好、D8 也修好之後複測，使用者發現：依序編輯多個 Top 欄位時，只改第一個欄位不會讓 Apply 按鈕重新啟用；要再改第二個欄位，兩個欄位的異動才會**一起**被偵測到、Apply 才變成可按。

**追查方向（有程式碼佐證，但未接除錯器實際確認，信心程度中等）**：SLA Top 欄位在 `Tab::on_value_change()` 走一條刻意的提早 return 路徑（`flush_process_top_fields_to_config()` + `schedule_support_top_ui_sync()`），跳過正常選項編輯會走的「重新整理欄位顯示、刷新欄位自身的 `m_value` 快取」流程（`Field.cpp` 的 `TextCtrl::propagate_value()` 用 `value_was_changed()` 比對這個快取，決定要不要觸發 `on_change_field()`）。這條快取正常只在 `set_value()` 被呼叫時更新，而 SLA Top 欄位的特殊路徑刻意不做這件事（為了避免每個按鍵都觸發昂貴的驗證/UI 更新）。這可能造成欄位間 commit 事件的時序跟預期不同步，但確切機制未經執行期驗證。

**決策：不追這個時序 bug，直接把整個 dirty-tracking 機制移除**，理由：

1. **Apply 本身是 idempotent 的**——生成器用固定 seed（`solver.seed(0)`，`SupportPointGenerator.cpp` 既有程式碼），同樣的輸入永遠得到同樣的結果，重複按不會造成資料損壞或非預期的隨機性。
2. **真正的安全機制是確認對話框，不是按鈕的 enable/disable**——`auto_generate()` 裡「將會清除所有手動編輯的點」的 Yes/No 對話框才是防止使用者誤觸而弄丟資料的機制；按鈕能不能按只是「有沒有事要做」的 UX 提示，不是安全網。
3. **這個機制這一輪內連續出現三次問題**（本節之前：完全沒追蹤 Top 欄位／D7；追蹤了但用 wx 事件時序，欄位間不同步／本節）——本質上是個脆弱的機制，要跟 wx widget 的 commit 時機精確同步才能正確運作，值得懷疑的維護成本已經超過它帶來的 UX 價值。

**修法**：移除 `auto_settings_need_apply()`、`mark_auto_settings_applied()`、`m_applied_auto_*` 系列成員變數、`m_auto_baseline_initialized`，以及所有呼叫端（`render_auto_support_panel()` 的 `m_imgui->disabled_begin(...)` 判斷、`on_set_state()`／`data_changed()`／`apply_remove_all()` 裡重置基準值的呼叫、`auto_generate()` 裡標記基準值的呼叫）。Auto 模式的 Apply 按鈕改為只要有效的 `mo` 存在就一律可按，不再判斷「有沒有東西要 apply」。D7／D8 記錄的問題發現過程與（D8 的）強制重跑機制本身保留不變——D8 解決的是「按下去有沒有真的重新生成」，跟本節「按鈕該不該讓你按」是兩個獨立的關注點，D9 只移除後者。

### D10. 第六個實測發現的問題：依序編輯多個 Top 欄位，只有最後一個以外的欄位要等下一個欄位被編輯才會生效（併入本 change 範圍）

D9 把按鈕 enable/disable 拿掉之後複測，發現一個比按鈕狀態更根本的問題：依序編輯多個 Top 欄位（例如先改接觸直徑、再改段長度），**改第一個欄位後直接按 Apply，畫面完全沒反應**；要接著再改第二個欄位，Apply 才會同時吃到兩個欄位的新值。「Process tab 的『已修改』橙點提示」也呈現同樣的落後一拍模式。

**追查過程**：`Field.cpp` 的 `TextCtrl::propagate_value()`／`value_was_changed()` 追過一輪，確認 `m_value`（欄位自身用來判斷「有沒有變」的快取）在每次呼叫 `value_was_changed()` 時就會自我更新（`get_value_by_opt_type(ret_str)`），理論上不需要依賴外部的 `set_value()` 呼叫才能刷新，靜態程式碼追蹤沒有找到能百分之百解釋「改第一個沒反應」的明確斷點；也確認過 `support_contact_diameter` 依賴 `support_contact_type == Sphere` 才會被 `toggle_field()` 致能（`ConfigManipulation.cpp:1037-1039`），但使用者的重現步驟裡 Contact Type 並未在這三步之內變動，這條路徑排除。

**關鍵發現**：`flush_process_top_fields_to_config()`（把 Top 欄位目前的 widget 值一次性寫進 preset config 的函式）**只有「點擊模型放置新手動點」這條路徑會在使用之前主動呼叫一次**（`:922`，`freeze_process_top_into_point()` 前）；`auto_generate()`（Apply 按鈕）完全沒有做同樣的事，完全依賴每個欄位自己的 wx 事件鏈（kill-focus／Enter → `Tab::on_value_change()` → `flush_process_top_fields_to_config()`）恰好都正確觸發過一輪。一旦某個欄位的 commit 事件鏈因為任何原因（時序、UI 刷新順序等，確切機制未完全查明）沒有準時觸發，`auto_generate()` 讀到的就是這個欄位在 preset 裡的**舊值**，直到下一次別的欄位的 commit 事件連帶把它一起 flush 進去為止。

**修法**：讓 `auto_generate()` 不要依賴每個欄位各自的 commit 事件鏈完全可靠這個前提——比照「放置新手動點」的既有寫法，在函式最開頭主動呼叫一次 `flush_process_top_fields_to_config()`，把當下 widget 顯示的值（不論其 commit 事件有沒有正確觸發過）直接寫進 preset。這讓「按下 Apply」本身成為權威的同步時間點，不再依賴 wx 欄位事件時序是否精確——跟 D9 是同一個設計方向：不追事件時序的 bug，改用一個不依賴它的機制取代。

**複測確認（收尾）**：修好之後複測，Apply 的功能性結果（實際套用哪些值）已完全正確，包含「即使 Process tab 沒顯示已修改提示，Apply 仍正確取用面板當下的值」這個情境。複測期間額外發現一個**純視覺**的殘留現象：Apply 之後再次依序編輯 Top 欄位，「已修改」提示（變色＋放棄修改按鈕）有時要連續編輯兩三個欄位才會一起顯示；但實測確認底層資料正確（點擊看不到的「放棄修改」按鈕位置，數值確實跳回 default），純粹是提示未即時重繪。追出這個提示背後的機制（`schedule_support_top_ui_sync()`／`update_changed_ui()`，`Tab.cpp`）源頭是 2026-06-16 的 `06ddfd10f fix(sla-ui): refresh support Top dirty/reset state immediately`，早於本 change 與整個支撐點系列 change，是既有機制自身的殘留問題，與本 change「auto 點凍結」的範圍無關——**不在本 change 修**，列入 tasks.md 第 8 節 Follow-up。

## Risks / Trade-offs

- **[改變既有 auto 點的實際切片幾何]** → 對「生成後又調過 Top 面板但沒重新生成」的既有專案，重新開啟後 auto 點的支撐頭尺寸解讀會與過去不同（過去讀到的是「重切當下」的即時值，現在讀到的是「生成當下」凍結的值）。這是本 change 明確要修正的行為，但屬於使用者可觀察的輸出變化。緩解：純屬修正既有不一致，凍結後的行為才是使用者一路以來期望、也是 UI 顯示（Change A 完成後）承諾的行為；發布說明應提及此行為修正。
- **[`SupportPointGeneratorConfig` 新增欄位遺漏初始化]** → 若有其他呼叫端（非 `support_points()`）建立 `SupportPointGeneratorConfig` 卻未設定新欄位，會用結構體的預設值（0.f），導致該路徑生成的點 Lower Diameter 等於 0 而非合理的 preset 值。緩解：任務中列出全域搜尋 `SupportPointGeneratorConfig` 的建構位置，逐一確認。
- **[`contact_sphere_radius` 的 0 值語意混淆]** → `SupportPoint::contact_sphere_radius` 的語意是「`>=0` 表示已設定、`>0` 才代表真正使用 sphere；`<0`（即 `SUPPORT_POINT_USE_PRESET`）才是未設定」。若生成器在不使用 sphere 時寫入 `0.f`（而非維持 `-1.f`），會被誤判為「已設定為不使用」而非「未設定」——這其實是我們要的效果（`point_uses_contact_sphere()` 對兩者的判斷結果一致，但語意上「已明確設定為 0」比「未設定」更準確地反映生成當下的 Contact Type 狀態），實作時需確認 `point_contact_sphere_radius_mm()` 與 `point_uses_contact_sphere()` 對 `0.f` 與 `-1.f` 的處理差異不會在其他呼叫路徑產生非預期行為。

## Migration Plan

無資料遷移、無檔案格式或 profile 變更。既有專案重新開啟後，若曾有「生成後又調整 Top 面板」的操作歷史，auto 點的切片幾何會回到「生成當下凍結值」，這是行為修正而非資料損壞——舊 3mf 檔案中儲存的 `SupportPoint` 資料本身不變（新欄位語意只影響本次程式版本之後**新生成**的點）。

回退策略：修改集中在 `SupportPointGenerator.cpp`（三處建構點的賦值）、`SLAPrintSteps.cpp::support_points()`（`config` 組裝）、`SupportTreeBuildsteps.cpp::filter()`（`back_r` 解析改用共用 helper，見 D5），三處皆可獨立 revert，不影響其他切片 step。

## Open Questions

- `SupportPointGeneratorConfig` 是否有除 `support_points()` 以外的其他建構呼叫點（例如測試程式碼）？需要在 tasks 第一步全域搜尋確認，避免遺漏初始化。
