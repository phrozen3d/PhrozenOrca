## Context

PhrozenOrca 目前有兩個以 CMake 選項 `PHROZEN_ORCA_ENABLE_RESIN`（`CMakeLists.txt` ~142行，預設 `OFF`）區分的 build 變體：

- **主線 FDM 版**：`build_release_vs2022.bat`，flag `OFF`。
- **resin/FDM 混合版（education）**：`build_resin_release_vs2022.bat`，flag `ON`，已使用獨立的 `deps/build-resin` 建置目錄與 `CMAKE_INSTALL_PREFIX="./PhrozenOrcaResin"` 本地輸出路徑。

這個旗標原本只用來關閉 resin 版的 app 更新檢查（見已封存變更 `2026-07-23-phrozen-disable-resin-update-check`），從未用來區分兩個變體的品牌識別字串。目前 `SLIC3R_APP_NAME`/`SLIC3R_APP_KEY`（`version.inc`）、執行檔名稱（`src/CMakeLists.txt`）、CPack/NSIS 安裝設定（根目錄 `CMakeLists.txt`）、視窗標題（`Plater.cpp`）在兩個變體之間完全相同，導致 AppData 資料夾、安裝目錄、登錄機碼互相覆蓋。

稽核時另外發現 `version.inc:13` 現在直接寫死 `Phrozen_VERSION = "1.2.0-Education"`，且完全不受任何旗標保護——這代表目前這份原始碼若拿去跑主線版建置腳本，也會產出帶有 "-Education" 字樣的版本號，是一個尚未被正確隔離、正好由本次變更一併修正的既有缺陷。

## Goals / Non-Goals

**Goals:**
- 當 `PHROZEN_ORCA_ENABLE_RESIN=ON` 時，所有使用者可見與作業系統可見的識別字串（AppData 資料夾名稱、執行檔實體檔名、安裝目錄、Windows 登錄機碼、主視窗標題/工作列名稱、macOS Bundle ID）都自動帶有尾綴：字串開頭為大寫則用 "-Education"，開頭為小寫則用 "-education"。
- 當旗標為 `OFF`（主線版）時，上述所有識別字串必須與目前行為**逐位元組相同**，不能有任何副作用。
- 執行檔實體檔名確實從 `phrozen-orca.exe` 改名為 `phrozen-orca-education.exe`（僅限 resin 版），因此所有依賴這個字面量的下游程式碼（`NetworkAgent.cpp` 的路徑推算、`.rc` 的 `OriginalFilename`）都要同步更新。
- 修正 `version.inc` 目前未受旗標保護、永遠帶 "-Education" 尾綴的既有缺陷，讓主線版重新取得乾淨的版本號。
- 盡量把「單一事實來源」原則落實到位：能靠變數（`SLIC3R_APP_NAME`/`SLIC3R_APP_KEY`）自動繼承尾綴的地方，不要另外手動寫死字面量（例如 `Plater.cpp` 的視窗標題改用變數，而不是另外加一行條件字串）。

**Non-Goals:**
- 不處理 "orcaslicer"/"OrcaSlicer" 品牌殘留字串問題（ProgID `Orca.Slicer.1`、`orcaslicer://` URL scheme、Linux `DesktopIntegrationDialog.cpp` 寫出的 `OrcaSlicer*.desktop`、`GCODEVIEWER_APP_NAME/KEY` 等）——這些已規劃為獨立的後續變更 `fix-orcaslicer-leftover-paths`。
- 不做任何 compile-time feature gating（哪些 SLA 參數/UI 在哪個變體要不要出現）——那是 `phrozen-build-variant-resin-split` 的範圍，本次不預先設計配合它的抽象層。
- 不修改 `BUILD_PHROZEN_ORCA`、`PhrozenConnect`、`PartPlateList`、`IMToolbar`、`BBS_TOOLBAR_ON_TOP` 或任何 `#ifdef BUILD_PHROZEN_ORCA` 保護的程式碼。
- 不改變任何 FDM-only 的參數、UI 或邏輯。
- 不處理 Linux D-Bus 匯流排名稱、macOS distributed notification 名稱裡 "OrcaSlicer" 字樣本身的品牌修正（那屬於 Non-Goals 第一點所述的後續變更）；本次只驗證這兩處的「單一實例互不干擾」在 exe 改名後是否仍然成立，不修改其字面量內容（除非驗證發現真的會撞名，才視為本次範圍內的必要修復）。

