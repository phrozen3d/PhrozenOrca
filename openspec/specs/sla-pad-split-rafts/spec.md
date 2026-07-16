# Capability: sla-pad-split-rafts

## Purpose

Resin（SLA）底筏依「**真實成品間隙**」而非質心距離判斷是否合併，讓分得夠開的島群各自獨立成筏（降低 MSLA 離型張力、省料、便於取件），並以受控連接橋處理無法分離的近距島群（避免不受控細頸產生切片碎屑）。

取代原本的質心距離判準——其門檻 `get_merge_distance = 2·(1.8·pad_wall_thickness) + pad_max_merge_distance`（預設 57.2mm）尺寸盲目且隱藏 `+7.2mm`，導致中心相距 33mm 的兩個 10mm cube 被錯誤黏成一整片底筏。

透過 `pad_split_rafts`（預設 `true`）、`raft_gap_threshold`（5.0mm）、`raft_bridge_width`（2.0mm）三個 **hidden setting** 控制；舊質心路徑完整保留，設 `pad_split_rafts=0` 即可完全退回原廠行為。

設計決策與已知邊界詳見封存的設計文件：`openspec/changes/archive/2026-07-16-sla-split-rafts/design.md`。

## Requirements

### Requirement: 分區底筏開關與可退回性
系統 SHALL 提供布林設定 `pad_split_rafts`（**預設 `true`**）。當 `pad_split_rafts` 為 `false` 時，底筏合併 MUST 完全沿用既有質心距離邏輯，產生與變更前逐位元一致的 pad 幾何。

> ⚠ 預設為 `true` 屬**刻意的行為變更（BREAKING）**：既有 SLA profile 升級後切片結果會改變。風險隔離由「可即時退回」提供，而非「預設關閉」。退回方案應於 Release Notes 主動告知。

#### Scenario: 預設啟用分區底筏
- **WHEN** 既有 SLA profile 未指定 `pad_split_rafts` 而直接切片
- **THEN** 取預設值 `true`，套用 edge-gap 分區底筏行為
- **AND** 不需任何 profile 遷移或版本相容處理

#### Scenario: 明確關閉時維持原廠行為
- **WHEN** 任一既有 SLA profile 以 `pad_split_rafts=false` 切片
- **THEN** 產生的 pad 幾何與本變更導入前完全相同

#### Scenario: 同一 build 內即時回退
- **WHEN** 使用者將 `pad_split_rafts` 由 `true` 切回 `false` 並重新切片
- **THEN** 底筏立即回到原廠質心邏輯的結果

### Requirement: 以成品間隙判斷底筏分離
當 `pad_split_rafts` 為 `true` 時，系統 SHALL 以島輪廓間的**真實最短間隙**（而非質心距離）決定是否合併；兩島群的**成品 footprint 間隙**大於 `raft_gap_threshold` 時 MUST 各自獨立成筏、彼此不相連。

#### Scenario: 分得夠開的兩立方體各自成筏
- **WHEN** 兩個 10mm cube 中心相距 33mm、`pad_split_rafts=true`、`raft_gap_threshold=5.0`
- **THEN** 產生兩片互不相連的獨立底筏

#### Scenario: 判準與物件尺寸無關
- **WHEN** 兩個大型平底物件的成品 footprint 間隙大於 `raft_gap_threshold`
- **THEN** 系統判定不合併，即使其質心距離很大或很小

### Requirement: 門檻語意對應肉眼可見成品間隙
`raft_gap_threshold`（float，預設 `5.0mm`）SHALL 代表兩片底筏**成品之間**肉眼可見的間隙；合併判斷 MUST 將 waffle/brim 外擴量（`2·waffle_offset`）納入，等價於比較 `g_finished = g_contour − 2·waffle_offset` 與 `raft_gap_threshold`。

**物理下限夾持**：當設定之 `raft_gap_threshold` 低於物理下限 `2·waffle_offset`（約 `2·brim_size`）時，實際合併判定門檻將自動夾持於該物理下限，以避免不受控細頸與碎屑。故有效門檻為 `max(raft_gap_threshold, 2·waffle_offset)`；此下限為 waffle closing 的先天幾何限制，無法藉調低參數突破。

#### Scenario: 門檻對應成品而非原始輪廓
- **WHEN** 兩島原始輪廓間隙為 `raft_gap_threshold + 2·waffle_offset`，且 `raft_gap_threshold ≥ 2·waffle_offset`
- **THEN** 系統判定其成品間隙恰為 `raft_gap_threshold`、位於合併臨界

#### Scenario: 門檻低於物理下限時夾持於下限
- **WHEN** 使用者將 `raft_gap_threshold` 設為低於 `2·waffle_offset`（預設 brim `1.6mm` 時約 `3.2mm`）
- **THEN** 實際合併判定門檻夾持於 `2·waffle_offset`，而非所設之較小值
- **AND** 凡 waffle 外擴會自然焊接的島對，一律先以受控橋接合，不產生不受控細頸或碎屑

### Requirement: 近距島群以受控橋主動接合
當兩島群的成品間隙小於等於 `raft_gap_threshold`（含小於 `2·waffle_offset` 的物理下限情形），系統 SHALL 主動以寬度 `raft_bridge_width` 的受控連接橋接合，MUST NOT 放任其於 waffle 外擴步驟自然「接吻」而產生不受控的細頸或切片碎屑（sliver）。

