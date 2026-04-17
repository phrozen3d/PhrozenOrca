## Why

在 `interconnect_pillars()` 的 `cascadefn` 中，存在兩個機制導致支撐柱跨越中間鄰居、直接橋接到更遠的柱子，造成橋段視覺上「跳格」。此問題於 `sla-support-weight-early-binding` 測試期間確認影響全部 weight 類型（Heavy、Medium、Light），屬預先存在的行為缺陷，需獨立修正。

## What Changes

- 移除或修正 `cascadefn` 中以 `r_start` 大小過濾鄰居的守衛條件（line ~1189），使不同粗細的柱子可互相橋接
- 在 min-dist 失敗後加入適當的終止邏輯，避免 loop 持續尋找更遠的鄰居而跳過中間柱
- 目標：每根柱子優先連接最近的合法鄰居，不跳格

## Capabilities

### New Capabilities

- `sla-pillar-bridge-routing`：支撐柱橋接目標選擇的行為規格——最近優先、不依 r_start 大小無條件跳過鄰居

### Modified Capabilities

（無——目前無任何既有 spec 描述此橋接選擇行為）

## Impact

- `src/libslic3r/SLA/SupportTreeBuildsteps.cpp`：`cascadefn` lambda（`interconnect_pillars()` 內，約 line 1148–1209）
  - Line ~1189：`if (neighborpillar.r_start < pillar.r_start) continue;` — 造成大柱跳過所有小柱
  - min-dist 失敗後的 loop continue（無 break）— 造成跨格橋接
- 影響全部 SLA 支撐 weight 類型（Heavy、Medium、Light）
- 不影響既有 API 或設定欄位，純屬橋接行為修正
