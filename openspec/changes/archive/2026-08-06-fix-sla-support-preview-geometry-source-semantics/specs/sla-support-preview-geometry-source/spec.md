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

#### Scenario: 只設定過部分欄位的支撐點

- **GIVEN** 一顆支撐點，只有 `head_width_mm` 被設定過實值，其餘可設定欄位（`head_back_radius_mm`／`head_penetration_mm`／`contact_sphere_radius`）維持 `SUPPORT_POINT_USE_PRESET`。此狀態不限點類型——auto 生成點天生即符合（見下方 Note）
- **WHEN** 渲染其 preview 錐體
- **THEN** 錐體長度採用該點的 `head_width_mm`
- **AND** Lower Diameter／Penetration／Contact Sphere 等其餘**有 sentinel 的**欄位採用 live preset 值
- **AND** 與該點切片產生的支撐幾何一致

Note：`head_front_radius`（Upper Diameter）沒有 `SUPPORT_POINT_USE_PRESET` sentinel，任何點（auto 或 manual）一旦存在就恆有具體值，SHALL NOT 被本規則誤讀為「可能採用 live preset」。

#### Scenario: 僅 head_front_radius 有值、其餘欄位皆未設定的點

- **GIVEN** 一顆支撐點，除 `head_front_radius`（無 sentinel，恆有值）外，其餘可設定欄位皆為 `SUPPORT_POINT_USE_PRESET`——即剛生成、尚未經選取編輯的 auto 點
- **WHEN** 渲染其 preview 錐體
- **THEN** 除 `head_front_radius` 外的全部尺寸採用 live preset 值
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

### Requirement: 手動點放置時全部 Top 欄位須於建立當下凍結

支撐點在手動放置的當下，SHALL 把全部 Top 欄位（Upper Diameter、Lower Diameter、Segment Length、Penetration、Contact Sphere）寫入建立當下的面板即時值，SHALL NOT 依賴其他欄位（如 Pillar Diameter）作為未設定欄位的替代來源。

#### Scenario: 新建手動點的 Lower Diameter 凍結

- **GIVEN** 手動編輯模式，面板 Lower Diameter 目前顯示某個值
- **WHEN** 使用者在模型表面點擊建立一顆新的手動點
- **THEN** 該點的 Lower Diameter 於建立當下即凍結為該面板值
- **AND** 之後調整面板的 Lower Diameter 或 Pillar Diameter，該點的錐體外型與切片結果都不改變

### Requirement: 多選支撐點時的顯示與編輯語意

多選多顆支撐點時，Top 欄位面板 SHALL 顯示**最後一個被選取的點**的值。編輯任一 Top 欄位時，SHALL 同步套用到**全部已選取的點**，SHALL NOT 只套用到顯示中的錨點。

#### Scenario: 多選顯示錨點值

- **GIVEN** 使用者依序選取兩顆以上尺寸不同的支撐點
- **WHEN** 檢視 Top 欄位面板
- **THEN** 面板顯示的是最後一個被選取的點的值

#### Scenario: 多選編輯同步套用全部選取點

- **GIVEN** 多顆支撐點處於選取狀態
- **WHEN** 使用者編輯面板上的任一 Top 欄位
- **THEN** 全部已選取的點同步套用該值並立即改變外型
- **AND** 未選取的點不受影響

### Requirement: picking 與顯示的幾何來源一致

支撐點 picking 半徑所依據的幾何參數 SHALL 與其 preview 錐體所使用的一致，SHALL NOT 因兩者各自解析而產生落差。

#### Scenario: hover 命中範圍符合可見錐體

- **GIVEN** 編輯模式，一顆 per-point 尺寸與 preset 明顯不同的支撐點
- **WHEN** 使用者將滑鼠移至該點可見錐體的邊緣
- **THEN** 命中判定與可見錐體一致
- **AND** 不出現看得到卻點不到、或點得到卻看不到的區域
