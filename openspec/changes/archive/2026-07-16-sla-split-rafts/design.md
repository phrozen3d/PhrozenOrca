## Context

Resin（SLA）底筏合併發生在 `ConcaveHull`（`src/libslic3r/SLA/ConcaveHull.cpp`）。現行流程：`union_ex` 所有 support/model 輪廓 → 若島數 >1 → 以各島**質心**建點索引 → `add_connector_rectangles` 用「質心對最近質心距離」與門檻 `scaled(mergedist)` 比較，畫連接橋 → 再 `union_ex`。門檻來自 `get_merge_distance(cfg) = 2·(1.8·wall_thickness) + max_merge_distance`（`Pad.cpp:144`），預設 `= 57.2mm`。

此設計的根本問題是「質心距離」判準：尺寸盲目、且隱藏 `+7.2mm` 使 UI 數值與實際門檻不一致。實測兩個 10mm cube（中心 33mm）因 33 < 57.2 被黏成一片。

既有可重用資產：`BoxIndex`（R-tree，`SpatIndex.hpp`，`Intersector` 已示範）、`AABBTreeLines::LinesDistancer`（`distance_from_lines_extra<false>()` 回傳 `{距離, 邊索引, 最近點}`）、`get_waffle_offset(cfg)`（`Pad.cpp:139`）、ClipperUtils（`offset`/`union_ex`）。合併判準是 `AroundPadSkeleton`、`BelowPadSkeleton` 兩條 pad 路徑共用的單一咽喉點。

## Goals / Non-Goals

**Goals:**
- 讓「成品 footprint 間隙」超過使用者門檻的島群各自獨立成筏。
- UI 參數語意誠實：`raft_gap_threshold` = 肉眼可見的兩片筏成品間隙。
- **預設開啟**（新行為即預設）；保留 `pad_split_rafts=false` 作為完整退回原廠質心邏輯的逃生門（風險隔離改由「可退回」而非「預設關閉」達成）。
- 近距島群以足夠強度的受控橋接合，避免切片碎屑（sliver）。
- 效能可承受密集支撐（數百接地島）。

**Non-Goals:**
- 不在「本該連成一塊」的密集區強制切割（那是另一種需求，非本案）。
- 不移除或重定義既有 `pad_max_merge_distance`（舊路徑原封保留）。
- 不追求消除「~2·waffle_offset 物理下限」——這是幾何先天限制，已接受。
- 不改動 waffle/brim 幾何生成與 embed_object（pad_around_object）邏輯本身。

## Decisions

### D-1 判準：edge-to-edge gap 取代質心距離
量測島輪廓間**真實最短間隙**而非質心距離。
- **為何**：質心距離尺寸盲目（大物件質心被推遠反而難合併），且無法對應使用者直覺。
- **替代方案**：(a) 僅調小 `max_merge_distance`／移除隱藏 7.2mm＝治標，判準仍質心式，已否決；(b) 全域形態學 closing（offset ±t/2）＝會填滿單一凹島的凹灣、跨中間島鏈式合併、大半徑 round offset 昂貴，已否決。

### D-2 路線乙：門檻折入 waffle 外擴，量測仍在 raw contour
合併條件 `g_raw ≤ raw_thresh`，其中 `raw_thresh = scaled(raft_gap_threshold) + 2·get_waffle_offset(cfg)`。
- **為何**：pad 最終會被 waffle/brim 外擴 `waffle_offset`，成品間隙 `g_fin = g_raw − 2·waffle_offset`。把此常數折入門檻，即可在 raw contour 上量測（廉價）而語意對應成品間隙——**無需實際 offset 每座島**。
- **替代方案**：實際把每島 offset waffle 後再量測，正確但每島多一次昂貴 offset，已否決。
- **近似說明**：waffle 用 round join，凸角處外擴非嚴格等於 `waffle_offset`，但最近點接近處近似直線逼近，`2·waffle_offset` 相減足夠準確。

### D-3 效能：broad-phase + narrow-phase 兩級
- Broad：`BoxIndex` 收各島 bbox，以「bbox 外擴 raw_thresh」查相交 → 候選 pair(i<j)，O(n log n)。
- Narrow：每島建一次 `LinesDistancer`（重用於該島所有 pair）；pair(A,B) **雙向 vertex→edge** 取最短，得 `g_raw` 與最近點對 `(pa,pb)`。因兩不相交簡單多邊形最短距離必為 vertex–edge，雙向查詢即 exact。
- **為何雙級**：真正規模風險是密集支撐的數百微小島；broad-phase 把 O(n²) 砍成近鄰。narrow-phase 可 TBB 並行。
- **替代方案**：offset-intersect 布林測試僅得 yes/no、拿不到畫橋所需最近點，已否決（`LinesDistancer` 一次給距離＋最近點，更合用）。

