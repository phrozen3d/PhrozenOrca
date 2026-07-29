## ADDED Requirements

### Requirement: Windows 檔案關聯不得使用 OrcaSlicer 命名空間
系統註冊 `.3mf`/`.stl`/`.step` 等副檔名的 Windows 檔案關聯時，SHALL 使用 PhrozenOrca 專屬的 ProgID 與描述文字，不得使用 `Orca.Slicer.1` 或任何包含 "OrcaSlicer" 字樣的 ProgID/描述。

#### Scenario: 安裝 PhrozenOrca 不覆蓋外部 OrcaSlicer 的檔案關聯
- **WHEN** 使用者的電腦上已安裝真正的 OrcaSlicer，並已將 `.3mf` 關聯到它，之後再安裝 PhrozenOrca 並執行「關聯檔案」設定
- **THEN** PhrozenOrca 註冊自己專屬的 ProgID，不覆蓋或竄改 OrcaSlicer 原本註冊的 `Orca.Slicer.1` 機碼內容

#### Scenario: 解除關聯只影響 PhrozenOrca 自己的機碼
- **WHEN** 使用者在 PhrozenOrca 內執行「取消檔案關聯」
- **THEN** 系統只刪除/還原 PhrozenOrca 自己 ProgID 底下的機碼，不影響其他軟體註冊的檔案關聯

### Requirement: 自訂 URL scheme 使用 PhrozenOrca 自己的命名
Windows 與 macOS 上註冊的自訂 URL scheme SHALL 一致使用 PhrozenOrca 自己的 scheme 名稱，不得註冊 `orcaslicer://`。

#### Scenario: Windows 端註冊 PhrozenOrca 專屬 scheme
- **WHEN** 使用者在 PhrozenOrca 內觸發 URL scheme 關聯設定
- **THEN** 系統在登錄檔中註冊的是 PhrozenOrca 自己的 scheme（與 macOS 端已宣告的 scheme 一致），而非 `orcaslicer`

### Requirement: Linux 桌面整合輸出的檔案品牌一致
Linux 上「桌面整合」功能寫出的圖示檔案、`.desktop` 檔案（含一般啟動項目與 URL protocol handler）SHALL 使用當前建置所對應的 `SLIC3R_APP_NAME`/`SLIC3R_APP_KEY`，不得寫死 "OrcaSlicer" 字面量。

#### Scenario: 執行桌面整合功能後產生正確命名的檔案
- **WHEN** 使用者在 Linux 上的 PhrozenOrca 內執行「桌面整合」功能
- **THEN** 產生的圖示檔名、`.desktop` 檔名與內容（`Name=`/`Icon=`）都反映目前建置的品牌名稱（例如主線版為 "PhrozenOrca"，education 版為 "PhrozenOrca-Education"），不出現任何 "OrcaSlicer" 字樣

#### Scenario: 不覆蓋外部 OrcaSlicer 的桌面整合檔案
- **WHEN** 使用者的 Linux 系統上已經對真正的 OrcaSlicer 執行過桌面整合
- **THEN** PhrozenOrca 執行桌面整合功能後產生的檔案與 OrcaSlicer 的檔案為不同檔名，兩者並存不互相覆蓋

### Requirement: 建置期 Linux .desktop 檔案的啟動指令正確
`PhrozenOrca.desktop` 的 `Exec=`、`StartupWMClass`、`MimeType` 中的 URL scheme 部分 SHALL 與實際輸出的執行檔名稱、URL scheme 一致，使安裝後的桌面捷徑能正確啟動軟體。

#### Scenario: 桌面捷徑能正確啟動軟體
- **WHEN** 使用者在 Linux 上透過套件管理或 FHS 安裝方式安裝 PhrozenOrca，並從應用程式選單點擊捷徑
- **THEN** 軟體正確啟動，不因 `Exec=` 指向不存在的執行檔名稱而失敗

### Requirement: 單一實例偵測的命名空間使用 PhrozenOrca 品牌
Linux D-Bus 匯流排/物件名稱與 macOS distributed notification 名稱 SHALL 使用 PhrozenOrca 自己的命名空間（而非 "softfever3d"/"orca-slicer"/"OrcaSlicer"），且與 build 變體分流相關的既有 hash/version 後綴機制維持不變。

#### Scenario: D-Bus 匯流排名稱反映 PhrozenOrca 品牌
- **WHEN** PhrozenOrca 在 Linux 上啟動並註冊單一實例偵測用的 D-Bus 服務
- **THEN** 匯流排與物件名稱使用 PhrozenOrca 專屬命名空間，不包含 "orca-slicer"/"softfever3d" 字樣

#### Scenario: macOS 通知名稱反映 PhrozenOrca 品牌
- **WHEN** PhrozenOrca 在 macOS 上偵測已執行的實例並發送 distributed notification
- **THEN** 通知名稱使用 PhrozenOrca 專屬前綴，不包含 "OrcaSlicer" 字樣

### Requirement: G-code Viewer 品牌顯示一致
G-code Viewer 的內部設定識別常數與所有 UI 曝光點（獨立視窗標題、關於視窗、系統資訊視窗）SHALL 顯示 PhrozenOrca 品牌名稱，不得顯示 "OrcaSlicer G-code Viewer"。

#### Scenario: G-code Viewer 視窗標題顯示正確品牌
- **WHEN** 使用者開啟 G-code Viewer 獨立視窗
- **THEN** 視窗標題與「關於」對話框顯示的軟體名稱為 PhrozenOrca 對應命名，不包含 "OrcaSlicer" 字樣

### Requirement: 刻意保留的相容性字串不受影響
以下功能涉及的 "orcaslicer"/"OrcaSlicer" 字串 SHALL 維持不變，本次品牌清理不得影響其行為：G-code 檔頭相容性偵測、印表機/耗材 profile 更新來源、Obico 與 SimplyPrint 第三方雲端服務登入、多引擎來源 URL scheme 辨識、關於視窗中的血緣揭露文字。

#### Scenario: Profile 更新功能不受影響
- **WHEN** 使用者觸發印表機/耗材 profile 更新檢查
- **THEN** 系統仍正確連線到原本的上游 profile repo 並完成更新，不因本次品牌清理而失效

#### Scenario: 第三方雲端服務登入不受影響
- **WHEN** 使用者透過 Obico 或 SimplyPrint 進行帳號登入/連動
- **THEN** 登入流程正常完成，OAuth client_id 與本次修改前一致