## Decisions

### 1. 尾綴注入點：在 `PHROZEN_ORCA_ENABLE_RESIN` 選項宣告後，覆寫 `version.inc` 設下的變數

**決策**：不修改 `version.inc` 本身的 include 順序，而是在根目錄 `CMakeLists.txt` 宣告 `option(PHROZEN_ORCA_ENABLE_RESIN ...)`（~142行）之後，新增一段 `if (PHROZEN_ORCA_ENABLE_RESIN)` 區塊，依當時字串開頭大小寫規則覆寫 `SLIC3R_APP_NAME`、`SLIC3R_APP_KEY`，並將 `version.inc` 的 `Phrozen_VERSION` 拆成不含尾綴的 base 版本號，另由此區塊決定是否 append "-Education"。

**替代方案考慮**：
- 直接在 `version.inc` 裡用 `if(DEFINED PHROZEN_ORCA_ENABLE_RESIN)` 判斷 —— 放棄，因為 `version.inc` 在 `option()` 宣告之前就被 include（`CMakeLists.txt:58` vs ~142行），此時旗標變數還不存在，判斷會失效或依賴 CMake 的隱式行為，不夠明確。
- 在每個使用 `SLIC3R_APP_NAME`/`SLIC3R_APP_KEY` 的地方individually 判斷旗標 —— 放棄，因為下游用到這兩個變數的地方分散在 GUI_App.cpp、AppConfig.cpp、.rc.in、.sh.in 等多處，個別判斷等於重複邏輯、容易漏改；在源頭變數覆寫一次，所有下游全部自動繼承，維護成本最低。

### 2. 執行檔實體改名，而非只做外顯包裝

**決策**：`phrozen-orca.exe` 在 resin 版真的改名為 `phrozen-orca-education.exe`（透過 `src/CMakeLists.txt` 的 `OUTPUT_NAME` 依旗標決定），而不是保留原檔名、只在安裝目錄/捷徑標籤上做區分。

**理由**：使用者/工作管理員可以直接用程序名稱分辨兩個變體在跑哪一個，不需要額外去看路徑；此決策已與專案負責人確認為明確需求。

**代價**：所有依賴這個檔名字面量的程式碼都要同步更新，具體是 `NetworkAgent.cpp:214-217`（`get_libpath_in_current_directory`，用字串比對加寫死長度 16 從目前執行檔路徑推算網路外掛 DLL 位置）與 `PhrozenOrca.rc.in` 的 `OriginalFilename` 欄位。這兩處都不是 `BUILD_PHROZEN_ORCA` 保護的程式碼，屬於本次變更的合法範圍。

### 3. 視窗標題改用 `SLIC3R_APP_NAME` 變數，而非另外加條件式字面量

**決策**：`Plater.cpp` 目前主視窗標題是 `m_project_name + " - PhrozenOrca"`（硬編碼字面量），本次改為 `m_project_name + " - " + SLIC3R_APP_NAME`。

**替代方案考慮**：只在這一行外面包一層 `#ifdef PHROZEN_ORCA_ENABLE_RESIN` 條件字串 —— 放棄，因為這樣以後任何品牌名稱調整都要記得同時改兩個地方（CMake 變數 + 這一行字面量），過去這個字面量沒有跟著 `SLIC3R_APP_NAME` 走本身就是問題根源之一；改用變數後只有一個真相來源。

**連動驗證**：`InstanceCheck.cpp:108` 用 `find(L"PhrozenOrca")` 對其他執行中視窗標題做子字串比對來判斷是否已有實例執行。新標題（無論是 "...-PhrozenOrca" 或 "...-PhrozenOrca-Education"）都仍包含 "PhrozenOrca" 子字串，理論上比對邏輯不會壞，但這是「理論上」，必須列入 tasks.md 的手動驗證項目，不能只靠程式碼推論。

### 4. Linux D-Bus / macOS 通知名稱：驗證優先於修改

