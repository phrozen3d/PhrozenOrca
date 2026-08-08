# sla-support-auto-point-top-field-freeze

## Overview

本 capability 規範 auto 生成支撐點（`island`／`slope` 類型）在生成當下，全部 Top 幾何欄位（Upper Diameter、Lower Diameter、Segment Length、Penetration、Contact Sphere）是否寫入具體值並凍結。

與相鄰 capability 的分工：`sla-support-param-wiring` 規範「preset 是否正確傳入切片演算法、變動時是否觸發重切」；本 capability 規範「已生成的 auto 點，其自身儲存的 Top 欄位是否完整，使其不必在每次重切時退回讀取即時 preset」。與 `sla-support-preview-geometry-source`（`fix-sla-support-preview-geometry-source-semantics`）的分工：該 capability 規範 preview／picking 如何**讀取**一顆點的幾何來源；本 capability 規範 auto 生成時**寫入**了什麼——本 capability 是前者「逐欄位讀取」規則能夠對 auto 點產生正確結果的資料前提。

對齊基準為切片端既有的逐欄位仲裁規則（`SupportPoint.hpp` 的 `point_*()` helper：per-point 值已設定就用 per-point，否則退回 preset）。本 capability 不改變這條規則，只確保 auto 點在生成當下，讓每一個欄位都處於「已設定」狀態。

## ADDED Requirements

### Requirement: Auto 生成點的全部 Top 欄位須於生成當下凍結

Auto 生成支撐點（`island`／`slope` 類型）時，系統 SHALL 將生成當下的 Top preset 值（Upper Diameter、Lower Diameter、Segment Length、Penetration、Contact Sphere／Contact Type）寫入每一顆新產生的支撐點自身的對應欄位，SHALL NOT 只寫入其中一個欄位而讓其餘欄位維持「未設定」狀態。

#### Scenario: 生成後全部欄位皆為具體值

- **GIVEN** Process tab 的 Top 欄位目前顯示某組數值
- **WHEN** 使用者觸發 Auto-generate 產生支撐點
- **THEN** 每一顆新生成的支撐點的 Upper Diameter、Lower Diameter、Segment Length、Penetration、Contact Sphere 欄位皆為生成當下的具體值
- **AND** 沒有任何一個欄位維持「使用 preset」的未設定狀態

### Requirement: 已生成的 auto 點不受後續面板編輯牽動

在使用者重新觸發整批生成（Apply/Auto-generate）之前，調整 Process tab 的任何 Top 欄位 SHALL NOT 改變已生成 auto 點的實際切片幾何。此規則對 preview 顯示與最終切片輸出 SHALL 一致生效。

#### Scenario: 面板調整不影響已生成點的切片結果

- **GIVEN** 已用 Top 欄位值 A 生成一批 auto 支撐點
- **WHEN** 使用者將 Process tab 的任一 Top 欄位改為值 B，且未重新觸發 Auto-generate
- **THEN** 該批 auto 點的支撐頭尺寸（Upper/Lower Diameter、Segment Length、Penetration、Contact Sphere）在切片輸出中仍為 A
- **AND** 即使觸發了包含 Structure 檢視或其他部分重切的動作，該批點的這些欄位仍不受影響

#### Scenario: 重新生成後改用新值

- **GIVEN** 已用 Top 欄位值 A 生成一批 auto 支撐點，Process tab 已改為值 B
- **WHEN** 使用者重新觸發 Auto-generate
- **THEN** 舊的支撐點依現行行為被清除
- **AND** 新生成的支撐點全部 Top 欄位凍結為值 B

### Requirement: Auto 點生成的效能不因欄位擴充而劣化

系統 SHALL 確保新增的欄位寫入不對支撐點生成流程造成可觀測的效能劣化，且不使 preview 端的幾何快取（`HeadGeomKey`）distinct key 數量增加。

#### Scenario: 大量支撐點生成耗時無退化

- **GIVEN** 一個會生成數千顆 auto 支撐點的模型
- **WHEN** 比較本 change 前後的 Auto-generate 執行耗時
- **THEN** 耗時差異在量測誤差範圍內，無可觀測的退化

#### Scenario: 幾何快取規模不因此增加

- **GIVEN** 一批共用同一組生成時 preset 快照的 auto 支撐點
- **WHEN** preview 渲染這批點
- **THEN** 這批點對應的 `HeadGeomKey` distinct 數量與本 change 之前相比持平或減少，不增加
