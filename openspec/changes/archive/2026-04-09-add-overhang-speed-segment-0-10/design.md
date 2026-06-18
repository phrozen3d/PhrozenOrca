## Context

OrcaSlicer 的懸空速度分段以 `overhang_overlap_levels = {90, 75, 50, 25, 13, 0}` 作為 overlap 邊界，對應 6 個速度值（索引 0 為 100% 即不減速，索引 1-4 為使用者設定的四段速度，索引 5 為 bridge speed）。第一個 overlap 邊界 90 代表「overhang 達 10% 開始減速」，目前 overlap >= 90%（overhang 0-10%）固定使用 100% 的 wall speed，使用者無法對極輕微懸空進行速度控制。

本設計以「加入新速度值替換固定的 100% 初始速度」的最小侵入方式實作第五段，無需修改 overlap 閾值陣列，也無需重命名現有四個設定選項。

## Goals / Non-Goals

**Goals:**
- 新增 `overhang_0_4_speed` 設定選項，控制 0%-10% 懸空範圍的列印速度
- 當使用者將值設為 0 時，行為與目前相同（不減速，使用 wall speed）
- 更新 GUI、profile JSON，保持向後兼容（舊 profile 不含此欄位時視為 0）

**Non-Goals:**
- 不修改現有四個速度選項的命名（`overhang_1_4_speed` ~ `overhang_4_4_speed`）
- 不修改 overlap 閾值陣列 `{90, 75, 50, 25, 13, 0}`
- 不影響 bridge speed 邏輯或 `slowdown_for_curled_perimeters` 邏輯

## Decisions

### 決策一：不新增 overlap 閾值，改為替換第一個速度值

**選擇：** 將 GCode.cpp 中 `dynamic_overhang_speeds` 的首元素從固定的 `{100, true}` 改為可設定的 `overhang_0_4_speed`（與其他段相同的條件式結構）。

**理由：** overlap >= 90% 的範圍（0-10% 懸空）本就對應速度陣列的第一個索引。目前該索引固定為 100%（不減速），將其改為可設定值即達成目標，且 threshold 陣列完全不需要更動。修改最小、風險最低。

**捨棄方案：** 在 threshold 陣列前插入新值（如 97），需要修改 ExtrusionProcessor 的插值邏輯並界定邊界語義，風險較高。

### 決策二：命名沿用 `_0_4_` 前綴而非重新編號為 `_1_5_` 系列

**選擇：** 新選項命名為 `overhang_0_4_speed`，表示「四段系統中的第 0 段（前置段）」。

**理由：** 若改名現有四段為 `_1_5_` ~ `_4_5_`，所有 profile JSON 與使用者設定都需要 migration，風險大。使用 `_0_4_` 命名既避免破壞性變更，又在語義上清楚表示「早於第一段的前置段」。

### 決策三：預設值為 0（不減速）

與現有四段的預設值一致，0 值在 GCode 生成時會被轉換回 100%（wall speed），確保向後兼容。

## Risks / Trade-offs

- **[命名一致性]** `overhang_0_4_speed` 與 `overhang_1_4_speed`...`overhang_4_4_speed` 共存，但總段數為 5 段，名稱中的 `_4_` 不再對應「共四段」。→ **緩解：** 在 label 和 tooltip 中清楚標示範圍（`[0%, 10%)`），使用者不會混淆
- **[Profile 兼容性]** 舊 profile 沒有 `overhang_0_4_speed` 欄位時，會使用預設值 0（不減速），行為與目前完全相同 → **緩解：** 無需 migration

## Migration Plan

無需資料遷移。新欄位預設值 0 與現有行為相同，舊 profile 自動使用預設值。

## Open Questions

- 無
