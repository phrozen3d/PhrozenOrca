## Context

PhrozenOrca 的 SLA 支撐 UI 參數（`pillar_diameter`、`support_boss_height` 等）登記在 `PrintConfig`（FDM 用途的類別，行 1550~1570）。`SLAPrint::apply()` 透過 `m_default_object_config.diff(config)` 只取 `SLAPrintObjectConfig` 已知的 key，所以這批 key 永遠看不到——使用者的調整從未傳入演算法。

資料流（修正後）：
```
TabSLAPrint::m_config (= sla_prints edited preset config)
    → SLAPrint::apply()  [m_default_object_config.diff(config)]
    → SLAPrintObject::m_config
    → make_support_cfg(po.m_config)
    → SupportTreeConfig
    → SupportTreeBuildsteps / SupportTreeMesher
```

## Goals / Non-Goals

**Goals:**
- 讓 Support 設定頁的所有 SLA 支撐參數確實影響演算法
- 新增 Contact Sphere 模式（與 pinhead 前球同心、半徑更大）
- 讓 Light / Medium / Heavy 切換成為全域 print config preset，觸發自動重切
- 移除演算法中讀取 `SupportPoint::weight` 的倍率與叢集排序邏輯

**Non-Goals:**
- 刪除 `PrintConfig` 中的舊 key（保留以防 preset 讀取爆炸）
- 每個支撐點獨立使用不同 preset（目前改為全域 config）
- Contact Sphere 下的動態顯示/隱藏 `support_contact_diameter`（可後續加）
- 舊 preset 值遷移（舊 key 從未影響演算法，靜默遺棄可接受）

## Decisions

### D1：`contact_sphere_radius_mm = 0.0` 作為「不加球」sentinel，不引入額外 enum

`SupportTreeConfig` 新增 `double contact_sphere_radius_mm = 0.0`。
`make_support_cfg()` 在 `support_contact_type == spSphere` 時才賦非零值；
`get_mesh<Head>` 以 `r_contact_mm > r_pin_mm` 判斷是否疊加球體。

替代方案：在 `SupportTreeConfig` 加 `SLAContactType` enum → 需要額外序列化/比較邏輯，沒有實際好處。

### D2：`r_contact_mm` 在 `filterfn` lambda 中賦值，而非 Head 建立時

Head 建立時 `r_back_mm` 尚為 NaN（尚未最終化），`filterfn`（`SupportTreeBuildsteps.cpp` ~line 774）是 `r_back_mm` 被正式寫入的地方，因此 `r_contact_mm = m_cfg.contact_sphere_radius_mm` 在此同步賦值。

### D3：`apply_weight_preset()` 同時更新 `m_new_point_head_diameter`

Gizmo 內建的 head diameter slider 控制下一個放置點的前球半徑。
切換 preset 時若不同步 `m_new_point_head_diameter`，下一個手動放置點仍用舊值。

### D4：L/M/H 切換不加 undo/redo

preset 切換屬於全域 print config 異動，與 support point 編輯歷史是不同層次，
混入 undo stack 會讓撤銷語意混亂。接受此限制。

### D5：Radio 初始化用 `pillar_diameter` 精確比對；無匹配則 fallback Medium

開啟 Gizmo 時讀取 `support_pillar_diameter`，與三組 preset 的 `pillar_diameter` 做 exact match（容差 1e-4）。
使用者若手動修改過柱徑，三組都不匹配，radio 不亮（`weight_int = -1`）；
但 `m_new_point_weight` fallback 為 Medium，下一次放置仍有合理預設。

### D6：保留每點的 `head_front_radius`（不改為全域）

`SupportPoint::head_front_radius` 記錄放置時的前球半徑，演算法用於建構每個點的 pinhead。
移除它需要大幅重構叢集和 pillar 建構邏輯，不在本次範圍。
`SupportPoint::weight` 欄位保留（序列化相容）但演算法不再讀取。

### D7：`SupportTreeBuildsteps.cpp` 中 weight 讀取方式重構

原始設計（移除所有 weight 讀取）在測試後調整為：手動點保留 per-weight 半徑選取，自動點改用全域 config。

| 位置 | 舊行為 | 實際修改後 |
|---|---|---|
| ~line 784–795 | `back_r` 依 weight 套倍率（×0.5/×1.0/×2.0） | 手動點依 `weight` 選 `light/medium/heavy_back_radius_mm`（絕對值，非倍率）；自動點用 `m_cfg.head_back_radius_mm` |
| ~line 658–665 | clustering 保留 weight 最重的點 | 移除比較，保留第一個點（迭代順序） |
| ~lines 908, 942, 968 | Light weight 跳過柱徑加寬 | 手動點：Heavy 才加寬，Light/Medium 跳過；自動點：一律加寬 |

**設計理由**：手動放置時使用者明確選擇了 L/M/H，演算法應尊重此選擇以生成對應粗細的支撐柱。若改為全域 config，切換 L/M/H 後放置的所有手動點柱徑會一致，無法在同一物件上混用不同粗細的手動點。

`SupportWeight` 語意從「倍率標記」改為「每點獨立的 preset 選擇」。

## Risks / Trade-offs

- **舊 preset 值靜默遺棄**：使用者過去在 UI 設定的數值（因 key 不符而從未生效），升級後歸零為新預設值。可接受，因為舊值本來就沒效果。
- **clustering 改為保留第一個點**：叢集中不再優先保留「最重」的點，順序依賴於支撐點迭代順序。理論上差異微小，實際需驗證切片結果。
- **柱徑加寬現在對所有點套用**：移除 Light weight 豁免後，所有點的柱徑加寬邏輯一致，可能略增材料用量。此為預期行為（Light preset 本身柱徑已設定較細）。
- **全域 preset 無法混用**：同一物件無法同時有 Light 和 Heavy 的手動點。使用者若有混用需求，只能手動逐點調整 `head_front_radius`（Gizmo slider）。

## Migration Plan

1. 編譯通過後，舊的 `.orca_slice` 或 `.3mf` 專案檔中若含有 `pillar_diameter` 等舊 key，`diff()` 仍會忽略，使用新欄位的預設值——行為一致，無需版本遷移。
2. 無資料庫或外部服務異動，不需 rollback 機制。

## Open Questions

- Light / Medium / Heavy 三組 preset 的數值尚需與硬體團隊確認（目前計畫草稿值：Light=0.6/0.3/0.4mm、Medium=1.0/0.4/0.6mm、Heavy=1.5/0.6/0.8mm）。
- Contact Sphere 的接觸直徑預設值是否需要依機型提供不同建議？
