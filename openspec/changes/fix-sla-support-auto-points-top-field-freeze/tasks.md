## 0. 前置條件與實施順序

- [x] 0.1 已確認 Change A 真值表結論（design.md D4）仍成立（Change A 已於 2026-08-06 完成並歸檔）：`head_front_radius` 無 sentinel 恆讀自身值，其餘四欄位 `>= 0` 用自身值否則退回 `preset_mm`——本 change 正是要讓 auto 點的這四個欄位在生成當下就有自身值可用
- [x] 0.2 全域搜尋確認：`SupportPointGeneratorConfig` 只有一處建構（`SLAPrintSteps.cpp:889`，`support_points()` 內），無其他呼叫點或測試程式碼，design.md 提到的「遺漏初始化」風險不存在

## 1. `SupportPointGeneratorConfig` 擴充欄位

- [x] 1.1 已在 `SupportPointGenerator.hpp` 新增 `head_back_radius_mm`／`head_width_mm`／`head_penetration_mm`／`contact_sphere_radius`，預設值 `0.f`，附註單位與語意（該值只是防禦性預設，實際使用前一定會被 `support_points()` 覆寫）
- [x] 1.2 欄位命名逐字對應 `SupportPoint.hpp` 的同名欄位，可直接複製賦值

## 2. `SLAPrintSteps.cpp` 讀取 preset 並傳遞

- [x] 2.1 `support_points()`（`:889` 附近）組裝 `config` 時一併讀取 `support_head_back_diameter`／`support_segment_length`／`support_head_penetration`／`support_contact_type`／`support_contact_diameter`，轉譯規則對照 `SLAPrint.cpp:129-130`（`make_support_cfg` 用的同一套 `ContactType::spSphere` 判斷語法）
- [x] 2.2 `contact_sphere_radius` 僅在 `support_contact_type == ContactType::spSphere` 時寫入正值，否則寫 `0.f`
- [x] 2.3 全部讀取都在生成迴圈外一次性完成，與 `head_diameter` 同一位置

## 3. `SupportPointGenerator.cpp` 生成點時寫入全部欄位

- [x] 3.1 `support_part_overhangs()`（slope 點）：建構後立即賦值四個新欄位
- [x] 3.2 `support_island()`（island 點，第一處）：同上
- [x] 3.3 `support_peninsulas()`（island 點，第二處）：同上
- [x] 3.4 已確認三處皆從呼叫鏈中同一個 `config` 物件讀值（`generate_support_points()` 內 `:1232/1247/1252` 三處呼叫傳的是同一個區域變數 `config`），無不同步風險
- [x] 3.5（新增）建置驗證：`libslic3r.vcxproj` 與 `libslic3r_gui.vcxproj`（Release／x64）皆增量編譯通過，`SupportPointGenerator.cpp`／`SLAPrintSteps.cpp` 無編譯錯誤或新增警告

## 3a. 修復 `SupportTreeBuildsteps.cpp` 既有的 `back_r` 解析 bug（實測 4.2 期間發現，併入本 change，見 design.md D5）

- [x] 3a.1 `filter()`（`:812-818`）的 `back_r` 手刻邏輯改為直接呼叫 `point_head_back_radius_mm(sp, m_cfg.head_back_radius_mm)`，移除 `sp.type == manual_add` 判斷與 `pillar_radius` fallback
- [x] 3a.2 建置驗證：`libslic3r.vcxproj` 增量編譯通過，`SupportTreeBuildsteps.cpp` 無編譯錯誤或新增警告
- [x] 3a.3（程式碼檢視確認）手動點行為：`head_back_radius_mm >= 0.f` 時新舊邏輯結果相同；差異只在 `head_back_radius_mm < 0` 且 `pillar_radius > 0` 這個組合——舊邏輯退回 `pillar_radius`，新邏輯（比照 helper）退回即時 preset。這正是 Change A D2b 已經在 GUI 端做過的同一個修正，現在切片引擎端也做，屬預期行為對齊、非新回歸；Change A 之後新建立的手動點 `head_back_radius_mm` 恆已凍結（不會是 -1），只有舊版存檔的既有手動點可能落入這個差異

## 3b. 修復柱體「加粗」機制被 D5 意外喚醒的既有 bug（複測 4.2 期間發現，併入本 change，見 design.md D6）

