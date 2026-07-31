## 1. 階段 1-A：Windows job（先做，成本最低）

- [x] 1.1 在 `phrozen-education-variant` 分支建立 `.github/workflows/build_education.yml` 骨架：
  - `on:` 同時包含 `workflow_dispatch:`（正式用）與開發期專用的 `push:` 區塊（`branches: ['phrozen-education-variant']`，`paths:` 限定 `.github/workflows/build_education.yml`、`build_release_macos.sh`、`build_resin_release_vs2022.bat`）
  - **不宣告任何 `inputs:`**（見 design.md 決策 3）
  - 檔案開頭加註解說明：resin/education 專用、主線永不自動觸發、存在目的是讓 Actions 出現手動按鈕、請勿刪除
  - 此階段先只放 Windows job，macOS job 整段註解掉或尚未加入（避免每次迭代消耗 10 倍計費）
- [x] 1.2 Windows job：checkout（`lfs: 'true'`）＋ deps 快取設定
  - 快取鍵**必須含 education 識別**，不可與主線的 `${os}-cache-phrozenorca_deps-build-${hashFiles('deps/**')}` 相同
  - 快取路徑必須是 `deps/build-resin/PhrozenOrca_dep`（`build_resin_release_vs2022.bat` 實際使用的目錄），不是主線的 `deps/build`
- [x] 1.3 Windows job：環境準備 —— `lukka/get-cmake@latest`（`cmakeVersion: "~3.28.0"`）、`microsoft/setup-msbuild@v2`、`choco install strawberryperl`
- [x] 1.4 Windows job：deps 建置（僅在快取未命中時執行）—— `build_resin_release_vs2022.bat deps` 與 `pack`
- [x] 1.5 Windows job：slicer 建置 —— `build_resin_release_vs2022.bat slicer`，並設定 `WindowsSdkDir` 與 `WindowsSDKVersion` env（參考 `build_orca.yml` 的 `Build slicer Win` 步驟）
- [x] 1.6 Windows job：版本字串擷取 —— 從 `version.inc` 讀 `Phrozen_VERSION`（現況為乾淨的 `1.2.0`），**並在 CI 端補上 `-Education` 尾綴**後才用於命名（見 design.md 決策 9）
- [x] 1.7 Windows job：打包 —— `choco install nsis` 後執行 `cpack -G NSIS`、產生 portable ZIP、以 7z 打包 PDB。**不得**使用既有的 `pack-win-release` composite action（見 design.md 決策 7），改為內聯步驟或另建 education 專用的獨立 composite action
- [x] 1.8 Windows job：上傳 artifact，名稱需帶 education 識別，與主線 artifact 名稱不重複

## 2. 階段 1-B：macOS 建置腳本支援 resin 旗標

- [ ] 2.1 `build_release_macos.sh`：於 getopts 新增旗標（例如 `-r`），使 `build_slicer()` 的 cmake 參數列加上 `-DPHROZEN_ORCA_ENABLE_RESIN=ON`。修改須為**純新增式**，不重構既有結構（見 design.md 決策 8）
- [ ] 2.2 `build_release_macos.sh`：於腳本前段依旗標推導出 `APP_NAME` 變數（主線 `PhrozenOrca` ／ education `PhrozenOrca-Education`）
- [ ] 2.3 `build_release_macos.sh`：將「Fix macOS app package」段落（約 204-212 行）中寫死的 `PhrozenOrca.app` 改用 `APP_NAME` 變數
- [ ] 2.4 `build_release_macos.sh`：將 `build_universal()`（約 236-250 行）中的 `$PROJECT_BUILD_DIR/PhrozenOrca/PhrozenOrca.app` 與 `cp -R .../PhrozenOrca.app` 改用變數
- [ ] 2.5 `build_release_macos.sh`：將 `build_universal()` 中的 **`BINARY_PATH="Contents/MacOS/PhrozenOrca"`** 改用變數 —— 這是 `lipo` 的輸入檔路徑，未同步會直接找不到檔案而失敗
- [ ] 2.6 確認主線行為不變：不帶新旗標執行時，所有路徑與產出物皆與修改前完全相同（`APP_NAME` 預設為 `PhrozenOrca`）

