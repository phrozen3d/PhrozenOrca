## ADDED Requirements

### Requirement: education 變體的專屬建置管線
系統 SHALL 提供一個專屬的 GitHub Actions workflow，用於建置 resin/education 變體（`PHROZEN_ORCA_ENABLE_RESIN=ON`），且該 workflow SHALL 在單次執行中同時涵蓋 Windows 與 macOS 兩個平台。此 workflow SHALL NOT 建置主線（FDM）變體。

#### Scenario: 單次觸發同時建出兩個平台
- **WHEN** 使用者觸發 education 建置 workflow
- **THEN** 該次執行同時產生 Windows 與 macOS 兩個平台的 job，且兩者皆以 `PHROZEN_ORCA_ENABLE_RESIN=ON` 建置

#### Scenario: 產出物齊備
- **WHEN** education 建置 workflow 成功完成
- **THEN** 可從該次執行的 artifact 下載到 Windows 的 NSIS 安裝檔、portable ZIP 與 PDB 壓縮檔，以及 macOS 的 DMG

### Requirement: 僅限手動觸發
education 建置 workflow SHALL 僅能以手動方式觸發，SHALL NOT 因為對任何分支的 push、pull request 或排程（cron）而自動執行。

#### Scenario: 一般開發推送不觸發建置
- **WHEN** 開發者對任何分支推送一般的程式碼變更
- **THEN** education 建置 workflow 不會被觸發，不消耗 CI 額度

#### Scenario: 手動觸發可指定分支
- **WHEN** 使用者從 Actions 頁面手動觸發 education 建置 workflow 並指定某個分支作為 ref
- **THEN** 該次執行使用該分支上的 workflow 定義與程式碼進行建置

### Requirement: 主線共用檔案零修改
本管線 SHALL NOT 在主線（預設分支）的任何共用 CI 檔案中加入 resin/education 相關的判斷、條件或參數。受保護的共用檔案包含既有的建置 workflow 鏈、Windows 打包用的 composite action，以及主線的建置腳本。主線 SHALL 僅新增獨立的、主線自身不會執行的 workflow 檔案。

#### Scenario: 主線既有建置行為不變
- **WHEN** 本 change 完成後，主線既有的建置流程（完整建置、每日 Windows、每週 macOS、release 建置）被觸發
- **THEN** 其行為、產出物名稱與內容與本 change 實施前完全相同

#### Scenario: 主線共用 CI 檔案內容未被更動
- **WHEN** 比對本 change 前後主線上的既有建置 workflow 鏈與 Windows 打包 composite action
- **THEN** 這些檔案的內容完全未被修改

### Requirement: 主線更新合併至 resin 支線時不產生共用檔案衝突
resin 支線上的既有共用檔案 —— 包含建置 workflow 鏈與**主線的建置腳本** —— SHALL 與主線保持內容一致，使主線的 FDM 更新合併進 resin 支線時，不會在這些檔案上產生合併衝突。education 變體所需的建置邏輯 SHALL 一律置於主線不會使用到的獨立新檔案中，而非以旗標或條件的形式加入共用檔案。

#### Scenario: 主線修正 CI 後合併至 resin 支線
- **WHEN** 主線修改了既有的建置 workflow 鏈中的任一檔案，之後將主線合併進 resin 支線
- **THEN** 這些檔案乾淨套用，不產生合併衝突

#### Scenario: 主線修正建置腳本後合併至 resin 支線
- **WHEN** 主線修改了共用的建置腳本，之後將主線合併進 resin 支線
- **THEN** 該腳本乾淨套用，不產生合併衝突（education 使用的是獨立的專用腳本，不觸及共用腳本）

### Requirement: 主線 CI 變更的漂移偵測
當主線的既有共用 CI 檔案或建置腳本發生變更並被合併進 resin 支線時，系統 SHALL 在下一次執行 education 建置時自動偵測到該變更並使建置失敗，以強制人工評估 education 的建置邏輯與建置腳本是否需要同步相同的修正。此偵測 SHALL 在昂貴的建置作業開始之前完成。

