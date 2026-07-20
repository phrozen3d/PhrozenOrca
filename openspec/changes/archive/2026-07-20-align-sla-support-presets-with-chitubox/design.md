## Context

SLA 支撐點 Gizmo 提供 Light / Middle / Heavy 三檔支撐尺寸快速預設，數值**硬編碼於 C++**（非 JSON）：

- `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp`
  - `struct SupportWeightPreset`（約 36–43 行）：目前 6 個欄位 `{ pillar_diameter, head_front_diameter, contact_diameter, base_diameter, base_height, head_width }`。
  - `static constexpr SupportWeightPreset k_weight_presets[3]`（約 45–49 行）：三檔聚合初始化。
  - `apply_weight_preset(SupportWeight)`（約 1842–1862 行）：按下 L/M/H 時把 preset 寫入 edited SLA print config 並觸發 Support tab 重載。
  - `on_set_state()`（約 1883–1892 行）：開啟 Gizmo 時以 `support_pillar_diameter` 對三檔柱徑做精確比對（`1e-4` 容差）決定高亮，無匹配則不選取。

參數在 UI 的顯示：Phrozen Support 分頁（`Tab.cpp` 約 7147–7161）顯示 contact_diameter / head_penetration / head_front_diameter / head_back_diameter / segment_length / pillar_diameter / base_diameter / base_height，**不顯示 `support_head_width`**。

幾何管線（`SLAPrint.cpp` 約 100–131，Default 支撐路徑）：`scfg.head_width_mm = c.support_segment_length`，即支撐頭錐段的軸向長度由 `support_segment_length` 驅動；`support_head_width` 在 Default 路徑**完全未被讀取**（僅 `branchingsupport_head_width` 供分枝支撐使用）。

目前現況缺口：`apply_weight_preset` 只寫入柱徑、前球徑、接觸球徑、底座徑/高、head_width、以及 `head_back = pillar`；**未寫入** `support_head_penetration` 與 `support_segment_length` → 這兩項在三檔間為固定值（來自 profile JSON）。

Profile 繼承鏈：`sla_print_common.json` → 6 個實體 process（`Speed Plus - Black@…`×3、`Tough ABS-like+@…`×3，皆 `inherits: sla_print_common`）。其中 `support_pillar_diameter` 在 common 與 6 子檔各自出現（值皆 `"1"`）；`support_segment_length`、`support_head_back_diameter` 僅在 common 出現。

## Goals / Non-Goals

**Goals:**
- 將 L/M/H 三檔的六項支撐尺寸（接觸直徑、接觸深度、上端直徑、較低直徑、段長度、支撐柱直徑）對齊 CHITUBOX。
- 讓「接觸深度」與「段長度」由固定值改為隨檔位變動。
- 載入 Phrozen SLA 系統預設時，Gizmo 反查以 Middle 高亮，且 Support 分頁顯示值與 Middle 一致。
- 既有使用者透過 profile 版號 bump 也能收到更新後的系統預設。

**Non-Goals:**
- **不調整 `support_head_width`**：該欄位不顯示於 Support 分頁、且 Default 支撐幾何實際讀 `support_segment_length`，對預設支撐無幾何作用。`apply_weight_preset` 對它的寫入視為無害 no-op，維持現狀，避免牽動序列化與分枝支撐。
- 不將 `support_base_diameter` / `support_base_height` 納入 CHITUBOX 對照；兩者仍隨 L/M/H 變動，沿用現有值（2/3/4、0.5/1/1.5）。
- 不修改 `PrintConfig.cpp` 硬編碼預設（由 profile JSON 覆蓋，且與既有 CHITUBOX 對齊變更慣例一致）。
- 不觸及 `branchingsupport_*` 分枝支撐系列。
- 不對既有使用者自訂 preset 做值遷移。

## Decisions

### D1: 擴充 `SupportWeightPreset` struct，新增 `head_penetration` 與 `segment_length` 兩欄位
以最小侵入方式把兩個原本固定的參數納入分檔。struct 為純聚合體（無建構子），`.hpp` 僅引用 `enum SupportWeight`、不依賴 struct 欄位順序，故影響侷限於此 `.cpp`。

