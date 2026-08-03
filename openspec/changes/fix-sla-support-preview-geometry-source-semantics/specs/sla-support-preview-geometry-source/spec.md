# sla-support-preview-geometry-source

## Overview

本 capability 規範 Points preview 錐體採用哪一組幾何參數：支撐點自身儲存的 per-point 值，或 Process tab 的 live preset 值。

與相鄰 capability 的分工：`sla-support-points-preview` 規範「畫在哪、多大、能不能被點到」——即錐體如何被擺放與拾取；本 capability 規範「那個尺寸是從哪來的」。

對齊基準為切片端。`SupportTreeBuildsteps` 無條件套用 `SupportPoint.hpp` 的 `point_*()` 助手，逐欄位仲裁（per-point 值 ≥ 0 就用 per-point，否則退回 preset），且不存在「編輯模式」或「選取狀態」的概念。

## ADDED Requirements

### Requirement: 幾何來源的判定規則必須完整且書面化

Points preview 的幾何參數來源判定 SHALL 有一份涵蓋全部輸入維度的書面規則，並 SHALL 對每一種輸入組合給出唯一確定的結果。

輸入維度至少包含：是否處於編輯模式、支撐點類型（`manual_add` / `island` / `slope`）、各 per-point 欄位是否已設定實值、該點是否被選取。

規則 SHALL NOT 存在未定義或依實作細節而異的組合。

#### Scenario: 每種輸入組合都有確定結果

- **GIVEN** 任一支撐點與任一 UI 狀態組合
- **WHEN** 依規則判定其 preview 幾何來源
- **THEN** 得到唯一確定的結果
- **AND** 該結果可由規則推導，不需查閱實作

### Requirement: 選取狀態不得改變 preview 的幾何尺寸

支撐點的選取狀態 SHALL NOT 改變該點 preview 錐體的幾何尺寸。選取 SHALL 只影響顏色等標示性呈現。

理由：preview 的職責是顯示該點會被切片產生成什麼樣，而切片端不存在選取概念。選取造成尺寸跳動會使使用者無法判斷哪一個尺寸才是實際結果。

#### Scenario: 選取尚無 explicit geometry 的手動點

- **GIVEN** 編輯模式，一顆 `manual_add` 且尚未設定任何 per-point 幾何欄位的支撐點
- **WHEN** 使用者點選該點
- **THEN** 該點的錐體尺寸與選取前完全相同
- **AND** 僅顏色改變以標示選取狀態

#### Scenario: 選取自動生成的點

- **GIVEN** 編輯模式，一顆自動生成的支撐點
- **WHEN** 使用者點選該點
- **THEN** 該點的錐體尺寸與選取前完全相同

#### Scenario: 取消選取

- **GIVEN** 編輯模式且已選取一顆支撐點
- **WHEN** 使用者取消選取
- **THEN** 該點的錐體尺寸不變

### Requirement: preview 幾何與切片結果一致

任一支撐點的 preview 錐體所顯示的頭部直徑、頭部寬度、penetration 與 contact 球半徑 SHALL 與該點實際被切片產生的支撐幾何一致。

仲裁 SHALL 為逐欄位：每個欄位各自判斷該點是否已設定實值，SHALL NOT 因某一欄位被設定過就使該點的**全部**欄位改用儲存值。

#### Scenario: 只設定過部分欄位的手動點

- **GIVEN** 一顆 `manual_add` 點，只有 `head_width_mm` 被設定過實值，其餘欄位維持 `SUPPORT_POINT_USE_PRESET`
- **WHEN** 渲染其 preview 錐體
- **THEN** 錐體長度採用該點的 `head_width_mm`
- **AND** 頭部直徑等其餘尺寸採用 live preset 值
- **AND** 與該點切片產生的支撐幾何一致

#### Scenario: 完全未設定 per-point 幾何的點

- **GIVEN** 一顆所有 per-point 幾何欄位皆為 `SUPPORT_POINT_USE_PRESET` 的支撐點
- **WHEN** 渲染其 preview 錐體
- **THEN** 全部尺寸採用 live preset 值
- **AND** 與該點切片產生的支撐幾何一致

#### Scenario: 編輯模式與非編輯模式結果相同

- **GIVEN** 任一支撐點
- **WHEN** 分別在編輯模式與非編輯模式下渲染其 preview
- **THEN** 兩者顯示的尺寸完全相同

### Requirement: live preset 編輯的作用對象可預測

修改 Process tab 的 Top 欄位時，SHALL 只影響對應欄位尚未設定實值的支撐點；已設定實值的欄位 SHALL NOT 被 preset 覆蓋。

此規則 SHALL 與切片端一致。

#### Scenario: 修改 preset 影響未設定的點

- **GIVEN** Points preview 顯示中，含自動生成的點與一顆已設定 `head_front_radius` 的手動點
- **WHEN** 使用者修改 `support_head_front_diameter` 並觸發重繪
- **THEN** 自動生成的點的錐體直徑跟著改變
- **AND** 已設定該欄位的手動點的錐體直徑不變

#### Scenario: 修改 preset 對逐欄位生效

- **GIVEN** 一顆只設定過 `head_width_mm` 的手動點
- **WHEN** 使用者修改 `support_head_front_diameter` 並觸發重繪
- **THEN** 該點的錐體直徑跟著改變（該欄位未設定實值）
- **AND** 該點的錐體長度不變（該欄位已設定實值）

### Requirement: picking 與顯示的幾何來源一致

支撐點 picking 半徑所依據的幾何參數 SHALL 與其 preview 錐體所使用的一致，SHALL NOT 因兩者各自解析而產生落差。

#### Scenario: hover 命中範圍符合可見錐體

- **GIVEN** 編輯模式，一顆 per-point 尺寸與 preset 明顯不同的支撐點
- **WHEN** 使用者將滑鼠移至該點可見錐體的邊緣
- **THEN** 命中判定與可見錐體一致
- **AND** 不出現看得到卻點不到、或點得到卻看不到的區域
