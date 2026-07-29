## ADDED Requirements

### Requirement: Education 尾綴命名規則
當建置旗標 `PHROZEN_ORCA_ENABLE_RESIN` 為 `ON` 時，系統的所有使用者可見與作業系統可見識別字串 SHALL 帶有尾綴：若該字串開頭為大寫字母，尾綴為 "-Education"；若開頭為小寫字母，尾綴為 "-education"。當旗標為 `OFF` 時，所有識別字串 SHALL 與旗標存在前的行為完全相同，不帶任何尾綴。

#### Scenario: resin/education 建置的字串大寫開頭
- **WHEN** 使用 `build_resin_release_vs2022.bat` 建置（`PHROZEN_ORCA_ENABLE_RESIN=ON`），且某識別字串原文為 "PhrozenOrca"
- **THEN** 該字串最終呈現為 "PhrozenOrca-Education"

#### Scenario: resin/education 建置的字串小寫開頭
- **WHEN** 使用 `build_resin_release_vs2022.bat` 建置，且某識別字串原文為 "phrozen-orca"
- **THEN** 該字串最終呈現為 "phrozen-orca-education"

#### Scenario: 主線 FDM 建置不受影響
- **WHEN** 使用 `build_release_vs2022.bat` 建置（`PHROZEN_ORCA_ENABLE_RESIN` 維持預設 `OFF`）
- **THEN** 所有識別字串維持原樣，不帶 "-Education"/"-education" 尾綴，且與旗標引入前的既有行為逐位元組相同

### Requirement: AppData 使用者資料夾隔離
系統的每台使用者資料/設定資料夾（透過 `SLIC3R_APP_KEY` 決定）SHALL 依建置旗標套用尾綴規則，使主線版與 resin/education 版讀寫不同的資料夾，避免 profile/設定互相污染。

#### Scenario: 兩個變體不共用 AppData 資料夾
- **WHEN** 使用者的電腦上同時安裝了主線版與 resin/education 版
- **THEN** 開啟 `%APPDATA%`（Windows）或 `~/Library/Application Support/`（macOS）時，兩者對應到不同名稱的資料夾（例如 `PhrozenOrca` 與 `PhrozenOrca-Education`），且各自的 profile/設定檔互不干擾

### Requirement: 安裝目錄與登錄機碼隔離
resin/education 版的 Windows 安裝程式 SHALL 使用與主線版不同的安裝目錄名稱、`CPACK_PACKAGE_INSTALL_REGISTRY_KEY`、桌面捷徑/開始功能表項目名稱，使先後安裝兩個版本不會互相覆蓋彼此的檔案或解除安裝機碼。

#### Scenario: 先安裝主線版、後安裝 resin/education 版
- **WHEN** 使用者先安裝主線版，再安裝 resin/education 版的安裝程式
- **THEN** 兩者被安裝到不同的目錄，控制台「新增或移除程式」清單中出現兩個獨立的解除安裝項目，且解除安裝其中一個不會移除另一個的檔案

#### Scenario: 反序安裝
- **WHEN** 使用者先安裝 resin/education 版，再安裝主線版
- **THEN** 結果與上一情境對稱：兩者仍各自獨立安裝與解除安裝，互不影響

### Requirement: 執行檔與程序識別隔離
resin/education 版的 Windows 執行檔實體檔名 SHALL 為 `phrozen-orca-education.exe`，且所有依賴執行檔檔名字面量推算路徑的程式碼（包含網路外掛 DLL 路徑推算、檔案版本資訊）SHALL 與此檔名同步，不得因改名而導致功能失效。

#### Scenario: 工作管理員可分辨兩個變體
- **WHEN** 使用者同時執行主線版與 resin/education 版
- **THEN** 工作管理員（Windows）或活動監視器（macOS）中兩個程序顯示不同的程序名稱，可直接分辨

#### Scenario: 網路外掛 DLL 路徑推算不受 exe 改名影響
- **WHEN** resin/education 版的執行檔位於某個安裝路徑下，且需要透過「目前執行檔路徑」推算網路外掛 DLL 的位置（`data_dir()/plugins/` 找不到時的備援路徑）
- **THEN** 系統能正確組出以 `phrozen-orca-education.exe` 為基準、將檔名部分替換為對應 DLL 檔名的完整路徑，不因字串長度或內容不符而找不到

### Requirement: 主視窗標題與工作列名稱隔離
主視窗標題 SHALL 透過單一的品牌名稱變數呈現（而非另外寫死的字面量），使 resin/education 版的視窗標題自動帶有尾綴，且既有的「偵測其他執行中實例」邏輯（透過視窗標題子字串比對）在改名後仍需正確運作。

#### Scenario: 視窗標題呈現正確品牌名稱
- **WHEN** 使用者開啟 resin/education 版並載入專案檔
- **THEN** 主視窗標題列顯示的品牌名稱部分為 "PhrozenOrca-Education"（而非 "PhrozenOrca"）

#### Scenario: 單一實例偵測邏輯不受影響
- **WHEN** resin/education 版已有一個實例在執行，使用者再次啟動該版本
- **THEN** 系統仍能正確偵測到已有實例執行中，並將現有視窗帶到前景（不因視窗標題新增尾綴而偵測失敗）

### Requirement: macOS 套件識別隔離
resin/education 版的 macOS `CFBundleIdentifier` 與自訂 URL scheme SHALL 與主線版不同，避免兩者在 macOS 上以相同 Bundle ID 安裝時互相覆蓋。

#### Scenario: macOS 上安裝兩個變體
- **WHEN** 使用者在同一台 Mac 上安裝主線版與 resin/education 版
- **THEN** 系統偏好設定或 Launch Services 資料庫中，兩者以不同的 Bundle Identifier 註冊，不互相覆蓋對方的應用程式套件