**採 CHITUBOX 值（8 欄，欄位順序沿用現有再於尾端追加兩欄）：**

| 檔位 | pillar | head_front | contact | base_dia | base_h | head_width | head_penetration | segment_length |
|---|---|---|---|---|---|---|---|---|
| Light | 0.8 | 0.3 | 0.5 | 2.0 | 0.5 | 0.5 | 0.3 | 2.0 |
| Middle | 1.2 | 0.4 | 0.8 | 3.0 | 1.0 | 1.0 | 0.4 | 2.0 |
| Heavy | 1.5 | 0.6 | 1.0 | 4.0 | 1.5 | 1.5 | 0.6 | 3.0 |

**⚠️ 初始化陷阱**：C++ 聚合初始化下，struct 新增欄位但初始化列表未補值時，缺值欄位會被**靜默補 0、不報編譯錯誤**。因此三列 `k_weight_presets` 必須同步補齊 8 個值，避免 penetration/segment 變成 0。

### D2: `apply_weight_preset` 新增兩行 `cfg.set()`
在既有寫入區塊補上：
- `cfg.set("support_head_penetration", (double)p.head_penetration, true);`
- `cfg.set("support_segment_length",   (double)p.segment_length,   true);`

`support_head_back_diameter` 維持既有寫法 `= p.pillar_diameter`：因 CHITUBOX 的較低直徑與柱徑完全相同（0.8/1.2/1.5），無需另設欄位。

### D3: 反查高亮邏輯不改，靠 profile 對齊
`on_set_state()` 僅以 `support_pillar_diameter` 反查。三檔新柱徑 0.8/1.2/1.5 互異、無碰撞，故反查邏輯本身不需改動；正確高亮的前提是**載入的系統預設柱徑等於某一檔**。因此把 profile 初始柱徑對齊 Middle（1.2）。

### D4: Profile JSON 繼承鏈變更（對齊 Middle）
- `sla_print_common.json`：`support_pillar_diameter` `1 → 1.2`、`support_segment_length` `3 → 2`、`support_head_back_diameter` `1 → 1.2`。（contact 0.8、penetration 0.4、head_front 0.4 已等於 Middle，維持。）
- 6 個子 process：各自覆寫的 `support_pillar_diameter` `"1" → "1.2"`（`segment_length`、`head_back` 僅存在於 common，由繼承取得，不需在子檔改）。

### D5: Vendor 版號 patch bump 觸發既有使用者更新
`PhrozenSLA.json` `version` `01.00.05 → 01.00.06`。

依 `PresetUpdater.cpp`（約 1298–1299）更新條件為：
`version_match = (maj==maj && min==min)` 且 `installed < bundled`。
- patch bump 維持 maj.min 不變 → `version_match=true` → 觸發更新、覆寫 vendor 資料夾。
- **必須維持 `01.00.0X` 格式、只動 patch**；若升 minor/major（如 `01.01.00`），`version_match=false`，自動更新將**靜默不觸發**。
- process preset 內的 `version` 欄非更新觸發條件，可選擇性同步（良好實務，非必要）。

## Risks / Trade-offs

- **聚合初始化靜默補 0**（D1）：最高風險點，靠「三列補滿 8 欄」與建置後手動驗證三檔數值化解。
- **版號格式誤用**（D5）：若誤升 minor/major，既有使用者收不到新 profile；以 design 明列 patch-only 約束與程式碼行號佐證化解。
- **既有自訂 preset 不遷移**：使用者若已建立自訂柱徑（非 0.8/1.2/1.5）之 preset，開 Gizmo 時三檔皆不高亮（`weight_int=-1`），屬預期行為，非缺陷。
- **base 參數仍隨檔位變動但非 CHITUBOX 值**：屬非目標，可能造成「六項對齊、底座沿用舊值」的混合狀態；已於 Non-Goals 明示，接受此取捨。
- **head_width 保留為 no-op**：保留可能造成日後閱讀困惑；取捨為維持現狀以避免牽動序列化與分枝支撐，並於 Non-Goals 記錄。