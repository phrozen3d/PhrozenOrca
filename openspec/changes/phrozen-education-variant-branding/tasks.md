## 1. version.inc 與 CMake 尾綴注入邏輯

- [ ] 1.1 修改 `version.inc`：把 `Phrozen_VERSION` 拆成不含尾綴的 base 版本號（例如 `"1.2.0"`），移除目前寫死的 `"-Education"` 字樣。
- [ ] 1.2 在根目錄 `CMakeLists.txt` 的 `option(PHROZEN_ORCA_ENABLE_RESIN ...)`（~142行）之後，新增 `if (PHROZEN_ORCA_ENABLE_RESIN)` 區塊：
  - 覆寫 `SLIC3R_APP_NAME` → append `"-Education"`
  - 覆寫 `SLIC3R_APP_KEY` → append `"-Education"`
  - 覆寫/組出最終 `Phrozen_VERSION` 顯示字串 → append `"-Education"`（注意：僅影響顯示字串，`PHROZEN_ORCA_VERSION_MAJOR/MINOR/PATCH` 的 regex 解析必須在 append 尾綴之前，使用 base 版本號解析）
- [ ] 1.3 在同一個 `CMakeLists.txt` 區塊內，依旗標覆寫以下 CPack/NSIS 變數：`CPACK_PACKAGE_NAME`（~889）、`CPACK_PACKAGE_FILE_NAME`（~894）、`CPACK_PACKAGE_INSTALL_REGISTRY_KEY`（~906）、`CPACK_PACKAGE_EXECUTABLES`/`CPACK_CREATE_DESKTOP_LINKS`（~910-911）、`CPACK_NSIS_INSTALLED_ICON_NAME`/`CPACK_NSIS_EXTRA_INSTALL_COMMANDS` 內的 `phrozen-orca.exe`/`PhrozenOrca.lnk` 字串（~901-904）。

## 2. 執行檔實體改名與連動修正

- [ ] 2.1 `src/CMakeLists.txt:123,124,177`：`OUTPUT_NAME "phrozen-orca"` 依旗標改為 `"phrozen-orca-education"`。
- [ ] 2.2 `src/CMakeLists.txt:225`：Linux `ln -sf PhrozenOrca phrozen-orca` symlink 目標同步改名。
- [ ] 2.3 `src/slic3r/Utils/NetworkAgent.cpp:214-217`（`get_libpath_in_current_directory`）：把比對字串 `"phrozen-orca.exe"` 與寫死的長度常數 16 依旗標同步改為 `"phrozen-orca-education.exe"`/對應長度。
- [ ] 2.4 `src/dev-utils/platform/msw/PhrozenOrca.rc.in:16`：`OriginalFilename` 依旗標同步改為 `"phrozen-orca-education.exe"`。

## 3. 視窗標題與 macOS 套件識別

- [ ] 3.1 `src/slic3r/GUI/Plater.cpp:8539`：把硬編碼字面量 `" - PhrozenOrca"` 改為 `" - " + SLIC3R_APP_NAME`（使用變數，不另外寫條件字面量）。
- [ ] 3.2 `src/dev-utils/platform/osx/Info.plist.in`：`CFBundleIdentifier`（`com.phrozen3d.phrozen-orca`）與 URL scheme 依旗標加上 education 對應的分流值。

## 4. QA 手動驗收步驟（提交測試人員驗收用）

> 以下每一項都需要**分別建置兩次**：一次用 `build_release_vs2022.bat`（主線版，flag OFF），一次用 `build_resin_release_vs2022.bat`（resin/education 版，flag ON）。每一項都要在兩個版本上各檢查一次，確認「主線版完全看不到任何 "-Education"/"-education" 字樣」且「resin/education 版每一處都看得到」。

- [ ] 4.1 **版本號字串**：開啟軟體 → 左上角選單「說明 (Help)」→「關於 PhrozenOrca (About)」，觀看版本號欄位。
  - 主線版預期顯示：`1.2.0`（不含任何尾綴）
  - resin/education 版預期顯示：`1.2.0-Education`

- [ ] 4.2 **AppData 使用者資料夾名稱**：開啟 Windows 檔案總管，網址列輸入 `%APPDATA%` 並按 Enter，觀察資料夾清單。
  - 主線版：應存在名為 `PhrozenOrca` 的資料夾
  - resin/education 版：應存在名為 `PhrozenOrca-Education` 的資料夾（且**不會**跟主線版的 `PhrozenOrca` 資料夾混用同一個，兩者可同時存在、內容互不影響）
  - 額外檢查：分別開啟兩個資料夾內的設定檔（`.ini`/`.conf`），確認各自版本存的 profile/參數沒有互相覆蓋