- [x] 3b.1 確認機制起源：`search_widening_path`／加粗觸發條件源自 `1555904be`（2022-07-15，BambuStudio 原始碼匯入），既有結構安全設計；手動點專屬的關閉判斷源自 `075881d38`（2026-04-29，`sla-manual-support-per-point-sizing`）
- [x] 3b.2 `routing_to_ground()` 群中心路徑（`create_ground_pillar` 第一處呼叫）：移除 `sp.type == manual_add` 專屬的 `widen` 判斷，`allow_widening` 固定傳 `false`
- [x] 3b.3 `routing_to_ground()` sidehead fallback 路徑（`create_ground_pillar` 第二處呼叫）：原本硬編碼 `sw = true`，改為固定傳 `false`
- [x] 3b.4 `connect_to_ground()`（`create_ground_pillar` 第三處呼叫）：`widen_ctg = !is_manual_small` 改為固定 `false`；同函式內另一處用 `is_manual_small` 控制 bridge 搜尋距離縮放的邏輯，改成型別無關的 `radius < m_cfg.head_back_radius_mm` 判斷
- [x] 3b.5 建置驗證：`libslic3r.vcxproj` 增量編譯通過，`SupportTreeBuildsteps.cpp` 無編譯錯誤或新增警告
- [x] 3b.6（追問中發現並修復）`allow_widening` 除了控制加粗，還在 `create_ground_pillar()` 內被借用來判斷 headless／synthetic 橋接接點（`head_id == ID_UNSET`，查不到原始支撐點時）是不是手動點（`is_manual = !allow_widening`）——三處呼叫都固定傳 `false` 後這段判斷會失準，把 auto 點誤判成手動點。已改為此情境保守預設 `false`（不再依賴 `allow_widening`），確認獨立後將 `allow_widening` 參數、內部加粗 `if` 分支、變成無呼叫者的 `search_widening_path()` 一併從 `.cpp`／`.hpp` 移除，不留死參數。重新編譯通過
- [x] 3b.7 手動驗收（已確認通過）：重跑觸發此 bug 的原始情境（Lower Diameter=1.2 生成 → 改 3 → 進 Manual 加點 → Apply → Structure 模式），**全部**auto 點（不分群中心／橋接鄰居角色）的支撐柱下端維持生成當下的凍結值，不再有「部分變粗」

## 3c. 修復 Auto 模式 Apply 按鈕不因 Top 欄位變動重新啟用的問題（複測 4.5 期間發現，併入本 change，見 design.md D7）

> **後續更新：本節的機制已於 3e 整個移除**，改為 Apply 按鈕恆為可按。本節 checkbox 保留作為問題發現與當時修法的記錄，程式碼中 `auto_settings_need_apply()`／`mark_auto_settings_applied()`／`m_applied_auto_*` 已不存在。

- [x] 3c.1 `auto_settings_need_apply()`（`:1548`）擴充比對 `support_head_front_diameter`／`support_head_back_diameter`／`support_segment_length`／`support_head_penetration`／`support_contact_type`／`support_contact_diameter`，與密度/權重/角度同等對待
- [x] 3c.2 `mark_auto_settings_applied()` 同步儲存這六個欄位的基準值到新增的 `m_applied_auto_*` 成員
- [x] 3c.3 建置驗證：`libslic3r_gui.vcxproj` 增量編譯通過，`GLGizmoSlaSupports.cpp` 無編譯錯誤或新增警告
- [x] 3c.4 手動驗收（已確認通過）：Auto-generate 一次後，單純改任一 Top 欄位（不做其他操作），Apply 按鈕立即重新啟用，不需要離開模式再回來

## 3d. 修復 Apply 按鈕啟用後按下去沒有實際重新生成的問題（複測 3c.4 期間發現，併入本 change，見 design.md D8）

- [x] 3d.1 確認根因：`slaposSupportPoints` 的失效清單不含任何 Top 欄位（刻意設計，避免被動重切觸發悄悄整批重造），`auto_generate()` 呼叫的 `reslice_until_step()` 只在該 step 已失效時才真的重算，單純改 Top 欄位不會讓它失效，因此瞬間空轉
- [x] 3d.2 `SLAPrint.hpp` 新增 public 方法 `invalidate_support_points_for_object(ObjectID)`，透過既有 `friend class SLAPrint` 關係呼叫 `SLAPrintObject` protected 的 `invalidate_step(slaposSupportPoints)`；標成 `const` 以對齊 `GLCanvas3D::sla_print()` 回傳的 `const SLAPrint*`
- [x] 3d.3 `GLGizmoSlaSupports::auto_generate()` 在 `reslice_until_step()` 之前呼叫這個新方法，確保明確按下 Apply 一定強制重跑，被動重切觸發不受影響
- [x] 3d.4 建置驗證：`libslic3r.vcxproj`（`SLAPrint.hpp` 異動）與 `libslic3r_gui.vcxproj`（呼叫端）皆增量編譯通過，無編譯錯誤或新增警告
- [x] 3d.5（已確認，見 3f.6）Auto-generate 一次後單純改 Top 欄位、按下 Apply，畫面確實變化，新點的五個欄位凍結為新值
- [x] 3d.6（已確認，見 4.2/4.4 的除錯與複測過程）被動重切觸發在只改過 Top 欄位、未按 Apply 的情況下，不會讓既有 auto 點外觀改變