**決策**：`InstanceCheck.cpp` 的 Linux D-Bus 匯流排/物件名稱與 `InstanceCheckMac.mm` 的 macOS distributed notification 名稱，其實際分流依據是「目前執行檔完整路徑」算出的 hash（`instance_hash`/`version` 後綴），並非單純比對品牌字面量本身。exe 改名後路徑本身就不同，hash 自然分流。本次不預先修改這兩處的字面量內容，而是在 tasks.md 中列入手動驗證步驟；只有在驗證中真的發現撞名或异常時，才視為本次範圍內必須修復的缺陷。

**理由**：避免對這兩處程式碼做不必要的改動（它們的品牌字本身"OrcaSlicer"是 Non-Goal 範圍要處理的問題），保持本次變更的最小侵入性。

## Risks / Trade-offs

- **[風險] exe 改名後 NetworkAgent.cpp 的字串比對若漏改，resin 版的網路外掛 DLL 會載入失敗，且此問題不會在編譯期被發現，只會在執行期靜默失敗（`get_libpath_in_current_directory` 找不到就回傳空字串，呼叫端會 log 一行 info 訊息後繼續，不會顯式報錯）。** → Mitigation：在 tasks.md 明確列出這處修改與對應的手動驗證步驟（檢查 log 或觀察相關網路連線功能是否正常）。
- **[風險] `version.inc` 拆分 base 版本號的修改若寫錯 regex，可能影響 `PHROZEN_ORCA_VERSION_MAJOR/MINOR/PATCH` 的解析（下游多處用於 CPack 版本號、`MACOSX_BUNDLE_SHORT_VERSION_STRING`、Semver 解析比較等）。** → Mitigation：確保 base 版本號（不含尾綴）才是拿去做 `REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)"` 解析的來源，尾綴只在組出最終顯示字串時才 append，不影響數字部分的解析。
- **[風險] 主線版（flag OFF）行為意外跑掉** —— 例如條件寫反、或某處忘記包在 `if` 判斷內，導致主線版也混入 "-Education" 字樣，或反過來 resin 版沒有正確帶尾綴。 → Mitigation：tasks.md 的每一項修改都同時要求「驗證主線版完全不受影響」與「驗證 resin/education 版每處都帶有尾綴」兩種情境，QA 驗收時兩個變體都要各建置一次比對。
- **[Trade-off，已知並接受] BREAKING for resin/education 使用者**：既有已安裝的 resin 版使用者，升級到本次變更後的新建置，會因為 AppData 資料夾名稱、安裝目錄、登錄機碼都改變而被視為全新安裝，需要重新匯入 profile。此為必要代價（正是本次變更要解決的資料夾誤用問題的另一面），主線 FDM 版使用者完全不受影響。

## Migration Plan

1. 修改 `version.inc` + 根目錄 `CMakeLists.txt`（尾綴注入邏輯）——一次性 CMake 層變更，兩個變體共用同一份原始碼庫，差異只在建置時的旗標。
2. 修改 `src/CMakeLists.txt`（`OUTPUT_NAME`、Linux symlink）、`NetworkAgent.cpp`、`PhrozenOrca.rc.in`、`Info.plist.in`、`Plater.cpp`。
3. 分別用 `build_release_vs2022.bat` 與 `build_resin_release_vs2022.bat` 各建置一次，依 tasks.md 的手動驗證步驟逐項比對兩個變體的輸出。
4. 沒有伺服器端/資料庫遷移，純屬用戶端安裝檔案結構變更；不需要額外的 rollback 機制 —— 若驗證失敗，直接修正原始碼重新建置即可，不涉及線上服務。
5. 通知 resin/education 版的既有使用者（若有）：下次更新為全新安裝，需要重新匯入 profile 設定（此為現有溝通流程的一部分，非本次變更的技術範圍）。

## Open Questions

- `NetworkAgent.cpp` 的 `BambuNetwork.dll`/`BambuSource.dll` 外掛機制是否實際被 PhrozenOrca 的正式發布安裝檔打包/使用（或只是 OrcaSlicer 上游繼承下來、目前未啟用的死代碼）——不影響本次修改是否要做（字面量同步是必要的），但會影響驗證時「是否能實際觀察到网络外掛功能受影響」的驗收方式，建議 QA 驗證時留意此點，若該功能在 PhrozenOrca 中本來就無法觸發，該項驗證可改為「確認程式碼路徑一致，不強求可觀察的功能表現」。