### D-4 橋接：受控最近點對短橋，取代質心橋
合併 pair 一律以 `(pa,pb)` 生成寬 `raft_bridge_width`、兩端各內插 penetration 進島體的 quad。
- **為何非質心橋**：C 形/中空/帶洞島的質心可能落在島體外或洞中 → 質心橋不接觸島體 → union 後浮空碎屑。最近點對橋短、局部、正落在間隙最窄處。
- **為何要 penetration**：兩端插入島體使橋與島**實質重疊**而非點觸，Clipper union 才不會留狹縫 sliver（沿用 `breakstick_holes` 精神）。
- **g→0 guard（甲 A3）**：近乎相觸時 `(pb−pa)` 正規化除零 → 改用該處輪廓局部法線定橋向。
- **微小島 clamp（甲 A4）**：`raft_bridge_width`、penetration 依島尺寸上限裁切，避免橋吃穿小島。

### D-5 物理下限：主動畫橋，不放任 waffle-kiss（門檻夾持）
`g_fin ≤ 2·waffle_offset` 的近距島群一律走「畫受控橋」乾淨路徑。
- **為何**：放任兩島於 waffle 外擴時「接吻」焊接，接觸高度可能趨近 0 → 切層 neck < 1 px → 斷成碎屑。受控橋 neck 恆 ≥ `raft_bridge_width`，穩固。
- **物理下限的來源（精確等價）**：`offset_waffle_style()` 執行 `closing(polys, 2·wo, wo)`（`wo = get_waffle_offset(cfg)`）。其 `expand(+2·wo)` 步驟本身即會焊接 `g_raw ≤ 4·wo` 的島對，且後續 `shrink(−wo)` 不會使已合併的 blob 再分開。因 `g_fin = g_raw − 2·wo`，得
  **waffle 自動焊接 ⟺ `g_raw ≤ 4·wo` ⟺ `g_fin ≤ 2·wo`** —— 這正是本節「物理下限 = `2·waffle_offset`」的來源，並非經驗值。
- ⭐ **門檻夾持（實作要點）**：畫橋條件為 `g_fin ≤ D`，與 waffle 焊接條件 `g_fin ≤ 2·wo` **並不自動一致**。若 `D < 2·wo`，落在 `(D, 2·wo]` 的島對會「**不畫橋卻仍被 waffle 焊接**」→ 正是本節要杜絕的不受控細頸。故 `raw_thresh` 必須把門檻夾到物理下限以上：
  ```cpp
  coord_t wo = get_waffle_offset(cfg);
  coord_t raw_thresh = std::max<coord_t>(scaled(raft_gap_threshold), 2 * wo) + 2 * wo;
  ```
  語意＝「**凡 waffle 會焊接的，一律先畫受控橋**」。僅在使用者把 `raft_gap_threshold` 設到 `2·wo`（預設 brim 1.6mm 時約 3.2mm）以下時才生效；預設 5mm 不受影響。
- **修訂註記**：本夾持係 `/opsx:verify` 稽核發現（原推理「`g_fin ≤ 0 ≤ D` 自動落入合併」僅涵蓋 `g_fin ≤ 0`，未涵蓋 `(0, 2·wo]` 區段）後補上。

### D-6 拓樸：接受全部 `g_fin ≤ D` 的 pair，以「跨島濾除」消蜘蛛網（非 MST）
不使用 union-find/MST 砍邊。接受所有符合門檻的 pair，但**濾除「橋線段穿越第三島輪廓」的橋**。
- **為何非 MST**：MST 會砍成 C 形開口筏；SLA 二次固化時 C 形兩端收縮應力不均 → 翹曲。允許自然閉環（如一圈**離散島群**接合成環狀底筏、中央留空）在應力分佈上更穩定。
- **正確性**：橋 A–B 若真穿過島 C，代表 C 介於中間 → `gap(A,C)`、`gap(C,B)` 皆 < `gap(A,B) ≤ D` → A、B 必經 C 傳遞連通 → 刪 A–B 不損失連通。
- **精度修正**：濾除**最終以線段 vs contour 精確相交為準**；AABB 僅作 broad-phase 快篩「要測哪些島」。理由：AABB 太鬆，斜長島包圍盒會誤含不相干橋、誤刪必要橋致群組斷裂。