## 3. 階段 1-C：macOS job（先不簽章）

- [ ] 3.1 macOS job：checkout ＋ deps 快取（education 專屬快取鍵；macOS 的 deps 路徑依 `build_release_macos.sh` 實際使用的位置設定）
- [ ] 3.2 macOS job：環境準備 —— `lukka/get-cmake@latest`、`brew install automake texinfo libtool`、`brew uninstall --ignore-dependencies zstd`（建置後再裝回）、釋放磁碟空間步驟（參考 `build_orca.yml` 的 `Free disk space`）
- [ ] 3.3 macOS job：deps 建置（僅快取未命中時）—— 呼叫 `build_release_macos.sh` 的 deps 模式並帶上 resin 旗標
- [ ] 3.4 macOS job：slicer 建置 —— 呼叫 `build_release_macos.sh` 帶 resin 旗標與 universal 架構參數；接著執行 `./scripts/run_gettext.sh`
- [ ] 3.5 macOS job：先走**不簽章**路徑產生 DMG（等同 `build_orca.yml` 的 `Create DMG without notary`），DMG 檔名須帶 education 識別與版本尾綴
- [ ] 3.6 macOS job：上傳 DMG artifact（education 命名）

## 4. 階段 1-D：加入簽章與公證（最貴，最後做）

- [ ] 4.1 macOS job：加入 codesign 步驟 —— 匯入 `MAC_CERTIFICATE_P12`、建立並解鎖 keychain、以 `MAC_CERTIFICATE_NAME` 簽署 `.app`，使用 `scripts/disable_validation.entitlements`、`--options runtime --timestamp`
- [ ] 4.2 macOS job：建立 `Applications` 符號連結後以 `hdiutil create` 產生 DMG，並對 DMG 本身簽章
- [ ] 4.3 macOS job：以 `xcrun notarytool store-credentials` 與 `submit --wait` 完成公證（使用 `APPLE_ID`、`TEAM_ID`、`APPLE_APP_PASSWORD`），再以 `xcrun stapler staple` 附加公證票據
- [ ] 4.4 確認所需 secrets 皆可取得：`MAC_CERTIFICATE_P12`、`MAC_CERTIFICATE_PASSWORD`、`KEYCHAIN_PASSWORD`、`MAC_CERTIFICATE_NAME`、`APPLE_ID`、`TEAM_ID`、`APPLE_APP_PASSWORD`

## 5. 漂移偵測機制

- [ ] 5.1 建立校驗值紀錄檔（建議 `.github/education-ci-parity.lock`），記錄 `build_orca.yml`、`build_deps.yml`、`build_check_cache.yml` 三個主線共用 CI 檔案的雜湊值，並附註「上次比對時主線的 commit」以利日後人工 diff
- [ ] 5.2 在 `build_education.yml` 中加入 parity check job：僅 checkout ＋ 計算雜湊並與紀錄檔比對，不執行任何編譯／deps／簽章
- [ ] 5.3 將 Windows 與 macOS 兩個建置 job 設為依賴 parity check job，確保**偵測失敗時昂貴的建置不會啟動**
- [ ] 5.4 失敗訊息需明確指出：哪個檔案變了、應該去比對什麼、以及確認後如何更新紀錄檔恢復綠燈（讓不熟悉此機制的人也能處理）
- [ ] 5.5 在紀錄檔或 workflow 註解中寫明更新流程

## 6. 階段 1 收尾

- [ ] 6.1 移除 `on:` 中開發期專用的整個 `push:` 區塊，只保留 `workflow_dispatch:`
- [ ] 6.2 完整跑一次驗證：確認移除開發期觸發後，一般 push 不會再觸發建置

