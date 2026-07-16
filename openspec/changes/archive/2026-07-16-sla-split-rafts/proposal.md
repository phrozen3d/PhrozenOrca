## Why

目前 Resin（SLA）底筏（pad）的合併判準使用「質心對質心距離」，門檻為 `get_merge_distance = 2·(1.8·pad_wall_thickness) + pad_max_merge_distance`（預設 `2·(1.8·2.0)+50 = 57.2mm`）。此判準有兩個缺陷：(1) 以質心距離而非真實輪廓間隙判斷，尺寸盲目；(2) 隱藏 `+7.2mm` 使 UI 顯示的「50」實際為 57.2。實測兩個 10mm cube（中心相距 33mm）本應分開，卻因 33 < 57.2 被畫橋黏成一整片底筏。使用者需要能讓「肉眼可見間隙夠大」的島群各自獨立成筏，以降低 MSLA 離型張力、省料、便於取件。

## What Changes

- 新增可選功能：以**真實成品間隙（edge-to-edge gap，量在 waffle/brim 外擴後的 footprint）**取代質心距離作為底筏合併判準。
- 新增布林開關 `pad_split_rafts`（**預設 `true`（1）**）。預設即啟用新的 edge-gap 合併路徑；**設為 `false` 時完全走現行舊邏輯、行為零變動**（可隨時退回原廠質心邏輯）。
- 新增參數 `raft_gap_threshold`（float，預設 `5.0mm`）：語意為「兩片底筏成品之間肉眼可見的間隙」，超過此值的島群不合併、各自獨立成筏。
- 新增參數 `raft_bridge_width`（float，預設 `2.0mm`）：合併島群時所畫「受控連接橋」的寬度，確保足夠機械強度對抗離型張力、避免橋於剝離瞬間斷裂。
- 合併演算法改為：broad-phase 包圍盒粗篩 → narrow-phase 精算最短間隙與最近點對 → 接受 `g_fin ≤ raft_gap_threshold` 的 pair → 濾除「穿越其他島輪廓」的多餘跨島橋 → 以最近點對生成帶內插（penetration）的受控短橋 → 聯集。允許自然閉環（一圈**離散島群**接合成環狀底筏、中央留空），同時消滅蜘蛛網式跨橋。
- **適用範圍**：本變更僅處理**複數個獨立島群之間**的 edge-gap 合併與閉環。**單一模型自身的內孔不在範圍內**——其 holes 在 `Pad.cpp` 以 `ep.contour` 取用藍圖時即被上游剝除，底筏恆為外輪廓的實心 waffle；此為原廠既有行為，與 `pad_split_rafts` 無關（詳見 design.md Risks 的已知原廠邊界）。
- 物理下限：兩片底筏成品間隙小於 `2·waffle_offset`（預設約 3.2mm）時註定於外擴步驟相觸，演算法一律主動以受控橋接合，避免不受控的 waffle-kiss 產生切片碎屑（sliver）。
- ⚠ **BREAKING（行為變更）**：`pad_split_rafts` **預設為 `true`**，故既有 SLA profile 升級後**切片結果會改變**——原本因質心門檻被黏成一片的島群，若成品間隙 > `raft_gap_threshold`（預設 5mm）將改為各自獨立成筏。此為本變更刻意採用的新預設行為。需要原廠行為者，於後台 config 設 `pad_split_rafts=0` 即可完全退回（同一 build 內可切換、零風險）。

## Capabilities

### New Capabilities
- `sla-pad-split-rafts`: Resin 底筏依「真實成品間隙」判斷是否合併，讓分得夠開的島群各自獨立成筏，並以受控連接橋處理無法分離的近距島群；透過 `pad_split_rafts` 開關、`raft_gap_threshold`、`raft_bridge_width` 參數控制。

### Modified Capabilities
<!-- 無：現有 specs 中沒有描述 pad 合併判準的 capability，本變更未修改任何既有 spec 的需求。惟因 `pad_split_rafts` 預設為 true，實際切片行為對既有 profile 屬 BREAKING（見 Impact）。 -->

## Impact

- **設定層**：`SLAPrintObjectConfig`（`src/libslic3r/PrintConfig.hpp`、`PrintConfig.cpp`）新增三個選項；`Preset.cpp` SLA print 選項清單註冊新 key。
- **核心演算法層**：`SLA/Pad.hpp`（`PadConfig` 新增欄位）、`SLAPrint.cpp`（`make_pad_cfg` 映射）、`SLA/ConcaveHull.hpp`/`ConcaveHull.cpp`（新增 edge-gap 合併路徑，舊建構子不動）、`SLA/Pad.cpp`（依 `pad_split_rafts` 分派新/舊路徑）。
- **GUI 層**：三個參數皆為 **hidden setting**（`def` 不設 `category`），不在 SLA 設定頁、設定搜尋、物件「加入設定」齒輪選單顯示；進階使用者僅能透過使用者 preset `.json`（`%APPDATA%\PhrozenOrca\user\...\sla_print\*.json`）或專案 `.3mf` 內嵌 config 直接編輯。**不新增任何 UI 元件或 localization 字串**。隱藏機制沿用 `pad_object_connector_*` 先例（空 category → 被 `is_improper_category` 略過）。
- **重用既有工具**（不重造幾何輪子）：`BoxIndex`（SpatIndex）、`AABBTreeLines::LinesDistancer`、`get_waffle_offset`、ClipperUtils。
- **相容性**：**預設開啟（`true`）→ 既有 profile 的切片輸出會改變（BREAKING）**；設 `pad_split_rafts=0` 可完全退回原廠質心邏輯。舊路徑程式碼與 `get_merge_distance` 全數保留，退回路徑零風險。
- **測試**：`tests/sla_print/` 既有 pad 測試須維持全綠；新增 edge-gap 合併之回歸與單元測試。
