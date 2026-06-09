## Why

Resin (SLA) 模式的手動支撐點編輯（SLA Support Point gizmo）需要讓使用者預覽模型離平台的抬升距離，但目前 `support_object_elevation` 參數在 UI 中未顯示，且進入 gizmo 時模型仍貼在平台上（`get_current_elevation()` 在支撐樹尚未建立時回傳 0）。CHITUBOX 等樹脂切片軟體將此參數稱為 **Model Lift Height**，是支撐設定的基本項目，應在 Raft 區塊中可見並驅動 gizmo 預覽抬升。

## What Changes

- 在 Resin Support 設定頁的 **Raft Setting** 區塊、**Raft Thickness** 上方新增 **Model Lift Height** 欄位。
- 重用既有 config key `support_object_elevation`，更新 label 與 tooltip（不新增持久化 key）。
- 進入 SLA Support Point gizmo 時，依 Model Lift Height 以視覺 Z shift 抬升選取模型；退出 gizmo 時模型貼回地面（shift = 0）。
- Gizmo 開啟期間若使用者調整 Model Lift Height，3D 預覽即時更新抬升高度。
- 當 `pad_enable` + `pad_around_object`（zero-elevation 模式）啟用時，隱藏或禁用 Model Lift Height 並維持 elevation = 0。
- 切片管線維持既有 `support_object_elevation` → `object_elevation_mm` 映射與最小值 clamp 行為。

## Capabilities

### New Capabilities

- `sla-model-lift-height`: Resin Raft UI 暴露 Model Lift Height，以及 SLA Support Point gizmo 進入/退出時的模型 Z 抬升預覽行為。

### Modified Capabilities

- `sla-support-param-wiring`: 補充 `support_object_elevation` 的 UI 可見性、標籤語意與 gizmo 預覽期間的即時抬升需求。

## Impact

- **Config / 核心**: `PrintConfig.cpp`（label、tooltip）、`SLAPrint.cpp`（`get_current_elevation` / gizmo 預覽路徑，若需調整）
- **GUI**: `Tab.cpp`（Raft Setting 欄位順序）、`ConfigManipulation.cpp`（`pad_around_object` 互斥顯示）
- **Gizmo**: `GLGizmosCommon.hpp/.cpp`（恢復 gizmo-aware Z shift）、`GLGizmoSlaSupports.cpp`（`on_set_state`）、`GLGizmoSlaBase.cpp`（`update_volumes`）
- **Profiles**: 無需新增 key（既有 `support_object_elevation: "5"` 可沿用）