#### Scenario: 物理下限以下主動畫橋
- **WHEN** 兩島成品間隙小於 `2·waffle_offset`
- **THEN** 系統以 `raft_bridge_width` 受控橋接合成單一連通底筏
- **AND** 連接處 neck 寬度不小於 `raft_bridge_width`

#### Scenario: 橋端內插避免碎屑
- **WHEN** 生成連接橋
- **THEN** 橋兩端各內插（penetration）進入島體使其實質重疊
- **AND** 聯集後不產生僅頂點相觸的狹縫碎屑

### Requirement: 連接橋強度可設定
`raft_bridge_width`（float，預設 `2.0mm`）SHALL 控制受控連接橋寬度，以提供對抗 MSLA 離型張力的機械強度。針對尺寸過小的島，系統 MUST 依島尺寸動態裁切橋寬與內插量，避免橋吃穿該島。

#### Scenario: 微小島夾制橋寬
- **WHEN** 一島的尺寸小於 `raft_bridge_width`
- **THEN** 該橋的寬度與內插量被裁切至不超過島尺寸

### Requirement: 允許自然閉環並消除跨島冗橋
系統 SHALL 接受所有成品間隙符合門檻的相鄰島 pair（允許形成自然閉環的**橋拓樸**，不以 MST 砍邊），同時 MUST 濾除「最近點對連接橋線段穿越其他島輪廓」的冗餘跨島橋；濾除判斷 MUST 以線段對島**輪廓（contour）**的精確相交為準，AABB 僅作粗篩。

> 適用範圍：本需求描述的是**複數個獨立島群之間的橋拓樸**。不涵蓋「孔洞是否留空」——單一模型自身的內孔、以及閉環島群圍出的內孔，皆會因原廠僅保留 `.contour` 的機制被實心化（已知原廠邊界一、二，詳見封存的 `design.md` Risks）。

#### Scenario: 環狀排列的離散島群僅相鄰接合、無弦橋
- **WHEN** 一圈離散島（例如 8 個 8mm Cube 均勻排在直徑 40mm 的圓周上）相鄰成品間隙皆小於門檻，而跨徑間隙大於門檻
- **THEN** 僅相鄰島之間生成連接橋，形成閉環拓樸並接合為單一連通底筏
- **AND** **不出現橫跨中央的蜘蛛網式跨島橋**，且切片正常完成不崩潰
- **AND** 中央孔洞被填實**不視為本需求之失敗**（屬 `.contour` 機制的既有系統行為）

#### Scenario: 成排島僅相鄰接合
- **WHEN** 多個小島排成一列、相鄰間隙小於門檻
- **THEN** 僅相鄰島之間生成連接橋，穿越中間島的跨接橋被濾除

### Requirement: 支撐島與模型島一視同仁
進入合併演算法前所有相互重疊的 support 與 model 底面輪廓 SHALL 已先行 `union_ex` 合併為「島群」；合併判斷 MUST 對所有島群使用單一 `raft_gap_threshold`，不區分 support 或 model 來源。

#### Scenario: 混合島群統一門檻
- **WHEN** 島群由模型本體與其週邊緊密支撐組成
- **THEN** 其與其他島群的合併判斷一律套用同一 `raft_gap_threshold`

### Requirement: 切片確定性
分區底筏演算法 SHALL 產生確定性（可重現）結果；候選 pair MUST 依島索引 `(i,j)` 排序、等距 tie-break 以島索引決定、連接橋幾何依固定順序聯集，使並行運算不影響輸出。

#### Scenario: 重複切片結果一致
- **WHEN** 同一模型與設定重複切片多次
- **THEN** 每次產生完全相同的底筏幾何

### Requirement: 參數僅後台可編輯、GUI 隱藏
三個參數 `pad_split_rafts`、`raft_gap_threshold`、`raft_bridge_width` SHALL 為 hidden setting：MUST 保留於設定宣告、Preset 選項清單註冊與預設值，讓進階使用者能透過使用者 preset `.json`（`%APPDATA%\PhrozenOrca\user\...\sla_print\*.json`）或專案 `.3mf` 內嵌 config 編輯並持久化；但 MUST NOT 出現於任何 GUI 面（SLA 設定頁、設定搜尋、物件「加入設定」齒輪選單）。

#### Scenario: 三個參數不出現於任何 GUI 面
- **WHEN** 使用者於 GUI 檢視 SLA 設定頁、使用設定搜尋、或開啟物件「加入設定」齒輪選單
- **THEN** `pad_split_rafts`、`raft_gap_threshold`、`raft_bridge_width` 皆不顯示、無法由 GUI 編輯

#### Scenario: 透過後台 config 編輯生效並可存回
- **WHEN** 進階使用者於使用者 preset `.json`（`%APPDATA%\PhrozenOrca\user\...\sla_print\*.json`）或專案 `.3mf` 內嵌 config 設定 `pad_split_rafts=1`、`raft_gap_threshold=5`、`raft_bridge_width=2`
- **THEN** 載入後數值生效並被切片流程讀取
- **AND** 存回 preset `.json`／專案 `.3mf` 後數值保留（round-trip persist）
