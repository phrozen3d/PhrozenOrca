## Context

`sla-support-weight`（commit `0ac67bee7`）已實作三檔手動支撐重量選擇，在 `filter()` 階段對每個 head 的 `r_back_mm` 套用縮放（Light=0.5x、Medium=1.0x、Heavy=2.0x）。後續所有幾何運算（`create_ground_pillar`、`connect_to_ground`、碰撞偵測）均正確讀取 `head.r_back_mm` 或 `pillar.r_start`。

唯一殘留的問題點在 `interconnect()` 函式（`SupportTreeBuildsteps.cpp:321`）：

```cpp
if (pillar_dist < 2 * m_cfg.head_back_radius_mm || ...)
```

此條件以**全域預設半徑**（0.5mm）作為最小安全間距的基準，而非兩柱的實際縮放半徑。當兩根 Heavy 柱（r=1.0mm）僅相距 1.5mm 時，條件 `1.5 < 2*0.5=1.0` 為 false，系統通過並嘗試建橋；橋段半徑 `min(1.0, 1.0)=1.0mm` 卻無法物理容納於 1.5mm 間距中，造成輸出模型交錯。

次要問題：`interconnect_pillars()` 為孤立柱尋找補強柱時，起始搜尋圓半徑固定為 `2 * base_radius_mm`，Heavy 柱的補強柱可能被放置在與本柱過近的位置。

邊緣案例：`filter()` 的去重 cluster（D_SP=0.1mm）選取 `a.front()` 時不考慮 weight，在兩點極近的情況下可能丟棄較高 weight 的點。

## Goals / Non-Goals

**Goals:**
- 修正 `interconnect()` 的最小間距檢查，改用兩柱實際半徑之和
- 修正 `interconnect_pillars()` 補強柱搜尋半徑以反映目標柱的縮放半徑
- 修正 `filter()` 去重 cluster 保留邏輯，優先保留 weight 較高的點

**Non-Goals:**
- 不修改 `filter()` 中 `bridge_mesh_distance` / `pinhead_mesh_intersect` 的安全距離計算（已依 r 等比縮放，行為正確）
- 不修改 GUI、序列化、或 SupportTreeConfig 參數
- 不處理自動生成支撐點（weight 欄位對其無效，行為無變化）
- 不修改 FDM 支撐程式碼

## Decisions

### Decision 1：interconnect() 最小間距由固定值改為兩柱半徑之和

**現有**：`pillar_dist < 2 * m_cfg.head_back_radius_mm`

**修改為**：`pillar_dist < pillar.r_start + nextpillar.r_start`

**理由**：最小安全間距的語意是「橋段能無重疊地穿過兩柱之間」。橋段半徑取 `min(pillar.r_start, nextpillar.r_start)`，因此兩柱邊緣的最小淨空需至少為 `pillar.r_start + nextpillar.r_start`（即中心到中心距離 ≥ 兩半徑之和）。舊的固定值只在 Medium（1.0x）下正確，Heavy 下過鬆、Light 下過嚴。

**備選方案考慮**：使用 `2 * max(pillar.r_start, nextpillar.r_start)`——此值更保守，但對 Light+Heavy 混搭情況過嚴，不採用。

### Decision 2：interconnect_pillars() 補強柱搜尋半徑

**現有**：`double r = 2 * m_cfg.base_radius_mm`

**修改為**：`double r = std::max(2 * m_cfg.base_radius_mm, 2 * pillar().r_start)`

**理由**：補強柱的起始搜尋圓是「在本柱旁邊找一個落地點」，間距需至少讓補強柱（同樣是 `r_start` 半徑）不與本柱重疊。`2 * pillar().r_start` 是兩柱中心間距的最小安全值。取 max 確保即使 r_start < base_radius_mm（Light 柱）時搜尋半徑不縮得過小。

### Decision 3：filter() 去重 cluster 保留 weight 最高的點

**現有**：
```cpp
for(auto& a : aliases) {
    filtered_indices.emplace_back(a.front());
}
```

**修改為**：在 cluster `a` 中找出 `m_support_pts[idx].weight` 最大的那個 index，若有並列則取 `a.front()`。

**理由**：D_SP=0.1mm 的 cluster 只去除真正重疊的點。在 manual_add 點中，使用者有可能（雖然罕見）在完全相同位置放置 Light 再放置 Heavy——應保留 Heavy。保留邏輯僅在 `a.size() > 1` 時生效，對大多數情況無效能影響。

## Risks / Trade-offs

- **[互連性下降]** Heavy 柱間距增大後，`interconnect_pillars()` 的橋段連接數可能減少，孤立的 Heavy 柱需要更多補強柱。→ 補強柱邏輯本身會自動處理（`needpillars` 計算），且補強柱搜尋半徑已同步修正。

- **[Light 柱互連過嚴]** `pillar.r_start + nextpillar.r_start` 對兩根 Light 柱（各 0.25mm）給出最小間距 0.5mm，等同舊值，行為不變。

- **[既有模型行為差異]** 已保存的含 Heavy 支撐點的 3mf 專案，重新切片後橋接結構可能因最小間距更嚴而改變。這是**預期的修正行為**，不是 regression。

## Migration Plan

純演算法修改，無資料遷移需求。所有改動均在 `SupportTreeBuildsteps.cpp` 內，不影響序列化格式或公開 API。重新建置後舊的 3mf 檔案可正常開啟，重新切片即可獲得修正後的支撐結構。
