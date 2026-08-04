## 1. version.inc 與 CMake 尾綴注入邏輯

- [x] 1.1 修改 `version.inc`：把 `Phrozen_VERSION` 拆成不含尾綴的 base 版本號（例如 `"1.2.0"`），移除目前寫死的 `"-Education"` 字樣。
- [x] 1.2 在根目錄 `CMakeLists.txt` 的 `option(PHROZEN_ORCA_ENABLE_RESIN ...)`（~142行）之後，新增 `if (PHROZEN_ORCA_ENABLE_RESIN)` 區塊：
  - 覆寫 `SLIC3R_APP_NAME` → append `"-Education"`
  - 覆寫 `SLIC3R_APP_KEY` → append `"-Education"`
  - 覆寫/組出最終 `Phrozen_VERSION` 顯示字串 → append `"-Education"`（注意：僅影響顯示字串，`PHROZEN_ORCA_VERSION_MAJOR/MINOR/PATCH` 的 regex 解析必須在 append 尾綴之前，使用 base 版本號解析）
- [x] 1.3 在同一個 `CMakeLists.txt` 區塊內，依旗標覆寫以下 CPack/NSIS 變數：`CPACK_PACKAGE_NAME`（~889）、`CPACK_PACKAGE_FILE_NAME`（~894）、`CPACK_PACKAGE_INSTALL_REGISTRY_KEY`（~906）、`CPACK_PACKAGE_EXECUTABLES`/`CPACK_CREATE_DESKTOP_LINKS`（~910-911）、`CPACK_NSIS_INSTALLED_ICON_NAME`/`CPACK_NSIS_EXTRA_INSTALL_COMMANDS` 內的 `phrozen-orca.exe`/`PhrozenOrca.lnk` 字串（~901-904）。

## 2. 執行檔實體改名與連動修正

- [x] 2.1 `src/CMakeLists.txt:123,124,177`：`OUTPUT_NAME "phrozen-orca"` 依旗標改為 `"phrozen-orca-education"`。
- [x] 2.2 `src/CMakeLists.txt:225`：Linux `ln -sf PhrozenOrca phrozen-orca` symlink 目標同步改名。
- [x] 2.3 `src/slic3r/Utils/NetworkAgent.cpp:214-217`（`get_libpath_in_current_directory`）：把比對字串 `"phrozen-orca.exe"` 與寫死的長度常數 16 依旗標同步改為 `"phrozen-orca-education.exe"`/對應長度。
- [x] 2.4 `src/dev-utils/platform/msw/PhrozenOrca.rc.in:16`：`OriginalFilename` 依旗標同步改為 `"phrozen-orca-education.exe"`。

## 3. 視窗標題與 macOS 套件識別

- [x] 3.1 `src/slic3r/GUI/Plater.cpp:8539`：把硬編碼字面量 `" - PhrozenOrca"` 改為 `" - " + SLIC3R_APP_NAME`（使用變數，不另外寫條件字面量）。
- [x] 3.2 `src/dev-utils/platform/osx/Info.plist.in`：`CFBundleIdentifier`（`com.phrozen3d.phrozen-orca`）與 URL scheme 依旗標加上 education 對應的分流值。
- [x] 3.3 **（規劃階段遺漏，2026-07-30 補做）`.app` 套件資料夾名稱**：`src/CMakeLists.txt` 的 `PhrozenOrca` target 在 `CMAKE_MACOSX_BUNDLE=ON`（`build_release_macos.sh` 實際使用的模式）下完全沒有設定 `OUTPUT_NAME`，導致不論哪個變體，`.app` 資料夾都寫死叫 `PhrozenOrca.app`——兩個版本裝到 `/Applications/` 會直接覆蓋。修正：
  - `src/CMakeLists.txt`：新增 `if (APPLE AND CMAKE_MACOSX_BUNDLE)` 區塊，`OUTPUT_NAME` 依旗標改為 `${PHROZEN_ORCA_EXE_BASENAME}`（resin 版即 `phrozen-orca-education`）
  - `src/CMakeLists.txt` 的 `BIN_RESOURCES_DIR`（原本寫死 `.../PhrozenOrca.app/Contents/Resources`）同步改用 `${PHROZEN_ORCA_EXE_BASENAME}.app`，否則建置期的資源 symlink 會指向錯誤（不存在）的路徑
  - 根目錄 `CMakeLists.txt` 的 `install(DIRECTORY ... DESTINATION "${CMAKE_INSTALL_PREFIX}/PhrozenOrca.app/...")`（`CMAKE_MACOSX_BUNDLE` 安裝分支）同步改用 `${PHROZEN_ORCA_EXE_BASENAME}.app`
  - **注意**：`MACOSX_BUNDLE_BUNDLE_NAME "PhrozenOrca"`（`src/CMakeLists.txt` ~252行）確認是死變數（沒有設定 `MACOSX_BUNDLE_INFO_PLIST` target property，這個專案用自己的 `Info.plist.in`/`configure_file` 機制，不吃 CMake 內建的 bundle-info-plist 樣板），故意不修改，避免多餘變更

