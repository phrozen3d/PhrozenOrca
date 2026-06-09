## Context

2026-03 已將 `support_critical_angle` 與 `support_bracing_angle` 拆分（overhang filter vs bridge slope）。目前 `support_bracing_angle` 仍映射至單一 `SupportTreeConfig::bridge_slope`，同時用於：

| 程式路徑 | 語意 |
|----------|------|
| `connect_to_nearpillar`, `connect_to_ground`, `create_ground_pillar`, `filterfn`, `SupportTreeUtils` | Top↔Middle（接觸頭↔主柱） |
| `connect_pillars` (`interconnect`) | Main↔Main（主柱 zigzag/cross 連接） |

Resin 支撐 UI（`TabSLAPrint`）分區為 Top / Main / Bottom / Raft Setting / Bridge。`angle_between_top_and_middle` 已出現在 PhrozenSLA profile JSON，但 `src/` 無對應 config 定義。

使用者決策：
- 預設 45°、上限 90°
- 手動支撐點不調整，沿用全域 preset
- 保留 `support_bracing_angle` 作為向後相容 alias
- 僅 Default Tree，不動 Branching

## Goals / Non-Goals

**Goals:**
- 將 `bridge_slope` 拆分為 `top_middle_slope` 與 `cross_slope`
- 新增 `angle_between_top_and_middle`、`cross_angle` config key 並完成 UI 佈局
- `support_bracing_angle` 讀取時 fallback，確保舊 profile 行為不變
- 角度參數變更觸發支撐樹重算

**Non-Goals:**
- Branching Tree（`branchingsupport_bracing_angle`）拆分
- 手動支撐點 per-point 角度 override（`SupportPoint::support_bracing_angle_deg` 維持現狀）
- 3MF 序列化格式變更
- `PrintConfig::support_bracing_angle`（LCD 光柵用，不同 C++ 類別）修改

## Decisions

### Config key 命名

- Top↔Middle：`angle_between_top_and_middle`（與現有 profile JSON 一致）
- Main↔Main：`cross_angle`（Bridge 區塊 UI 標籤 Cross Angle）

**替代方案**：`angle_between_middle_and_middle` — 更對稱但較長，且 profile 已用 `angle_between_top_and_middle`，採較短 `cross_angle`。

### `SupportTreeConfig` 欄位拆分

```cpp
double top_middle_slope = M_PI / 4;  // Top↔Middle
double cross_slope      = M_PI / 4;  // Main↔Main
```

保留 `bridge_slope` 為 deprecated alias（getter 或 inline 註解）可選；建議直接移除成員引用、全面改用新欄位，減少混淆。

引擎接線（`SLAPrint.cpp::make_support_cfg`，Default Tree only）：

```cpp
scfg.top_middle_slope = resolve_angle(c.angle_between_top_and_middle, c.support_bracing_angle) * PI / 180.0;
scfg.cross_slope      = resolve_angle(c.cross_angle, c.support_bracing_angle) * PI / 180.0;
```

`resolve_angle(new_key, fallback_key)`：若 `new_key` 存在且有效則用之，否則 fallback 至 `support_bracing_angle`（預設 45°）。

### 引擎使用對照

| 函式 | 使用欄位 |
|------|----------|
| `connect_to_nearpillar` | `top_middle_slope` |
| `connect_to_ground` / `create_ground_pillar` | `top_middle_slope`（含 per-point `support_bracing_angle_deg` override，維持現狀） |
| `filterfn` / `SupportTreeUtils` pinhead placement | `top_middle_slope` |
| `connect_pillars` / `interconnect` | `cross_slope` |

### UI 佈局（`Tab.cpp`）

```
Main:
  support_pillar_diameter
  angle_between_top_and_middle    ← 取代 support_bracing_angle

Raft Setting:
  pad_wall_thickness
  pad_brim_size
  pad_max_merge_distance
  pad_wall_slope

Bridge:
  support_critical_angle          ← Support Angle
  cross_angle                     ← 新增
  support_max_bridge_length
  support_max_pillar_link_distance
```

`support_bracing_angle` 不再出現在 UI，但 config 定義保留供 alias 與舊 profile 讀取。

### Config 定義（`PrintConfig.cpp`）

| Key | Label | Default | Min | Max |
|-----|-------|---------|-----|-----|
| `angle_between_top_and_middle` | Angle Between Top And Middle | 45 | 0 | 90 |
| `cross_angle` | Cross Angle | 45 | 0 | 90 |
| `support_bracing_angle` | （保留，不顯示於 UI） | 45 | 0 | 90 |

`SLAPrintObjectConfig` macro 新增兩欄；`Preset.cpp` 登記新 key。

### 向後相容策略

載入舊 profile 時：
1. 若只有 `support_bracing_angle`（無新 key）→ 兩個新參數皆 fallback 為其值（行為與現狀相同）
2. 若 profile 已有 `angle_between_top_and_middle`（現有 PhrozenSLA JSON）→ 用於 Top↔Middle；`cross_angle` 缺省時 fallback 至 `support_bracing_angle` 或預設 45°
3. UI 寫入時只寫新 key，不寫 `support_bracing_angle`（避免雙源）

可在 `make_support_cfg` 做 runtime resolve，無需獨立 migration script。

### Invalidation keys

`SLAPrint.cpp::invalidate_state_by_config_options` 新增：
- `angle_between_top_and_middle`
- `cross_angle`

保留既有 `support_bracing_angle` 觸發。

## Risks / Trade-offs

**舊 profile 只設 `angle_between_top_and_middle` 而無 `cross_angle`**
→ `cross_angle` fallback 至 `support_bracing_angle` 或 45°，與修改前單一角度行為一致。

**Branching Tree 仍用單一 `branchingsupport_bracing_angle`**
→ 與 Default Tree 行為不一致，但本次明確排除；文件標註為已知限制。

**`PrintConfig` 與 `SLAPrintObjectConfig` 同名 key 混淆**
→ 僅修改 `SLAPrintObjectConfig`；`PrintConfig::support_bracing_angle`（LCD）不動。

**手動點 `support_bracing_angle_deg` 仍映射舊語意**
→ 使用者決定不調整；該欄位繼續作為 Top↔Middle override，不影響 Cross Angle。

## Migration Plan

1. 實作 config + 引擎拆分（行為正確）
2. 更新 UI 佈局與 i18n
3. 更新 PhrozenSLA profile JSON：補 `"cross_angle": "45"`；保留或移除 `angle_between_top_and_middle`（已存在）
4. 驗證：舊 profile（僅 `support_bracing_angle`）切片結果與修改前一致

Rollback：還原程式碼；profile 中新增的 `cross_angle` 會被忽略，不影響舊版。

## Open Questions

（無 — 使用者已確認全部決策）