## 3e. 移除 Apply 按鈕的 dirty-tracking 機制，改為恆為可按（複測 3c 期間發現新的時序問題，決定整個機制移除，見 design.md D9）

- [x] 3e.1 確認問題：依序編輯多個 Top 欄位時，改第一個不會被偵測到，要改第二個才會兩個一起被偵測到——追查方向指向 SLA Top 欄位在 `Tab::on_value_change()` 走的提早 return 路徑跳過了 `Field` 物件正常刷新 `m_value` 快取的流程（`Field.cpp` `TextCtrl::propagate_value()`／`value_was_changed()`），但未接除錯器實際確認，信心程度中等
- [x] 3e.2 決策：不追這個時序 bug，直接移除整個 dirty-tracking 機制——Apply 本身 idempotent（生成器固定 seed）、真正的安全機制是確認對話框不是按鈕狀態、且此機制這輪已連續出兩次問題（3c 之前完全沒追蹤 Top 欄位／3c 本身的時序不同步）
- [x] 3e.3 移除 `auto_settings_need_apply()`、`mark_auto_settings_applied()` 函式本體與宣告
- [x] 3e.4 移除 `m_applied_auto_weight`／`m_applied_auto_density`／`m_applied_auto_critical_angle`／`m_applied_auto_front_diameter`／`m_applied_auto_back_diameter`／`m_applied_auto_segment_length`／`m_applied_auto_penetration`／`m_applied_auto_contact_type`／`m_applied_auto_contact_diameter`／`m_auto_baseline_initialized` 全部成員變數
- [x] 3e.5 移除全部呼叫端：`render_auto_support_panel()` 的基準值初始化區塊與 `m_imgui->disabled_begin(!auto_settings_need_apply(mo))`（Apply 按鈕改為直接渲染，不再包 disabled 區塊）；`on_set_state()`／`data_changed()`／`apply_remove_all()` 裡重置 `m_auto_baseline_initialized` 的三處；`auto_generate()` 裡標記基準值的兩行
- [x] 3e.6 建置驗證：`libslic3r_gui.vcxproj` 增量編譯通過，無編譯錯誤或新增警告，`grep` 確認無殘留參照
- [x] 3e.7 手動驗收（部分通過）：Apply 按鈕全程保持可按（3e 目標達成）；但依序編輯多個 Top 欄位後按下 Apply，發現只有最後一個欄位以外的異動沒被吃到——這是 3e 沒有解決的**功能性**問題（3e 只處理按鈕狀態），追查與修法見 3f

## 3f. 修復依序編輯多個 Top 欄位、按下 Apply 只吃到部分欄位的問題（複測 3e.7 期間發現，併入本 change，見 design.md D10）

- [x] 3f.1 確認現象：改第一個 Top 欄位後直接按 Apply，畫面沒反應；接著改第二個欄位，Apply 才會同時吃到兩者。Process tab 的「已修改」橙點提示也呈現同樣落後一拍的模式
- [x] 3f.2 排除假說：`Field.cpp` 的 `TextCtrl::value_was_changed()` 會在每次呼叫時自我更新 `m_value` 快取，理論上不需要外部 `set_value()` 才能刷新，靜態追蹤沒有找到明確斷點；`support_contact_diameter` 雖然依賴 `support_contact_type == Sphere` 才致能（`ConfigManipulation.cpp:1037-1039`），但重現步驟未涉及切換 Contact Type，此路徑排除
- [x] 3f.3 關鍵發現：`flush_process_top_fields_to_config()` 只有「點擊模型放置新手動點」路徑（`:922`）會在使用前主動呼叫；`auto_generate()`（Apply 按鈕）完全沒有，完全依賴每個欄位各自的 wx commit 事件鏈準時觸發，一旦某欄位的事件鏈沒準時觸發，`auto_generate()` 讀到的就是該欄位在 preset 裡的舊值
- [x] 3f.4 修法：`auto_generate()` 開頭比照放置新手動點的既有寫法，主動呼叫一次 `flush_process_top_fields_to_config()`，讓「按下 Apply」本身成為權威同步點，不再依賴 wx 欄位事件時序是否精確
- [x] 3f.5 建置驗證：`libslic3r_gui.vcxproj` 增量編譯通過，無編譯錯誤或新增警告
- [x] 3f.6 手動驗收（已確認通過）：依序編輯多個 Top 欄位，每改完一個就立刻按 Apply，確認每一次都正確反映當時已編輯的全部欄位——功能性目標達成。複測期間額外發現一個純視覺的殘留問題（見 8. Follow-up）：Apply 之後再次編輯 Top 欄位，「已修改」提示（變色＋放棄修改按鈕）偶爾要連續改兩三個欄位才會一起顯示，但實測確認底層資料與 Apply／discard 功能皆正確（點擊看不到的「放棄修改」按鈕位置，數值確實會跳回 default），純粹是提示沒有即時重繪，不影響本 change 的驗收判準

