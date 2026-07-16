<!--
執行紅線（apply 階段必須遵守）：
① 一次只做一個「## 階段」。做完該階段最後的「🛑 手動驗證閘門」即必須停止，等待使用者回報通過，嚴禁連做多階段。
② 驗證以「使用者手動操作 GUI／手動切片看 3D 幾何」為主。
③ 嚴禁自動執行任何編譯／建置指令。每階段結束一律提示：「請使用者自行在本地端編譯，編譯成功後再回來手動驗證」。
-->

## 1. 參數宣告與註冊（GUI 隱藏、僅後台 config 可編輯）

- [x] 1.1 於 `SLAPrintObjectConfig`（`src/libslic3r/PrintConfig.hpp`，約 L1718–1728 pad 選項區）新增 `((ConfigOptionBool, pad_split_rafts))`、`((ConfigOptionFloat, raft_gap_threshold))`、`((ConfigOptionFloat, raft_bridge_width))`
- [x] 1.2 於 `src/libslic3r/PrintConfig.cpp`（約 L7748，`pad_max_merge_distance` 定義附近）新增三個 `def = this->add(...)`：保留 label／tooltip／sidetext／min／mode／預設值（**`pad_split_rafts=true`**、`raft_gap_threshold=5.0`、`raft_bridge_width=2.0`），tooltip 說明「間隙為肉眼可見成品間隙」與「約 2×brim 物理下限」；**但不設 `category`（hidden setting，沿用 `pad_object_connector_*` 先例）**，使其不出現於物件齒輪「加入設定」選單
- [x] 1.3 於 `src/libslic3r/Preset.cpp`（SLA print 選項清單，約 L1050–1053）註冊三個新 key（使用者 preset `.json`／專案 `.3mf` 內嵌 config 存取所需）
- [x] 1.4 **移除** `src/slic3r/GUI/Tab.cpp`（約 L7166–7168）先前新增的三行 `append_single_option_line` → 自 SLA 設定頁與設定搜尋移除
- [x] 1.5 **移除** `src/slic3r/GUI/ConfigManipulation.cpp` `toggle_print_sla_options` 內先前新增的灰化連動區塊（GUI 無欄位即無需灰化）
- [x] 1.6 🛑 手動驗證閘門：請使用者自行本地編譯，成功後確認 —（a）三個參數**不出現於**任何 GUI 面：SLA 設定頁、設定搜尋框、物件右鍵「加入設定」齒輪選單（Support 分類）；（b）於使用者 preset `.json`（`%APPDATA%\PhrozenOrca\user\...\sla_print\*.json`）或專案 `.3mf` 內嵌 config 設 `pad_split_rafts=1`／`raft_gap_threshold=5`／`raft_bridge_width=2`，存檔重載後數值保留（round-trip）且切片流程讀得到值；（c）預設值正確（**on** / 5.0 / 2.0；註：本閘門當初以預設 off 驗證，預設值於階段 6 後依使用者決策改為 on，僅改預設、未改路徑），`pad_split_rafts=false` 切片與原廠一致。通過前不得進入階段 2。

## 2. 參數映射與藍圖路由（骨架，行為暫等同舊邏輯）

- [x] 2.1 於 `src/libslic3r/SLA/Pad.hpp` 的 `PadConfig` 新增欄位 `bool split_rafts=true; double raft_gap_threshold_mm; double raft_bridge_width_mm;`（預設須與 `PrintConfig.cpp` 同步）
- [x] 2.2 於 `src/libslic3r/SLAPrint.cpp` `make_pad_cfg`（L194）將三個新 config 映射進 `PadConfig`
- [x] 2.3 於 `src/libslic3r/SLA/Pad.cpp`（`wafflized_concave_hull` L292 與 `BelowPadSkeleton` L334 的 ConcaveHull 建構點）加入依 `cfg.split_rafts` 的分派；**此階段 true 分支先呼叫既有 ConcaveHull 舊邏輯（等同行為的骨架）**，`get_merge_distance` 保留供舊路徑
- [x] 2.4 🛑 手動驗證閘門：請使用者自行本地編譯，成功後手動切片確認 — `pad_split_rafts=false` 時雙 Cube 仍黏成一片（與原廠一致、無回歸）；勾選 `pad_split_rafts=true` 切片不崩潰、行為暫時仍同舊邏輯。通過前不得進入階段 3。