監控範圍 SHALL 涵蓋 education 建置邏輯所複製或參照的所有主線檔案，包含既有的建置 workflow 鏈，以及 education 專用建置腳本所複製自的主線建置腳本。

#### Scenario: 偵測到主線 CI 已變更
- **WHEN** 主線的共用 CI 檔案變更已合併進 resin 支線，且尚未經人工確認 education 建置邏輯是否需同步
- **THEN** 下一次觸發 education 建置時，偵測作業失敗並明確指出哪些檔案發生變更，且後續的建置作業不會啟動

#### Scenario: 偵測到主線建置腳本已變更
- **WHEN** 主線的建置腳本（education 專用腳本的複製來源）發生變更並合併進 resin 支線，且尚未經人工確認 education 專用腳本是否需同步
- **THEN** 下一次觸發 education 建置時，偵測作業失敗並指出該腳本已變更，且後續的建置作業不會啟動

#### Scenario: 人工確認後恢復正常
- **WHEN** 人員比對主線變更、完成必要的同步（或確認無須同步）並更新記錄的校驗值後，再次觸發 education 建置
- **THEN** 偵測作業通過，建置作業正常進行

#### Scenario: 偵測作業本身成本低廉
- **WHEN** 偵測作業執行
- **THEN** 其只需取得原始碼與計算校驗值，不觸發任何編譯、相依套件建置或簽章流程

### Requirement: 產出物命名可與主線區分
education 變體的所有產出物（artifact 名稱、Windows 安裝檔檔名、macOS DMG 檔名）SHALL 帶有可明確識別為 education 變體的命名，使其不會與主線產出物混淆。

#### Scenario: 安裝檔與 DMG 檔名帶有變體識別
- **WHEN** 使用者下載 education 建置產生的 Windows 安裝檔與 macOS DMG
- **THEN** 兩者的檔名皆帶有 education 識別與版本尾綴，可與主線同版本的產出物明確區分

#### Scenario: artifact 名稱可區分
- **WHEN** 使用者檢視 education 建置執行結果的 artifact 清單
- **THEN** artifact 名稱可明確辨識為 education 變體，不與主線建置的 artifact 名稱重複

### Requirement: macOS 產出物須經簽章與公證
macOS 的 DMG 產出物 SHALL 經過 codesign 簽章、Apple 公證（notarization）與 staple，使終端使用者在 macOS 上開啟時不會被 Gatekeeper 阻擋。

#### Scenario: 公證後的 DMG 可通過 Gatekeeper
- **WHEN** 終端使用者下載 education 的 macOS DMG 並開啟其中的應用程式
- **THEN** macOS 不顯示「無法驗證開發者」之類的 Gatekeeper 阻擋訊息

#### Scenario: 簽章資訊可驗證
- **WHEN** 對產出的 DMG 或其中的應用程式套件執行簽章驗證
- **THEN** 驗證結果顯示簽章有效、已啟用 hardened runtime，且公證票據已附加

### Requirement: education 相依套件快取與主線隔離
education 建置使用的相依套件（deps）快取 SHALL 使用與主線不同的快取識別，且其快取路徑 SHALL 與 education 建置腳本實際使用的相依套件目錄一致。

#### Scenario: 不誤命中主線快取
- **WHEN** education 建置執行相依套件快取還原
- **THEN** 只會命中 education 專屬的快取項目，不會命中主線建立的快取，也不會發生快取宣稱命中但檔案落在錯誤路徑的情形

### Requirement: 從不適用的分支觸發時安全失敗
當 education 建置 workflow 被指定在不具備 education 建置能力的分支（例如主線）上執行時，該次執行 SHALL 快速失敗，且 SHALL NOT 產出任何看似 education 變體但實為主線變體的成品。

#### Scenario: 誤選主線分支觸發
- **WHEN** 使用者手動觸發 education 建置但將 ref 指定為主線分支
- **THEN** 執行因缺少 education 專用建置腳本而失敗，不產生任何 artifact