## 4. 驗證：生成後即凍結

- [x] 4.1（已於除錯過程中反覆確認）用 Top 欄位值 A 觸發 Auto-generate，每一顆新生成點的五個 Top 欄位皆為 A 對應的具體值
- [x] 4.2（已確認，見 3a/3b 除錯過程）不重新生成，改 Lower Diameter 為 B，切到 Structure／完整切片，支撐頭 Lower 直徑仍是 A——3a／3b 兩個既有 bug 就是靠這個情境反覆重現、修復、複測到通過
- [x] 4.3（已確認，見 3f.6 複測）Segment Length／Penetration／Contact Diameter 逐一測過，皆維持生成當下的值，不受後續面板編輯影響
- [x] 4.4（已確認，見 3b.7 最終複測）切到 Structure 檢視模式，支撐結構幾何反映生成當下的值，不是切換當下的即時面板值
- [x] 4.5（已確認，見 3f.6）重新按下 Auto-generate，新點的五個欄位正確凍結為新值 B

## 5. 驗證：與 Change A 交叉確認（Change A 已於 2026-08-06 完成並歸檔，非條件式，直接可測）

- [x] 5.1（已確認，貫穿整個除錯過程）preview 顯示的 auto 點尺寸與本 change 的切片輸出一致，兩邊都是生成當下凍結值
- [x] 5.2（已確認，見「下直徑=1.2生成→改3→Manual加點→Apply→Structure」測試）auto 點與手動點在「生成/建立後即凍結全部欄位」這件事上行為一致

## 6. 驗證：效能與快取

- [ ] 6.1 用一個會生成數千顆 auto 支撐點的模型，比較本 change 前後 Auto-generate 的執行耗時，確認差異在量測誤差範圍內
- [ ] 6.2 確認 `perf-sla-support-points-preview-render` 的 `HeadGeomKey` distinct 數量與 64 筆門檻觸發頻率，本 change 後持平或改善，不劣化

## 7. 驗證：不得回歸

- [x] 7.1（程式碼檢視確認）`SLAPrintObject::invalidate_state_by_config_options()`（`SLAPrint.cpp:1120-1149`）完全未被本 change 觸碰，密度類參數與 Top 欄位的失效對應關係維持原樣
- [x] 7.2（程式碼檢視確認，範圍已隨 3a/3b 更新）第 1-3 節只動 `support_part_overhangs()`／`support_island()`／`support_peninsulas()`（`island`/`slope` 專用路徑）與 `SupportPointGeneratorConfig`，手動點生成/凍結路徑（`freeze_process_top_into_point()`，`GLGizmoSlaSupports.cpp`）完全沒有觸碰。3a／3b 額外修的 `SupportTreeBuildsteps.cpp` 是共用的切片消費端，手動點也會經過，但影響方向是**修正既有 bug、對齊 point_*() helper 既定規則**（3a.3／3b 已記錄手動點行為差異僅限邊緣情況、且是預期對齊而非回歸），不是引入新的手動點專屬邏輯
- [x] 7.3（程式碼檢視確認）`SupportPoint::serialize()`（`SupportPoint.hpp:117`）與 5-float 相容建構子皆未修改；新欄位只存在於 `SupportPointGeneratorConfig`（單次生成呼叫內的暫存結構，從不序列化），不影響任何既有／舊版 3mf 的支撐點資料
- [x] 7.4 承 0.2，全域搜尋只有一處建構點，已在 `support_points()` 內完整初始化，無其他呼叫點需要確認

## 8. Follow-up（out of scope）

- preview／picking 的讀取邏輯修正 → `fix-sla-support-preview-geometry-source-semantics`（Change A，平行進行，互不阻擋）
- 手動點的 `head_back_radius_mm` 凍結（`pillar_radius` fallback 移除）→ 屬於 Change A 範圍，非本 change
- **Process tab「已修改」提示（變色＋放棄修改按鈕）偶爾要連續編輯兩三個 Top 欄位才會一起顯示，純視覺重繪延遲，不影響底層資料／Apply／discard 的實際功能**（3f.6 複測期間發現）。根因在既有的 `schedule_support_top_ui_sync()`／`update_changed_ui()` 刷新機制（`Tab.cpp`），源頭可追溯到 2026-06-16 的 `06ddfd10f fix(sla-ui): refresh support Top dirty/reset state immediately`，早於本 change 與整個支撐點系列 change，與本 change「auto 點凍結」的範圍無關，不在此修。若要修，需另開一個聚焦於 Top 欄位 UI 刷新時機的 change