## 7. 階段 2：讓手動按鈕出現（動主線，僅一個獨立檔案）

- [ ] 7.1 開 PR 將 `build_education.yml` **單獨**合入 `phrozen-custom-dev`（不夾帶任何其他檔案）
- [ ] 7.2 PR 描述須寫明三件事：①這是 resin/education 專用；②主線永遠不會自動觸發它；③存在目的是讓 Actions 頁面出現手動觸發按鈕，**請勿刪除**
- [ ] 7.3 確認此 PR 未觸發完整建置（`build_all.yml` 的 PR 條件只有 `main`／`release`，`phrozen-custom-dev` 不在其中）
- [ ] 7.4 合入後確認：主線不再修改此檔案，之後由 resin 支線自由演進

## 8. 階段 3：回歸 resin 主維護分支

- [ ] 8.1 merge `phrozen-education-variant` → `phrozen-resin-dev`
- [ ] 8.2 確認合併後 `.github/` 中的既有共用 CI 檔案（`build_orca.yml`、`build_deps.yml`、`build_check_cache.yml`、`build_all.yml`）與主線內容仍完全相同、未產生衝突
- [ ] 8.3 從 Actions UI 選 ref = `phrozen-resin-dev` 觸發一次，確認可正常建置

## 9. QA 手動驗收步驟（提交測試人員驗收用）

> 以下步驟涵蓋三個階段。標註「階段 N」者請在對應階段完成後執行。所有 workflow 執行記錄可在 GitHub repo 的 **Actions** 頁籤查看。

### 9.1 觸發機制驗收

- [ ] 9.1.1 **（階段 1）開發期分支觸發生效**：對 `phrozen-education-variant` 分支推送一個只修改 `.github/workflows/build_education.yml` 的 commit。
  - 開啟 GitHub repo → **Actions** 頁籤 → 左側選擇 `build_education`
  - 預期：出現一筆新的執行記錄，觸發來源顯示為 push

- [ ] 9.1.2 **（階段 1）paths 過濾生效**：對 `phrozen-education-variant` 分支推送一個**只修改 C++ 原始碼**（例如 `src/slic3r/GUI/` 底下任一檔案）的 commit。
  - 開啟 **Actions** → `build_education`
  - 預期：**不會**出現新的執行記錄（避免改程式碼時誤觸發昂貴建置）

- [ ] 9.1.3 **（階段 1 收尾後）開發期觸發已移除**：再次推送一個修改 `.github/workflows/build_education.yml` 的 commit。
  - 預期：**不再**自動觸發任何執行

- [ ] 9.1.4 **（階段 2）手動按鈕出現**：`build_education.yml` 合入 `phrozen-custom-dev` 後，開啟 **Actions** → 左側選擇 `build_education`。
  - 預期：右上角出現 **Run workflow** 按鈕（合入主線前不會出現）

- [ ] 9.1.5 **（階段 2）跨 ref 觸發正確**：點 **Run workflow** → 分支下拉選單選擇 `phrozen-education-variant` → 執行。
  - 預期：執行成功，且產出物為 education 變體（依 9.2／9.3 驗證）

- [ ] 9.1.6 **（階段 2）誤選主線分支會安全失敗**：點 **Run workflow** → 分支選擇 `phrozen-custom-dev` → 執行。
  - 預期：**快速失敗**（因主線沒有 `build_resin_release_vs2022.bat`），且 **artifact 區塊沒有任何產出物**
  - 重點確認：不可產出任何「看似 education 實為主線」的成品

- [ ] 9.1.7 **（階段 3）從 resin-dev 觸發正常**：merge 回 `phrozen-resin-dev` 後，**Run workflow** 選擇 `phrozen-resin-dev` → 執行。
  - 預期：執行成功，產出物與 9.1.5 一致

### 9.2 Windows 產出物驗收