## 4. QA 手動驗收步驟（提交測試人員驗收用）

> 以下每一項都需要**分別建置兩次**：一次用 `build_release_vs2022.bat`（主線版，flag OFF），一次用 `build_resin_release_vs2022.bat`（resin/education 版，flag ON）。每一項都要在兩個版本上各檢查一次，確認「主線版完全看不到任何 "-Education"/"-education" 字樣」且「resin/education 版每一處都看得到」。

- [x] 4.1 **版本號字串**：開啟軟體 → 左上角選單「說明 (Help)」→「關於 PhrozenOrca (About)」，觀看版本號欄位。
  - 主線版預期顯示：`1.2.0`（不含任何尾綴）
  - resin/education 版預期顯示：`1.2.0-Education`
  - **Pass**（2026-07-30 實測確認）

- [x] 4.2 **AppData 使用者資料夾名稱**：開啟 Windows 檔案總管，網址列輸入 `%APPDATA%` 並按 Enter，觀察資料夾清單。
  - 主線版：應存在名為 `PhrozenOrca` 的資料夾
  - resin/education 版：應存在名為 `PhrozenOrca-Education` 的資料夾（且**不會**跟主線版的 `PhrozenOrca` 資料夾混用同一個，兩者可同時存在、內容互不影響）
  - 額外檢查：分別開啟兩個資料夾內的設定檔（`.ini`/`.conf`），確認各自版本存的 profile/參數沒有互相覆蓋
  - **Pass**（2026-07-30 實測確認）

