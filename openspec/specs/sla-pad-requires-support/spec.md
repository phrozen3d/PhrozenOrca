# Capability: sla-pad-requires-support

## Purpose

控制 SLA 切片中 pad（底盤）生成是否以支撐啟用為前提。透過編譯期常數 `kPadRequiresSupport` 統一管理三個切片步驟的行為，避免在無支撐的情況下錯誤生成 pad 導致「unprintable objects」警告。

## Requirements

### Requirement: Pad 生成須以支撐啟用為前提（kPadRequiresSupport 開關）

`src/libslic3r/SLAPrint.hpp` SHALL 定義 `inline constexpr bool kPadRequiresSupport`，預設值為 `true`。此開關統一控制下列三處行為：

1. `generate_pad()`（`SLAPrintSteps.cpp`）：當 `kPadRequiresSupport && !generate_support` 時，SHALL 移除既有 pad 並跳過生成，不論 `pad_enable` 設定值。
2. `get_elevation()`（`SLAPrint.cpp`）：當 `kPadRequiresSupport && !generate_support` 時，SHALL 回傳 0，不將 pad 高度計入物件 elevation。
3. `slice_supports()`（`SLAPrintSteps.cpp`）：`need_support_slices` SHALL 等於 `generate_support || (!kPadRequiresSupport && pad_enable)`，確保 `kPadRequiresSupport=true` 時無支撐即跳過支撐切片步驟。

當 `kPadRequiresSupport = false` 時，三處行為 SHALL 完整還原上游 OrcaSlicer 邏輯（builtin pad 路徑）。

#### Scenario: kPadRequiresSupport=true，未啟用支撐時切片，不應出現 pad

- **WHEN** `kPadRequiresSupport=true`，使用者在 `generate_support=false`、`pad_enable=true` 的設定下觸發切片
- **THEN** 切片結果 SHALL NOT 包含任何 pad 幾何，物件底部 SHALL NOT 出現底盤結構，且切片流程 SHALL NOT 拋出「unprintable objects」警告

#### Scenario: kPadRequiresSupport=true，啟用支撐時切片，pad 正常生成

- **WHEN** `kPadRequiresSupport=true`，使用者在 `generate_support=true`、`pad_enable=true` 的設定下觸發切片
- **THEN** `generate_pad()` SHALL 按原有邏輯生成 pad，pad 正確出現於支撐樹底部

#### Scenario: kPadRequiresSupport=true，先有支撐切片後關閉支撐重切，pad 應被清除

- **WHEN** `kPadRequiresSupport=true`，使用者先以 `generate_support=true`、`pad_enable=true` 切片（已生成 pad），接著將 `generate_support` 改為 false 並再次切片
- **THEN** 重新切片後 pad mesh SHALL 被移除，不殘留先前生成的 pad，3D 場景 SHALL 刷新

#### Scenario: kPadRequiresSupport=false，無支撐時切片，上游 builtin pad 行為還原

- **WHEN** `kPadRequiresSupport=false`，使用者在 `generate_support=false`、`pad_enable=true` 的設定下觸發切片
- **THEN** `generate_pad()` SHALL 採樣物件底部輪廓（`pad_blueprint`）並生成 builtin pad，切片流程 SHALL 正常完成，不拋出任何錯誤

#### Scenario: pad_enable=false 時行為不受開關影響

- **WHEN** `pad_enable=false`，無論 `generate_support` 與 `kPadRequiresSupport` 為何值
- **THEN** `generate_pad()` SHALL 進入原有的 `else if` 分支（移除 pad），行為與修改前相同
