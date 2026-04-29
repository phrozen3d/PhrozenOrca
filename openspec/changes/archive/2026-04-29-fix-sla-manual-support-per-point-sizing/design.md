## Context

目前 `apply_weight_preset()` 在使用者點擊 L/M/H 按鈕時，直接呼叫 `cfg.set(...)` 修改全域 SLA print preset 的 6 個參數，並觸發設定頁刷新與重切。後端 `SupportTreeBuildsteps::filter()` 在決定每個支撐點的 `head_back_radius`（柱體半徑）時，使用全域 `m_cfg.head_back_radius_mm`，所有點一律相同。另外，3MF 的支撐點文字格式（5 floats）不含柱體尺寸，導致存檔重開後各手動點的個別尺寸遺失。

## Goals / Non-Goals

**Goals:**
- 手動支撐點的柱體半徑在切片時依各點存儲的 `pillar_radius` 決定，而非全域 config
- L/M/H 按鈕點擊後支撐頁面（全域 cfg）立即更新為對應 preset 的參數值（保留現有 cfg.set 行為）
- 選取既有手動支撐點時，Gizmo 面板顯示該點存儲的 pillar_radius 值（不觸碰全域 cfg）
- 3MF 格式（version 2）持久化 pillar_radius，舊版（version 1）向後相容
- 自動生成支撐繼續使用全域 config，行為不變

**Non-Goals:**
- 不修改 SLA 支撐頭的其他幾何（contact_diameter、base_diameter 等）的 per-point 差異
- 不變更自動支撐的生成邏輯

## Decisions

### Decision 1：在 SupportPoint 存 pillar_radius，而非 SupportTreeConfig 查表

**選擇**：在 `SupportPoint` 新增 `float pillar_radius = 0.f` 直接存儲柱體半徑（mm）。`0.f` 代表「使用全域設定」（auto 點預設值）。

**捨棄**：在 `SupportTreeConfig` 新增 `light_back_radius_mm`、`medium_back_radius_mm`、`heavy_back_radius_mm` 三個欄位，算法依 `SupportPoint::weight` 查表取值。

**理由**：  
- 直接存儲實際 mm 值，幾何語意清晰，不依賴 enum 與查表的間接映射  
- 使用者的柱徑來自支撐設定頁的實際參數值（`support_pillar_diameter`），以此直接存入更忠實反映放置當下的設定  
- 未來若支援任意自訂柱徑（非 L/M/H 三檔），不需新增 enum 值或查表欄位，天然相容  
- 3MF 第 6 個 float 直接為 mm 值，可讀性高，不依賴 enum 順序不變

### Decision 2：3MF 格式從 version 1 升為 version 2，pillar_radius 存為第 6 個 float

**選擇**：3MF 文字格式每行由 5 floats 擴充為 6 floats（x, y, z, head_front_radius, type_f, pillar_radius_f），version 常數由 1 升為 2。

**捨棄**：透過 cereal 二進制存入 SupportPoint 序列化流（不修改文字格式）。

**理由**：  
- 文字格式才是跨版本 / 跨平台的標準交換格式；cereal 格式僅用於 Gizmo 的 undo/redo 快照  
- version 常數已有 v0 → v1 的 precedent，v2 只需在讀取端加一個 `if (version >= 2)` 分支，向後相容成本極低  
- pillar_radius_f 以實際 mm 值存入，讀取時直接賦值 `sp.pillar_radius`，無需 enum 映射或邊界檢查

### Decision 3：apply_weight_preset() 保留 cfg.set()，同時新增 m_new_point_pillar_diameter

**選擇**：保留 `apply_weight_preset()` 中的 `cfg.set(...)` 及 `CallAfter` tab 刷新邏輯（支撐頁面即時更新為 preset 值）。新增 `m_new_point_pillar_diameter = p.pillar_diameter;` 一行，供 Gizmo 面板顯示與選取點時使用。

**選取既有點的行為**：`select_point()` 讀取 `sp.pillar_radius * 2.f` 更新 `m_new_point_pillar_diameter`，**不呼叫** `cfg.set()`。支撐頁面維持上次 L/M/H 按鈕設定的全域值，Gizmo 面板另顯示該點存儲的尺寸。

**影響**：  
- L/M/H 切換仍更新支撐頁面全域設定（行為不變，使用者預期如此）  
- 每個手動點放置時存入 `pillar_radius`，切片時讀此值，不再受後續 L/M/H 切換影響  
- 選取點時 Gizmo 面板反映該點存儲的尺寸，支撐頁面保持獨立

### Decision 4：保留 SupportPoint::weight 僅供 UI 顯示，幾何不依賴 weight

`SupportPoint::weight`（Light/Medium/Heavy enum）保留在結構體中，供 Gizmo 面板 L/M/H 按鈕 highlight 使用（選取點時顯示該點對應的檔位）。幾何算法（`filter()`）完全不讀取 `weight`，只讀 `pillar_radius`。

3MF 格式第 6 個 float 存 `pillar_radius`（mm 值），不存 `weight`。cereal 序列化繼續存 `weight`，供 undo/redo 快照。

## Risks / Trade-offs

- **[Risk]** `filterfn` 的第三個參數語意是否與 `pillar_radius` 直接對應？→ 實作時確認 `head_back_radius_mm` 在 filter 中的用途（head back = pillar 起始半徑），若語意一致，`sp.pillar_radius` 可直接作為 `back_r` 傳入
- **[Trade-off]** 選取既有點時支撐頁面不跟著更新 → 使用者需注意 Gizmo 面板顯示的是「該點存儲的尺寸」，支撐頁面顯示的是「全域 preset 設定」，兩者可能不同；應在 Gizmo 面板加上清晰的標籤區分
- **[Trade-off]** version 1 舊 3MF 讀入時 `pillar_radius = 0.f`，切片使用全域設定，行為等同修改前，不做額外補救

## Migration Plan

1. 3MF version 1 檔案讀入：`pillar_radius` 設為 `0.f`，切片使用全域 `head_back_radius_mm`，結果與修改前相同
2. 3MF version 2 檔案需新版軟體才能完整讀入；舊版打開 v2 檔案時第 6 欄被忽略，等同 version 1 行為
3. 無需 DB migration 或部署步驟；純 client-side 變更

## Open Questions

（已無未解決問題）