- [ ] 4.3 **安裝目錄與登錄機碼隔離**：依序安裝主線版與 resin/education 版的 Windows 安裝程式（NSIS `.exe`）。
  - 安裝完成後，開啟「控制台」→「程式和功能」（或 Windows 設定 →「應用程式」），確認清單中出現**兩個獨立的項目**（例如 "PhrozenOrca" 與 "PhrozenOrca-Education"），而不是只有一個、或後安裝的把先安裝的覆蓋掉
  - 開啟 `regedit`，導覽至 `HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Uninstall\`（或 `HKEY_LOCAL_MACHINE` 對應路徑，視安裝模式而定），確認存在兩個不同名稱的解除安裝機碼
  - 分別確認兩者的預設安裝路徑不同（例如 `C:\Program Files\PhrozenOrca\` 與 `C:\Program Files\PhrozenOrca-Education\`）
  - 解除安裝其中一個版本，確認另一個版本的檔案與捷徑**沒有**被一併移除

- [ ] 4.4 **執行檔名稱與工作管理員程序識別**：分別在安裝目錄底下找到執行檔。
  - 主線版執行檔應為 `phrozen-orca.exe`
  - resin/education 版執行檔應為 `phrozen-orca-education.exe`
  - 同時執行兩個版本，開啟工作管理員（Ctrl+Shift+Esc）→「詳細資料」頁籤，確認可以用「名稱」欄位直接分辨兩個程序（分別顯示 `phrozen-orca.exe` 與 `phrozen-orca-education.exe`），不會顯示成同名的兩個程序
  - 在檔案總管中對 `phrozen-orca-education.exe` 按右鍵 →「內容」→「詳細資料」頁籤，確認「原始檔名 (Original file name)」欄位顯示 `phrozen-orca-education.exe`

- [ ] 4.5 **網路外掛 DLL 路徑推算（NetworkAgent.cpp 連動）**：此項為技術性驗證，若相關網路連線功能在 PhrozenOrca 中本來就不可觸發，可退而確認程式碼路徑一致即可（見 design.md 的 Open Questions）。
  - 若可觸發：在 resin/education 版中開啟任何會用到雲端裝置連線/攝影機串流的功能，觀察功能是否正常運作（不應出現「找不到外掛/DLL 載入失敗」相關錯誤）
  - 若要用 log 驗證：於軟體資料夾內找到執行 log（一般在 AppData 資料夾的 log 子目錄），搜尋是否有 `can not Load Library` 或 `can not get path in current directory` 等訊息 —— 不應出現

- [ ] 4.6 **主視窗標題**：分別開啟兩個版本，新建或開啟任一專案檔，觀察視窗最上方標題列文字。
  - 主線版標題列應顯示類似：`未命名 - PhrozenOrca`（不含 "-Education"）
  - resin/education 版標題列應顯示類似：`未命名 - PhrozenOrca-Education`

- [ ] 4.7 **單一實例偵測（Windows）**：在 resin/education 版已開啟一個視窗的情況下，再次點擊該版本的執行檔或桌面捷徑啟動它。
  - 預期結果：不會開啟第二個視窗，而是把已經開啟的視窗帶到前景（正常的「已有實例在執行」行為）；同時對主線版重複這個測試，確認主線版的單一實例偵測也正常，且**兩個版本可以同時各自開一個視窗**，互不干擾、互不誤判成同一個實例

- [ ] 4.8 **macOS 套件識別**（若有 macOS 建置環境可測）：分別安裝兩個版本的 `.app` 到「應用程式」資料夾。
  - 開啟「終端機」，執行 `mdls -name kMDItemCFBundleIdentifier /Applications/PhrozenOrca.app` 與對應 resin/education 版路徑，確認兩者回傳不同的 Bundle Identifier
  - 確認兩個 `.app` 可以同時存在於「應用程式」資料夾，安裝其中一個不會覆蓋另一個
  - 若軟體有自訂 URL scheme 功能，可在瀏覽器網址列測試對應的 `phrozenorca://`/education 版 scheme 連結，確認各自開啟正確的對應版本

- [ ] 4.9 **回歸測試：主線版完全不受影響**：對照本次修改的每一項（4.1–4.8），在**只有主線版**單獨安裝（未安裝 resin/education 版）的乾淨環境下重跑一次，確認所有識別字串、資料夾、執行檔名稱、安裝機碼都與修改前完全一致，沒有任何 "-Education"/"-education" 字樣意外出現。
