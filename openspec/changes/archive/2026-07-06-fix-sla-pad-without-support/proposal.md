## Why

在 Resin（SLA）模式下，當使用者未啟用支撐生成時切片，物件底部仍會出現類似 raft/pad 的底盤結構。Phrozen 的產品設計預期：pad 只應在支撐存在時作為支撐樹的底座出現，單獨無支撐時不應生成任何 pad。

## What Changes

- 當 `generate_support=false` 時，`generate_pad()` 應跳過 pad 生成（移除既有 pad 並提早返回），不論 `pad_enable` 設定值為何。
- 這消除了上游「builtin pad」行為（即在無支撐模式下以物件底部輪廓採樣生成 pad）。

## Capabilities

### New Capabilities

- `sla-pad-requires-support`: 定義 SLA pad 生成的 guard 規則——僅在 `generate_support=true` 時生成 pad；當 `generate_support=false` 時，`generate_pad()` 應移除既有 pad 並跳過生成。

### Modified Capabilities

（無既有 spec 需要修改需求層級）

## Impact

- **主要檔案**：`src/libslic3r/SLAPrintSteps.cpp`（`generate_pad()` 函式，約第 1028 行）
- **次要影響**：無，profile 檔案不需修改，UI 行為不受影響
- **不影響**：`slice_supports()` 的 guard（第 1075 行已正確處理）、零高度（zero-elevation / embed_object）pad 模式