### D-7 風險隔離與參數
新增 `pad_split_rafts`（bool, **預設 true**）、`raft_gap_threshold`（float, 5.0mm）、`raft_bridge_width`（float, 2.0mm）於 `SLAPrintObjectConfig`。`pad_split_rafts=true`（預設）走新 edge-gap 路徑；設為 `false` 時**完全走現行 `ConcaveHull` 舊建構子**。新舊路徑並存、`get_merge_distance` 保留供舊路徑。
- **預設值變更（true）之取捨**：新行為成為預設 → 對既有 profile 屬 BREAKING（切片結果改變），換取「使用者無需知道隱藏參數即可獲得分區底筏」。因本參數為 hidden setting（D-8，GUI 不可見），若預設為 false 則實質等同功能未啟用。風險隔離改由「舊路徑完整保留 + 可於後台設 `0` 即時退回」達成，而非「預設關閉」。
- 一致性要求：`PrintConfig.cpp` 的 `pad_split_rafts` 預設與 `Pad.hpp` 的 `PadConfig::split_rafts` 成員預設**必須同步**（後者影響直接建構 `PadConfig` 的呼叫端，如測試）。

### D-8 GUI 隱藏（hidden setting，僅後台可編輯）
三個參數不曝露於任何 GUI 面，僅保留宣告／註冊／預設值供進階使用者以使用者 preset `.json`（`%APPDATA%\PhrozenOrca\user\...\sla_print\*.json`）或專案 `.3mf` 內嵌 config 編輯。
- **為何**：此功能屬進階／實驗性，先不曝露以避免一般使用者誤用；同時保留後台可調性。
- **作法**：三個 `def` 不設 `category`（label／tooltip 可留作 preset `.json` 自我文件）。SLA 物件「加入設定」齒輪選單以 `SettingsFactory::get_options()` 列舉整個 `SLAPrintObjectConfig::keys()`，再於 `get_full_settings_hierarchy()` 依 `category` 分組；`is_improper_category()` 對**空 category** 回傳 true → 該 key 被略過。故空 category 即可在齒輪選單、設定頁、設定搜尋三面同時隱藏。沿用 `pad_object_connector_*`／`pad_object_gap` 既有先例。
- **不影響後台讀寫**：preset `.json`／專案 `.3mf` 內嵌 config 讀寫僅認 key 名（`PrintConfig.hpp` 宣告 + `Preset.cpp` 選項清單註冊 + `def` 預設值即足），與 GUI metadata 無關。
- **替代方案**：以 `toggle_field` 灰化＝仍顯示欄位、不符「畫面上看不到」需求，已否決；於 GUI 端逐一過濾 key＝散落多處、易漏（設定搜尋、齒輪選單），不如源頭空 category 一次到位，已否決。

### 演算法管線（僅 split 開啟）
```
1. union_ex(all contours)                 // 沿用；此後島 disjoint
2. if islands ≤ 1 → return                 // 沿用 early-return
3. broad: BoxIndex，bbox 外擴 raw_thresh → 候選 pair(i<j)
4. narrow: LinesDistancer 雙向 vertex→edge → g_raw、(pa,pb)
          若 g_raw ≤ raw_thresh → 候選合併
5. 跨島濾除: 橋線段 (pa,pb) vs 其他島 contour 精確相交 → 命中即丟棄
6. 生成受控橋: dir 正規化(含 g→0 法線 guard); w/pen 依島尺寸 clamp; 兩端內插
7. union_ex(islands + bridges)             // 接回 waffle/offset 流程
```
決定性（甲 A6）：候選 pair 依 `(i,j)` 排序、橋幾何依序 push、輸出前排序後 union；tie-break 以島索引。

## Risks / Trade-offs

- [稀疏環蜘蛛網] D≫島距時 skip-one 弦橋橫跨中央空洞、不穿任何島 contour → 存活輕微跨橋 → **Mitigation**：預設 `raft_gap_threshold=5mm` 會被 `g_fin ≤ D` 自我限制；且與「允許閉環」意圖一致，可接受。
- **[已知原廠邊界：單一模型自身的內孔不會保留於底筏]** 兩條 pad 路徑在建構 `ConcaveHull` **之前**即以 `ep.contour` 取用藍圖（`Pad.cpp` 的 `BelowPadSkeleton` 與 `wafflized_concave_hull`），`ConcaveHull::merge_polygons()` 內的 `get_contours()` 亦同 → **模型自身的 holes 在上游即被剝除**，底筏恆為「外輪廓的 waffle」＝實心。故單一中空圓柱／環形模型的底筏**必然中央無孔**，且與 `pad_split_rafts` 無關（`split=false` 結果相同，非本變更之回歸）。此為原廠既有行為，本變更**不改動**（呼應 Non-Goals「不改動 waffle/brim 幾何生成」）。
  - 佐證：預設 `pad_brim_size=1.6`、`pad_wall_height=0`、`pad_wall_slope=90°` → `waffle_offset=1.6mm`、closing(+3.2/−1.6)，只能填滿 Ø<6.4mm 的孔；Ø36 的孔即使保留也會存活 → 可證孔並非被 waffle 填滿，而是上游即不存在。
  - 影響：本變更的「自然閉環」僅適用於**複數個獨立島群**（如環狀排列的離散小 Cube／支撐柱），不適用於單一模型的內孔。驗證模型須據此選用（見 tasks.md 5.3）。
  - 若未來需要「環形模型 → 環形底筏（省料、降低離型力）」，屬另一獨立功能，建議另開 change（例如 `sla-pad-preserve-model-holes`）設計，因其須改動 pad 上游剝孔架構並影響 `split=false` 的原廠行為。