- [x] 4.3 **安裝目錄與登錄機碼隔離**：依序安裝主線版與 resin/education 版的 Windows 安裝程式（NSIS `.exe`）。
  - 安裝完成後，開啟「應用程式與功能」(Apps & Features)，確認清單中出現**兩個獨立的項目**（"PhrozenOrca" 與 "PhrozenOrca-Education"）
  - **驗證方式更正**：手動在 `regedit` 裡導覽 `Uninstall` 機碼容易因為子機碼名稱/hive（HKLM vs HKCU，視安裝時是否觸發 UAC elevation 而定）找錯位置而誤判「找不到」；改用「應用程式與功能」(Apps & Features) 確認即可，該介面會自動彙整 HKLM/HKCU 兩處，較不易誤判
  - 分別確認兩者的預設安裝路徑不同（例如 `C:\Program Files\PhrozenOrca\` 與 `C:\Program Files\PhrozenOrca-Education\`）
  - **Pass**（2026-07-30 實測：主線版與 education 版皆各自出現獨立項目，安裝機碼隔離正確生效；解除安裝其中一個版本，確認另一個版本的檔案與捷徑沒有被一併移除，此子項亦已實測確認）

- [x] 4.4 **執行檔名稱與工作管理員程序識別**：分別在安裝目錄底下找到執行檔。
  - 主線版執行檔應為 `phrozen-orca.exe`
  - resin/education 版執行檔應為 `phrozen-orca-education.exe`
  - 同時執行兩個版本，開啟工作管理員（Ctrl+Shift+Esc）→「詳細資料」頁籤，確認可以用「名稱」欄位直接分辨兩個程序
  - 在檔案總管中對 `phrozen-orca-education.exe` 按右鍵 →「內容」→「詳細資料」頁籤，確認「原始檔名 (Original file name)」欄位顯示 `phrozen-orca-education.exe`
  - **Pass**（2026-07-30 實測確認）

- [x] ~~4.5 網路外掛 DLL 路徑推算（NetworkAgent.cpp 連動）~~ —— **標記為不適用（N/A），design.md Open Question 已解答**
  - 實測發現：兩個版本的 `%APPDATA%\<PhrozenOrca|PhrozenOrca-Education>\plugins\` 底下都沒有 `BambuNetwork.dll`；且 PhrozenOrca 產品定位本來就不支援 Bambu 品牌印表機，邏輯上這個外掛從未也不會被用到
  - **log 驗證方法的限制**：`can not Load Library`/`can not get path in current directory` 都是用 `BOOST_LOG_TRIVIAL(info)` 記錄，但預設 log 等級不含 `[info]`（實測的 debug log 只有 `[warning]`/`[error]`），這兩行訊息不論觸發與否都不會出現在預設 log 裡，此驗證方法本身不可靠，不建議未來沿用
  - **結論**：這條程式碼路徑在 PhrozenOrca 實際產品情境下是死路徑，不會被觸發，因此無法（也不需要）觀察到功能面差異；`NetworkAgent.cpp` 的字串同步修改在邏輯上仍然正確、必要（避免未來若重新啟用此外掛時因忘記同步而失效），但本次驗收不強求可觀察的功能表現

- [x] 4.6 **主視窗標題**：改用 Alt+Tab 切換器或工作列圖示 tooltip 確認，而非畫面上的自訂頂部工具列文字。
  - **重要澄清**：`Plater.cpp:8539` 改的是 wxFrame **原生（OS 層級）視窗標題**，用於 Alt+Tab 切換器與工作列 tooltip；畫面上看到的「未命名」是 `BBLTopbar`（`BBS_TOOLBAR_ON_TOP`，CLAUDE.md 明文保護的 PhrozenOrca 客製化區塊）畫出來的文字，該處只顯示專案名稱、不含品牌字樣，這是既有行為、與本次修改無關，**不應修改**
  - 驗證方式：按 `Alt+Tab` 或滑鼠停留在工作列圖示上，確認 tooltip/切換器顯示 `未命名 - PhrozenOrca-Education`（education 版）或 `未命名 - PhrozenOrca`（主線版）
  - **Pass**（2026-07-30 實測：以 Alt+Tab/工作列 tooltip 方式確認生效）

- [x] ~~4.7 單一實例偵測（Windows）~~ —— **標記為不適用（N/A），超出本次範圍**
  - 實測發現：目前不論主線版或 education 版，都可以重複開啟同一版本（沒有真的擋下第二個實例）。這是繼承自 OrcaSlicer base 的既有缺口，與本次 education 尾綴修改無關 —— 本次修改沒有改動、也沒有破壞這個機制原本的運作方式（見 design.md 決策4：驗證確認 hash 分流邏輯不受影響）。既然這個機制本來就沒有實際作用，就不存在「兩版本互相誤判成同一實例」的風險，此項對本次 change 而言不適用。若要修「單一實例偵測完全沒作用」，屬於另一個獨立的既有缺陷，不在本次範圍內。

- [ ] 4.8 **macOS 套件識別**（若有 macOS 建置環境可測）：分別安裝兩個版本的 `.app` 到「應用程式」資料夾。
  - 開啟「終端機」，執行 `mdls -name kMDItemCFBundleIdentifier /Applications/PhrozenOrca.app` 與對應 resin/education 版路徑，確認兩者回傳不同的 Bundle Identifier
  - 確認兩個 `.app` 可以同時存在於「應用程式」資料夾，安裝其中一個不會覆蓋另一個
  - 若軟體有自訂 URL scheme 功能，可在瀏覽器網址列測試對應的 `phrozenorca://`/education 版 scheme 連結，確認各自開啟正確的對應版本

- [ ] 4.9 **回歸測試：主線版完全不受影響**：對照本次修改的每一項（4.1–4.8），在**只有主線版**單獨安裝（未安裝 resin/education 版）的乾淨環境下重跑一次，確認所有識別字串、資料夾、執行檔名稱、安裝機碼都與修改前完全一致，沒有任何 "-Education"/"-education" 字樣意外出現。
  - 部分確認（2026-07-30）：以獨立的 `build\PhrozenOrca\` 建置（與 `build-resin\` 完全分開的目錄）實測，log 顯示資源路徑為 `C:\dev\PrusaSlicer\PhrozenOrca\build\PhrozenOrca\resources\...`、AppData 資料夾為乾淨的 `PhrozenOrca`（無尾綴），4.1/4.2/4.3/4.4/4.6 皆已對主線版實測 Pass。尚待補：4.8（無 macOS 環境）跑完後的完整回歸比對。