- [ ] 9.2.1 **artifact 命名可區分**：在成功執行的頁面下方 **Artifacts** 區塊檢視清單。
  - 預期：各 artifact 名稱皆帶有 education 識別，與主線建置（`build_all` 的執行）的 artifact 名稱不重複

- [ ] 9.2.2 **安裝檔檔名**：下載 Windows 安裝檔 artifact 並解壓。
  - 預期：安裝檔檔名帶有 education 識別與版本尾綴（例如 `PhrozenOrca-Education_Windows_Installer_V1.2.0-Education.exe`），與主線同版本安裝檔可明確區分

- [ ] 9.2.3 **安裝後的識別隔離**：執行該安裝檔完成安裝。
  - 檔案總管網址列輸入 `%APPDATA%` → 預期出現 `PhrozenOrca-Education` 資料夾（不是 `PhrozenOrca`）
  - 安裝目錄底下的執行檔 → 預期為 `phrozen-orca-education.exe`
  - Windows「應用程式與功能」→ 預期出現獨立的 `PhrozenOrca-Education` 項目
  - 開啟軟體 →「說明」→「關於」→ 預期版本號顯示 `1.2.0-Education`

- [ ] 9.2.4 **portable ZIP 與 PDB**：確認 artifact 中同時有 portable ZIP 與 PDB 壓縮檔，且皆可正常解壓

- [ ] 9.2.5 **deps 快取行為**：開啟該次執行的 Windows job log，搜尋 cache 相關步驟。
  - 預期：快取鍵含 education 識別；還原路徑為 `deps/build-resin/PhrozenOrca_dep`
  - 重點確認：**沒有**出現「宣稱快取命中，但後續建置卻找不到 deps」的矛盾情形（這是快取鍵誤用主線的典型症狀）
  - 第二次觸發時應顯示快取命中，且 deps 建置步驟被跳過

### 9.3 macOS 產出物驗收

- [ ] 9.3.1 **DMG 檔名**：下載 macOS DMG artifact。
  - 預期：檔名帶有 education 識別與版本尾綴，與主線 DMG 可明確區分

- [ ] 9.3.2 **`.app` 名稱**：在 Mac 上掛載該 DMG。
  - 預期：其中的應用程式套件名稱為 `PhrozenOrca-Education.app`（**不是** `PhrozenOrca.app`）

- [ ] 9.3.3 **Bundle Identifier**：把 `.app` 拖到「應用程式」資料夾後，開啟「終端機」執行：
  ```
  mdls -name kMDItemCFBundleIdentifier /Applications/PhrozenOrca-Education.app
  ```
  - 預期：回傳 `com.phrozen3d.phrozen-orca-education`
  - 若主線版也已安裝，對其執行同樣指令 → 預期回傳 `com.phrozen3d.phrozen-orca`，兩者不同

- [ ] 9.3.4 **內部 binary 名稱**：終端機執行：
  ```
  ls /Applications/PhrozenOrca-Education.app/Contents/MacOS/
  ```
  - 預期：看到 `PhrozenOrca-Education`（與 `Info.plist` 的 `CFBundleExecutable` 一致）

- [ ] 9.3.5 **簽章驗證**：終端機執行：
  ```
  codesign -dv --verbose=4 /Applications/PhrozenOrca-Education.app
  ```
  - 預期：顯示有效的 Developer ID 簽章、`flags` 含 `runtime`（hardened runtime 已啟用）、有 timestamp

- [ ] 9.3.6 **公證驗證（Gatekeeper）**：終端機執行：
  ```
  spctl -a -vv /Applications/PhrozenOrca-Education.app
  ```
  - 預期：結果為 `accepted`、`source=Notarized Developer ID`
  - 補充驗證：在一台**未曾安裝過此 App 的 Mac** 上下載 DMG 並開啟，預期**不會**跳出「無法驗證開發者」的 Gatekeeper 阻擋訊息