## 3. 核心 Edge-Gap 判準：分離（尚未畫橋）

- [x] 3.1 於 `ConcaveHull`（`ConcaveHull.hpp`/`.cpp`）新增 edge-gap 合併路徑（新建構子或 sibling 函式），**舊建構子完全不動**
- [x] 3.2 實作 broad-phase：以 `BoxIndex`（`SpatIndex.hpp`；參考 `Pad.cpp` `Intersector` 用法）收各島 bbox、以「外擴 `raw_thresh`」查相交得候選 pair(i<j)，其中 `raw_thresh = scaled(raft_gap_threshold) + 2·get_waffle_offset(cfg)`（路線乙）
- [x] 3.3 實作 narrow-phase：每島建一次 `AABBTreeLines::LinesDistancer`，pair 雙向 vertex→edge 取最短 `g_raw` 與最近點對；`g_raw ≤ raw_thresh` 判定合併
- [x] 3.4 此階段合併僅做 `union_ex`（不畫橋），讓「不合併」的島各自獨立成筏，再接回 waffle 流程
- [x] 3.5 🛑 手動驗證閘門（**僅驗證分離**；「大門檻合併」因本階段尚未畫橋，移至 4.4 驗）：請使用者自行本地編譯，成功後手動切片確認 — 雙 Cube（中心 33mm、`split=true`、`raft_gap_threshold=5`）產生**兩片互不相連的獨立底筏**（3D 幾何可見分離）；對照 `split=false` 仍黏成一片。通過前不得進入階段 4。

## 4. 受控連接橋生成（近距島群接合、抗碎屑）

- [x] 4.1 對判定合併的 pair，以最近點對 `(pa,pb)` 生成寬 `raft_bridge_width`、兩端各內插 penetration 進島體的橋 quad，加入聯集集合
- [x] 4.2 g→0 guard（甲 A3）：`(pb−pa)` 近乎零時改用該處輪廓局部法線定橋向
- [x] 4.3 微小島 clamp（甲 A4）：橋寬與 penetration 依島尺寸上限裁切
- [x] 4.4 🛑 手動驗證閘門：請使用者自行本地編譯，成功後手動切片確認 —（a）將雙 Cube 拉近至成品間隙 < 2mm（物理下限內）→ 產生**單一連通底筏、以清楚的 ~2mm 頸部接合、無細碎 sliver**；調整 `raft_bridge_width` 可見頸部寬度改變。（b）**（由 3.5 移入）** 雙 Cube 維持中心 33mm，把 `raft_gap_threshold` 調到很大（如 40）→ 因橋已實作，兩片**又合回單一連通底筏**；門檻設 5 則維持兩片獨立。通過前不得進入階段 5。

## 5. 跨島冗橋濾除與自然閉環

