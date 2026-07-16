# Handover Summary — `sla-split-rafts`

> 供新會話接軌用。閱讀本檔即可完整承接，無需重跑先前的 grill 對話。

## (1) 當前進度

- **規劃/文件階段（Step 6）：完成。** 四大 artifact 全數產出並通過 `openspec validate sla-split-rafts`（4/4）。
- **實作階段：尚未開始（0/6 微階段）。** tasks.md 的第 1～6 階段皆未動工，尚無任何程式碼變更。
- 下一步 = `/opsx:apply`，但必須遵守 tasks.md 檔頭三條紅線：①一次只做一個 `##` 階段、每階段末尾 🛑 手動驗證閘門通過前不得續作；②驗證以「GUI 目視灰化連動」與「雙 Cube／環狀／成排 手動切片看 3D 幾何」為主；③**嚴禁自動編譯**，每階段結束提示使用者自行本地編譯後再驗證。

## (2) 已拍板的核心幾何決策

**根因**：Resin pad 合併用「質心距離」，門檻 `get_merge_distance = 2·(1.8·wall_thickness) + max_merge_distance`（`Pad.cpp:144`）＝預設 57.2mm。雙 Cube（中心 33mm）因 33 < 57.2 被畫橋黏成一片。治本＝改用真實邊緣間隙。

**D1 風險隔離**：新開關 `pad_split_rafts`（bool, 預設 false → 完全走舊邏輯、100% 相容）＋ `raft_gap_threshold`（float, 5.0mm，語意＝成品 footprint 間隙）。
**D2 橋強度**：`raft_bridge_width`（float, 2.0mm），抗 MSLA 離型張力、防橋斷。
**D3 一視同仁**：support/model 進演算法前已 `union_ex` 成島群，統一用單一 `raft_gap_threshold`。
**D4 允許自然閉環＋消蜘蛛網（非 MST）**：接受所有 `g_fin ≤ D` 的相鄰 pair（允許環狀閉環，避免 MST 砍成 C 形致二次固化翹曲）；
  - ⭐ **關鍵修正：跨島冗橋濾除以「線段 vs 島 Contour 精確相交」為準，AABB 僅作 broad-phase 粗篩。** 理由：AABB 太鬆會誤刪必要橋致群組斷裂。正確性：橋 A–B 若真穿過島 C，則 gap(A,C)、gap(C,B) 皆 < gap(A,B) ≤ D → 必經 C 傳遞連通，刪 A–B 不損失連通。

**甲 A1–A8**（全採用）：A1 只量 `outer.contour`；A2 保留開頭 `union_ex`（島 disjoint）；A3 g→0 正規化 guard 改用局部法線；A4 微小島依尺寸 clamp 橋寬/penetration；A5 用 `get_waffle_offset(cfg)` 實際值（非寫死 1.6，round-join 為近似）；A6 確定性（pair 依 (i,j) 排序、tie-break 島索引、橋幾何有序聯集）；A7 舊 `return` bug 於改寫消失；A8 around-object 與 below-object 兩條 pad 路徑都驗。

⭐ **路線乙「逆向門檻折入」（效能關鍵）**：不實際把每島 offset waffle 再量測，而是把 waffle 外擴折進門檻——於 raw contour 上量最短距離 `g_raw`，比較 `g_raw ≤ raw_thresh`，其中
`raw_thresh = scaled(raft_gap_threshold) + 2·get_waffle_offset(cfg)`。
等價於 `g_finished = g_raw − 2·waffle_offset ≤ raft_gap_threshold`，語意對應肉眼成品間隙，且每島省一次昂貴 offset。
物理下限：`g_fin ≤ 2·waffle_offset`（預設 ~3.2mm）的近距島群因 `g_fin ≤ 0 ≤ D` 自動落入合併，一律主動畫受控橋（不放任 waffle-kiss 產生 sliver）。

**演算法管線**（僅 split=true）：`union_ex` → 島≤1 return → broad(`BoxIndex`, bbox 外擴 raw_thresh) → narrow(`AABBTreeLines::LinesDistancer` 雙向 vertex→edge 取 g_raw 與最近點對) → 接受 g_raw≤raw_thresh → 跨島 contour 濾除 → 生成最近點對受控橋(含 A3/A4) → `union_ex(islands+bridges)` → 接回 waffle 流程。

## (3) 檔案位置

**計畫檔（含完整偽代碼與檔案清單）**：`C:\Users\kwshi\.claude\plans\3d-raft-hidden-newell.md`

**四大 Artifact**（capability 名：`sla-pad-split-rafts`）：
- `openspec/changes/sla-split-rafts/proposal.md`
- `openspec/changes/sla-split-rafts/design.md`（D-1～D-7 決策 + rationale + 已否決替代方案）
- `openspec/changes/sla-split-rafts/specs/sla-pad-split-rafts/spec.md`（9 條 ADDED Requirements + scenarios）
- `openspec/changes/sla-split-rafts/tasks.md`（6 微階段，每階段末 🛑 手動驗證閘門）

**預計修改的原始碼**（皆尚未動）：
- 設定：`PrintConfig.hpp`(~L1728)、`PrintConfig.cpp`(~L7748)、`Preset.cpp`(~L1050)
- 演算法：`SLA/Pad.hpp`(PadConfig)、`SLAPrint.cpp`(make_pad_cfg ~L194)、`SLA/ConcaveHull.{hpp,cpp}`(新路徑，舊建構子不動)、`SLA/Pad.cpp`(~L292/L334 分派)
- GUI：`slic3r/GUI/Tab.cpp`(~L7166 選項行、~L7355 grey-out)
- 重用工具：`BoxIndex`(SpatIndex.hpp)、`LinesDistancer`(AABBTreeLines.hpp:327)、`get_waffle_offset`(Pad.cpp:139)、ClipperUtils