- **[已知原廠邊界二：閉環島群圍出的內孔同樣會被實心化]（技術債，本變更接受此限制）** 即使複數島群成功以橋圍成環狀，其**圍出的內孔**在 `ConcaveHull` 輸出或下游 pipeline 中亦會因原廠僅保留 `.contour` 的機制而被實心化。**經實測確認**：8 個離散 Cube 排成圓環，底筏中央孔洞仍被強制填實。
  - 機制：`union_ex(islands + bridges)` 會正確產生「contour=外環、hole=中央孔」的 `ExPolygon`，但緊接的 `get_contours()`（`ConcaveHull.hpp:9`）只取 `.contour` → **hole 於此被丟棄**。整個 `ConcaveHull` 類別皆為 contour-only：成員 `m_polys` 為 `Polygons`、`to_expolygons()` 以無 hole 的 `ExPolygon(p)` 包裝、`offset_waffle_style()` 亦作用於 `hull.polygons()`。
  - **影響範圍界定**：本限制**不影響** D-6 的核心價值——「橋拓樸」仍完全正確（接受閉環、不以 MST 砍邊、濾除穿越島的弦橋），可由「無橫跨中央的蜘蛛網弦橋」驗證。受影響的僅是「中央孔在最終 pad 幾何上是否留空」這一視覺結果。
  - **接受理由**：修正須改動 `ConcaveHull` 的 contour-only 架構（`get_contours`／`m_polys` 型別／`to_expolygons`／waffle 介面），會連帶改變 `split=false` 的原廠行為，違反本變更 Non-Goals 與風險隔離原則。列為技術債，與「邊界一」合併於未來的 `sla-pad-preserve-model-holes` change 一併處理。
- [waffle round-join 近似] `2·waffle_offset` 相減在凸角處非嚴格精確 → **Mitigation**：最近點逼近近直線，誤差在次毫米量級，遠小於門檻。
- [大門檻效能] `raft_gap_threshold` 很大時 broad-phase 幾乎不剪枝、narrow pair 爆增 → **Mitigation**：預設值小；TBB 並行；文件說明大值≈回到單片筏。
- [微小島橋吃穿] 支撐點極小島 → **Mitigation**：甲 A4 依島尺寸 clamp 橋寬/penetration。
- [兩條 pad 路徑一致性] around-object 與 below-object 都需正確分派 → **Mitigation**：合併判準為單一咽喉點，改一處雙路徑生效；兩路徑都納入驗證。

## Migration Plan

- **部署**：⚠ `pad_split_rafts` **預設 true** → 既有 profile 載入後**切片行為即改變**（BREAKING）。不需要 profile 遷移或版本相容處理（新 key 缺漏時即取預設 true），但**須於發行說明告知使用者此行為變更**及退回方式。
- **啟用**：進階使用者於使用者 preset `.json`（`%APPDATA%\PhrozenOrca\user\...\sla_print\*.json`）或專案 `.3mf` 內嵌 config 設 `pad_split_rafts=1`，並填入 `raft_gap_threshold`／`raft_bridge_width`；GUI 不提供欄位（hidden setting，見 D-8）。
- **回退**：關閉開關即完全回到原廠邏輯（同一 build 內可切換），零風險。

## Open Questions

- `raft_bridge_width` 是否需依 MSLA 離型力再做材料相關的預設微調？（目前 2.0mm 為經驗值，先固定，後續可依實測調整。）
- 「~2·brim 物理下限」的說明因 GUI 隱藏（D-8）不再有 tooltip 曝露；改置於 `def` tooltip 註解與 preset `.json` 文件供進階使用者參考。
