## Why

PhrozenOrca 的 resin/FDM 混合版（`PHROZEN_ORCA_ENABLE_RESIN=ON`，透過 `build_resin_release_vs2022.bat` 建置）與主線 FDM 版（`build_release_vs2022.bat`，flag `OFF`）目前共用完全相同的品牌識別字串：AppData profile 資料夾名稱、執行檔名稱、安裝目錄、Windows 登錄檔安裝機碼、視窗標題/工作列名稱等。這造成三個實際問題：

1. 使用者先後安裝兩個版本會互相覆蓋（同一個安裝目錄、同一個登錄檔 uninstall key）。
2. AppData 裡的 profile/設定檔會被誤用（兩版本讀寫同一個資料夾），造成參數顯示錯亂。
3. 工作管理員/工作列中兩個版本的程序無法用名稱分辨，容易讓使用者或客服搞混手上開的是哪一版。

此外，稽核過程中發現 `version.inc` 目前把 `Phrozen_VERSION` 寫死為 `"1.2.0-Education"`，且完全沒有被 `PHROZEN_ORCA_ENABLE_RESIN` 旗標保護 —— 也就是說，現在若拿這份原始碼直接跑 `build_release_vs2022.bat`（主線版建置腳本），版本字串也會混進 "-Education" 字樣。這是一個已經存在、尚未被任何旗標正確隔離的潛在 bug，本次變更會一併修正。

## What Changes

- 新增依 `PHROZEN_ORCA_ENABLE_RESIN` 旗標決定是否 append "-Education"（大寫開頭字串）或 "-education"（小寫開頭字串）尾綴的邏輯，套用到以下識別字串：
  - `version.inc` 的 `Phrozen_VERSION`：拆出乾淨的 base 版本號，CMake 依旗標決定是否 append 尾綴（修正目前未受保護、永遠帶 "-Education" 的問題）。
  - `SLIC3R_APP_NAME` / `SLIC3R_APP_KEY`（決定 AppData profile 資料夾名稱、`wxApp::SetAppName` 等下游行為）。
  - Windows 執行檔實體檔名：`phrozen-orca.exe` → `phrozen-orca-education.exe`（僅限 resin 版），含 `src/CMakeLists.txt` 的 `OUTPUT_NAME`、Linux symlink 目標、`PhrozenOrca.rc.in` 的 `OriginalFilename`。
  - `NetworkAgent.cpp` 用來從目前執行檔路徑推算網路外掛 DLL 位置的字串比對（連同寫死的長度常數）需與新 exe 檔名同步，否則 resin 版會找不到網路外掛 DLL。
  - CPack/NSIS 安裝相關字串：`CPACK_PACKAGE_NAME`、`CPACK_PACKAGE_FILE_NAME`、`CPACK_PACKAGE_INSTALL_REGISTRY_KEY`、`CPACK_PACKAGE_EXECUTABLES`、`CPACK_CREATE_DESKTOP_LINKS`、NSIS 安裝完成後的捷徑/圖示字串。
  - macOS `Info.plist.in` 的 `CFBundleIdentifier` 與 URL scheme。
  - 主視窗標題（`Plater.cpp`）：改用 `SLIC3R_APP_NAME` 變數取代目前硬編碼的字面量 "PhrozenOrca"，讓品牌名稱只有一個真相來源，尾綴會自動繼承。
- 驗證（非必要修改，除非驗證發現真的會撞名）：`InstanceCheck.cpp` 的視窗標題子字串比對、Linux D-Bus 匯流排/物件名稱、macOS distributed notification 名稱，這幾處目前是靠「執行檔完整路徑」的 hash 或子字串比對來分辨實例，exe 改名後理論上會自然分流，但需要手動驗證確認。
- **BREAKING**（僅限 resin/education 變體）：resin 版使用者若已安裝過帶有舊識別（`PhrozenOrca` 資料夾名稱、`phrozen-orca.exe`）的版本，升級到本次變更後的版本會被視為全新安裝（新的 AppData 資料夾、新的安裝目錄與登錄機碼），需要重新匯入 profile 設定；主線 FDM 版完全不受影響。

## Capabilities

### New Capabilities
- `resin-education-variant-branding`: 定義當 `PHROZEN_ORCA_ENABLE_RESIN=ON` 時，所有使用者/作業系統可見的識別字串（AppData 資料夾、執行檔名稱、安裝目錄、登錄機碼、視窗標題、macOS Bundle ID 等）都必須帶有 "-Education"/"-education" 尾綴，且主線 FDM 版（flag `OFF`）的對應行為必須維持完全不變。

### Modified Capabilities
（無 —— 目前 `openspec/specs/` 沒有既有的 spec 涵蓋品牌識別字串或 build-variant 命名規則；`resin-build-update-check-gate` 涵蓋的是更新檢查行為，非本次範圍。）

## Impact

- **建置設定**：`version.inc`、根目錄 `CMakeLists.txt`（`PHROZEN_ORCA_ENABLE_RESIN` 選項宣告之後新增覆寫邏輯）、`src/CMakeLists.txt`（`OUTPUT_NAME`、Linux symlink）。
- **原始碼**：`src/slic3r/Utils/NetworkAgent.cpp`（執行檔路徑字串比對）、`src/slic3r/GUI/Plater.cpp`（視窗標題）、`src/dev-utils/platform/msw/PhrozenOrca.rc.in`、`src/dev-utils/platform/osx/Info.plist.in`。
- **安裝程式**：CPack/NSIS 相關變數（在根目錄 `CMakeLists.txt` 內）。
- **不受影響**：`BUILD_PHROZEN_ORCA`、`PhrozenConnect`、`PartPlateList`、`IMToolbar`、`BBS_TOOLBAR_ON_TOP` 等 PhrozenOrca 專屬客製化程式碼；任何 FDM-only 參數/UI/邏輯；`orcaslicer`/`OrcaSlicer` 品牌殘留字串問題（ProgID、URL scheme、Linux 桌面整合寫出的檔名、G-code Viewer 品牌字）——這些已規劃為獨立的後續變更 `fix-orcaslicer-leftover-paths`，不在本次範圍內。