- [ ] 9.3.7 **使用者資料夾隔離**（同時解決 `phrozen-education-variant-branding` 遺留的驗收項目）：啟動 education 版一次後，Finder 按 `Cmd+Shift+G` 輸入 `~/Library/Application Support/`。
  - 預期：出現 `PhrozenOrca-Education` 資料夾
  - 若主線版也安裝過 → 預期 `PhrozenOrca` 與 `PhrozenOrca-Education` 兩個資料夾並存、內容互不干擾
  - **重點確認**：此項用於驗證 macOS 上的使用者資料夾是否確實依 `SLIC3R_APP_KEY` 命名（見 design.md Open Questions）；若實際資料夾名稱不符預期，須記錄實際值並回報

- [ ] 9.3.8 **兩個版本可並存**：確認 `PhrozenOrca.app` 與 `PhrozenOrca-Education.app` 可同時存在於「應用程式」資料夾，安裝其中一個不會覆蓋另一個；刪除其中一個時另一個的檔案與資料夾都不受影響

### 9.4 漂移偵測機制驗收

- [ ] 9.4.1 **正常情況通過**：在未變更主線共用 CI 檔案的情況下觸發 education 建置。
  - 開啟該次執行 → 預期 parity check job **通過（綠燈）**，且建置 job 正常接續執行

- [ ] 9.4.2 **偵測到變更時失敗**：在 resin 支線上刻意修改 `.github/workflows/build_orca.yml` 任意一行（測試用，事後還原）後觸發 education 建置。
  - 預期：parity check job **失敗（紅燈）**，且失敗訊息明確指出是 `build_orca.yml` 發生變更
  - **重點確認**：Windows 與 macOS 兩個建置 job 皆顯示為 **skipped**，未被啟動（避免浪費昂貴的 macOS 額度）

- [ ] 9.4.3 **更新校驗值後恢復**：依失敗訊息的指示更新 `.github/education-ci-parity.lock` 後再次觸發。
  - 預期：parity check 通過，建置正常進行

- [ ] 9.4.4 **成本確認**：檢視 parity check job 的執行時間。
  - 預期：僅數十秒等級（只做 checkout 與雜湊計算），未執行任何編譯、deps 或簽章步驟

### 9.5 主線不受影響的回歸驗收

- [ ] 9.5.1 **主線共用 CI 檔案未被修改**：在 `phrozen-custom-dev` 上比對本 change 前後的 `.github/workflows/build_all.yml`、`build_check_cache.yml`、`build_deps.yml`、`build_orca.yml`、`.github/actions/pack-win-release/action.yml`、`.github/actions/pack-win-nightly/action.yml`、`build_release_vs2022.bat`、`build_release_macos.sh`。
  - 預期：以上檔案內容**完全未被修改**；主線上唯一的新增是 `.github/workflows/build_education.yml` 這一個獨立檔案

- [ ] 9.5.2 **主線既有建置行為不變**：在主線觸發一次既有的完整建置（`build_all`）。
  - 預期：執行成功，產出物名稱與內容與本 change 實施前完全相同，**不含任何 `-Education`／`-education` 字樣**

- [ ] 9.5.3 **主線排程建置不受影響**：確認 `windows_nightly_dev`（每日）與 `macos_weekly_dev`（每週五）的排程執行仍正常，且建置的仍是 `phrozen-custom-dev` 的內容

- [ ] 9.5.4 **主線不會自動觸發 education 建置**：對 `phrozen-custom-dev` 推送任意變更。
  - 預期：`build_education` **不會**出現任何自動執行記錄

- [ ] 9.5.5 **（階段 3 後）merge 無衝突**：檢視 `phrozen-education-variant` → `phrozen-resin-dev` 的合併結果。
  - 預期：`.github/` 中的既有共用 CI 檔案未產生任何衝突，且合併後其內容與主線仍完全相同