- [x] 5.1 實作跨島濾除：對每條候選橋線段，先以 AABB 粗篩可能相交島，再以「線段 vs 島 contour 精確相交」為準；命中第三島即丟棄該橋
- [x] 5.2 確認允許自然閉環（不以 MST 砍邊）：符合門檻的相鄰 pair 全數接受，僅濾掉穿越島的跨接橋
- [x] 5.3 🛑 手動驗證閘門：請使用者自行本地編譯，成功後手動切片確認 —（**環狀案例**：**8 個 8mm Cube 均勻排在直徑 40mm 圓周上**，`split=true`、`raft_gap_threshold=5`、`raft_bridge_width=2` → 以預設 `pad_brim_size=1.6` 計 `raw_thresh = 5 + 2×1.6 = 8.2mm`；相鄰中心距 `40·sin(22.5°)=15.31mm` → 邊距 ≈**7.3mm ≤ 8.2 → 接合**，跨徑（skip-one）中心距 `40·sin(45°)=28.28mm` → 邊距 ≈**20.3mm > 8.2 → 濾除**）：預期 **① 不生成橫跨中央的蜘蛛網式弦橋、② 僅相鄰接合為單一連通底筏、③ 切片正常完成不崩潰**。⚠ **中央孔洞被填平屬系統既有行為（`.contour` 機制），不列入本閘門判準**——見 design.md「已知原廠邊界二」（已實測確認）。（成排案例）一排小 Cube → **僅相鄰接合、無跨島橋**。通過前不得進入階段 6。

## 6. 確定性、效能與回歸收尾

- [x] 6.1 確定性（甲 A6）：候選 pair 依 `(i,j)` 排序、等距 tie-break 以島索引、橋幾何依固定順序聯集；narrow-phase 若用 TBB 並行則於輸出前排序 —— 已稽核：`std::set<pair<i,j>>` 使走訪順序不受 `BoxIndex` 查詢順序影響；narrow 以嚴格 `d < best` 使低島索引/低頂點索引勝出；輸出依 `(i,j)` 排序且橋依同序 append 後才 union；全程循序（未用 TBB），保證鏈已寫入 `find_merge_pairs` 註解
- [x] 6.2 兩條 pad 路徑（around-object 與 below-object）皆走新路徑並各自驗證 —— 已確認兩處建構點（`Pad.cpp:326` `wafflized_concave_hull`／`Pad.cpp:358` `BelowPadSkeleton`）皆經單一咽喉點 `make_pad_concave_hull`（`Pad.cpp:156`）分派；並各自新增自動化測試（見 6.3）
- [x] 6.3 補充 `tests/sla_print/` 回歸與 edge-gap 單元測試（可離線執行，非自動編譯主程式）—— 新增於 `tests/sla_print/sla_print_tests.cpp`：`SplitRaftsPadGeometryIsValid`（below-object）、`SplitRaftsPadAroundObjectIsValid`（around-object）、`Edge gap raft splitting decides merging by real contour gap`（門檻低於間隙→2 片；高於間隙→1 片）、`Edge gap raft splitting is deterministic`（重複建構逐點比對）。舊路徑回歸由既有 4 個 pad 測試涵蓋——因 `PadConfig::split_rafts` 預設已改為 true，該 4 測試已**顯式設 `padcfg.split_rafts = false`** 以維持其驗證舊邏輯之意圖
- [x] 6.4 🛑 手動驗證閘門：請使用者自行本地編譯，成功後手動確認 — 同一模型與設定重複切片多次**結果完全一致**；放置密集支撐（數百接地島）切片**不卡死**；`pad_split_rafts=false` 全面回歸原廠。全部通過即完成本變更。—— **6.4 Verified manually by user**（另：「確定性」亦由單元測試 `Edge gap raft splitting is deterministic` 自動覆蓋）
  - **文件對齊（`/opsx:verify` 稽核後補正）**：W-1 已修正 `PrintConfig.hpp` 註解與 `/*= true*/`（三處預設值一致）；W-2 已於 `Pad.cpp` 以 `std::max<coord_t>(scaled(raft_gap_threshold), 2*wo) + 2*wo` 夾持物理下限，並同步改寫 `design.md` D-5（含 `2·waffle_offset` 之精確推導）與 `spec.md` R3（新增「物理下限夾持」限制說明及 `門檻低於物理下限時夾持於下限` scenario），使規格與程式碼 100% 誠實對齊；S-1 已補 `Edge gap raft bridges are clamped to tiny islands`、`Edge gap raft bridges penetrate the islands and leave no sliver` 兩個專屬單元測試
