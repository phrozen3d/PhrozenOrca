## Context

PhrozenOrca 的 fork 血緣為 Slic3r → PrusaSlicer → BambuStudio → OrcaSlicer → PhrozenOrca。品牌重命名（OrcaSlicer → PhrozenOrca）在多數地方已經完成（`SLIC3R_APP_NAME`/`SLIC3R_APP_KEY` = "PhrozenOrca"、CPack 安裝設定、Windows/macOS 資源檔的圖示與名稱等），但稽核發現有一批識別字串仍寫死為 "orcaslicer"/"OrcaSlicer" 字面量，而非使用既有的品牌變數，或是完全獨立於品牌變數之外的另一組硬編碼字串（例如檔案關聯 ProgID、Linux D-Bus 命名空間）。

這批殘留分成兩種性質：
1. **本來就該用變數、卻寫死了字面量**（`DesktopIntegrationDialog.cpp` 的 Linux 桌面整合輸出）—— 只要改成引用 `SLIC3R_APP_NAME`/`SLIC3R_APP_KEY`，即可同時修正品牌與自動繼承 `phrozen-education-variant-branding` 的尾綴分流。
2. **本來就是獨立命名、需要重新指定成 PhrozenOrca 專屬值**（ProgID、URL scheme、D-Bus 匯流排名稱、macOS 通知前綴、G-code Viewer 常數）—— 這些不是「原本該是變數卻寫錯」，而是需要決定一個新的、屬於 PhrozenOrca 自己的具體值。

## Goals / Non-Goals

**Goals:**
- 移除所有會導致 PhrozenOrca 與外部真正 OrcaSlicer（或其他 Orca 系 fork）共用系統資源（檔案關聯機碼、URL scheme、Linux 桌面整合檔案、D-Bus 匯流排、macOS 通知）的殘留字串。
- 移除使用者在 UI 上會直接看到的殘留品牌曝光（G-code Viewer 標題/關於視窗）。
- 修正一個連帶發現的、與品牌無關但同樣位於 Linux `.desktop` 檔案裡的既有 bug（`Exec=orca-slicer` 與實際執行檔名稱不符）。
- 明確排除並記錄所有「刻意保留、故意保持 OrcaSlicer 相容性」的字串，避免本次或未來修改誤觸。

**Non-Goals:**
- 不重複處理 `phrozen-education-variant-branding` 已經完成的「兩個 PhrozenOrca 變體（主線 vs resin/education）互不干擾」邏輯 —— 那個 change 已經用路徑 hash 或旗標分流解決了那部分。本次只處理「品牌字本身還是 OrcaSlicer」這件事，兩者疊加後應自動同時滿足两个关注点，不需要在本次重新設計分流機制。
- 不修改任何 `BUILD_PHROZEN_ORCA` 保護的程式碼（`PhrozenConnect`、`PartPlateList`、`IMToolbar`、`BBS_TOOLBAR_ON_TOP`）。
- 不修改任何 FDM-only 參數/UI/邏輯。
- 不修改任何刻意保留的相容性/上游依賴字串（詳見 Decisions 第 6 點的完整列表）。

## Decisions

### 1. Windows 檔案關聯 ProgID：改用 PhrozenOrca 專屬 ProgID

**決策**：`GUI_App.cpp` 的 `associate_files`/`disassociate_files` 把 `prog_id = L" Orca.Slicer.1"` 改為 PhrozenOrca 專屬值（例如 `L"Phrozen.Orca.1"`），`prog_desc` 從 `L"OrcaSlicer"` 改為對應 `SLIC3R_APP_NAME`。

**替代方案考慮**：保留 `Orca.Slicer.1` 但加上版本號區分 —— 放棄，因為問題根源是「共用了不屬於自己的命名空間」，即使加版本號仍然是在 OrcaSlicer 的命名空間下註冊，治標不治本；改用完全獨立的 `Phrozen.*` 命名空間才是正確做法。

**風險**：既有使用者若已經安裝過舊版本、`.3mf`/`.stl` 檔案關聯到舊的 `Orca.Slicer.1` ProgID，升級後需要重新建立檔案關聯（作業系統會因為 ProgID 改變而視為新的關聯目標）。此為必要的一次性代價。

### 2. URL scheme：改用 PhrozenOrca 自己已宣告但未實際註冊的 scheme

**決策**：`associate_url(L"orcaslicer")` 改為 `associate_url` PhrozenOrca 自己的 scheme（macOS `Info.plist.in` 已宣告的 `phrozenorca`，Windows 端目前完全沒有註冊任何自訂 scheme，屬於順便補齊）。

**理由**：避免新增第三個 scheme 名稱造成 Windows/macOS 兩平台不一致；直接沿用已存在於 macOS 設定裡、但從未在 Windows 端真正註冊過的既有值。

### 3. Linux 桌面整合功能：改用變數，不另外寫新字面量

**決策**：`DesktopIntegrationDialog.cpp` 裡所有目前寫死 "OrcaSlicer" 的地方（圖示檔名、`.desktop` 檔案內容、URL protocol `.desktop` 檔案）全部改為引用 `SLIC3R_APP_NAME`/`SLIC3R_APP_KEY`。

**理由**：這個函式的 `version_suffix` 參數機制本來就是為了避免版本間衝突而存在的，只是品牌名稱那一半沒有跟著做；改用變數後不僅修正品牌問題，也讓 `phrozen-education-variant-branding` 的尾綴分流自動套用到 Linux 桌面整合輸出，不需要在這個 change 裡重複判斷 `PHROZEN_ORCA_ENABLE_RESIN` 旗標。

### 4. Linux `.desktop` 檔案的 `Exec=` 修正

**決策**：`PhrozenOrca.desktop` 的 `Exec=orca-slicer %U` 改為與實際 `SLIC3R_APP_CMD` 一致（主線版 `phrozen-orca`，education 版透過 `phrozen-education-variant-branding` 已完成的機制得到 `phrozen-orca-education`）；`MimeType`/`StartupWMClass` 裡的 `orcaslicer`/`orca-slicer` 一併改為對應值。

**理由**：這是一個獨立的、與品牌命名無關但同一個檔案裡發現的既有 bug —— 目前這份桌面捷徑檔案很可能根本無法啟動軟體（除非剛好系統上有一個叫 `orca-slicer` 的執行檔或 symlink），一併修正避免遺漏。

### 5. D-Bus 匯流排名稱與 macOS 通知名稱：只改品牌字，不動分流機制

**決策**：`InstanceCheck.cpp` 的 D-Bus 匯流排/物件名稱把 `com.softfever3d.orca-slicer`/`OrcaSlicer` 部分改為 PhrozenOrca 對應命名空間（例如 `com.phrozen3d.phrozen-orca`，與 macOS `Info.plist.in` 已使用的 Bundle ID 前綴一致）；`InstanceCheckMac.mm` 的 `"OtherOrcaSlicerInstance..."` 改為 `"OtherPhrozenOrcaInstance..."`。原本接在後面的 `version`/`instance_hash` 分流後綴維持不動（那是 `phrozen-education-variant-branding` 已確認可行的機制）。

**理由**：品牌字命名空間屬於「識別 PhrozenOrca 自己」的問題，分流後綴屬於「識別不同 build 變體」的問題，兩者正交，各自處理互不干擾。

### 6. G-code Viewer 品牌常數：改用 PhrozenOrca 命名

**決策**：`libslic3r.h` 的 `GCODEVIEWER_APP_NAME`（"OrcaSlicer G-code Viewer" → 對應 PhrozenOrca 命名，例如 "PhrozenOrca G-code Viewer"）與 `GCODEVIEWER_APP_KEY`（"OrcaSlicerGcodeViewer" → "PhrozenOrcaGcodeViewer"）。

**風險**：`GCODEVIEWER_APP_KEY` 目前被用來組出設定檔檔名（`AppConfig.cpp`），改變這個常數會讓既有使用者的 G-code Viewer 設定檔名稱改變，等同於該設定檔的一次性遷移（使用者會遺失原有的 G-code Viewer 專屬設定，例如視窗大小/位置偏好，需重新設定一次）。此設定檔內容與主程式的 profile/列印參數無關，風險與影響範圍都很小。

### 7. 低優先項的處理方式

**決策**：`PhrozenOrca-gcodeviewer.rc.in` 先確認是否真的無 `configure_file()`/CMake target 引用；若確認無引用則直接刪除（而非修正內容，因為修正一份沒人使用的檔案沒有意義）。`Process.cpp` 的 OpenGL 檢查視窗類別名稱字面量屬於零風險的品牌字順手修正（Win32 視窗類別是 process-scoped，不會真的跨程序撞名），一併修正但不需要額外驗證步驟。

### 8. 明確排除清單（供實作與 QA 對照，避免誤觸）

以下字串**刻意保留、本次不修改**：
- `src/libslic3r/Config.cpp` 解析 G-code 檔頭 `"; OrcaSlicer"` 等字樣（辨識其他 Bambu/Orca 系軟體產出檔案，用於相容性匯入）。
- `src/slic3r/Utils/PresetUpdater.cpp` 的 `PROFILE_UPDATE_URL`（`OrcaSlicer/orcaslicer-profiles` repo）與對應的 zip 檔名比對規則（實際的上游 profile 更新來源）。
- `src/slic3r/Utils/Obico.cpp` 的 OAuth `client_id=OrcaSlicer`、`src/slic3r/Utils/SimplyPrint.cpp` 的 `CLIENT_ID = "simplyprintorcaslicer"`（第三方已登記的 OAuth client_id）。
- `src/slic3r/GUI/Downloader.cpp`、`GUI_App.cpp` 裡辨識多種來源 URL scheme 的正規表示式（`orcaslicer|prusaslicer|bambustudio|cura`）。
- `AboutDialog.cpp` 的 "Phrozen Orca is based on OrcaSlicer..." 血緣說明文字。
- 純註解、GitHub issue 連結相關文字。

## Risks / Trade-offs

- **[風險] ProgID/GCODEVIEWER_APP_KEY 改變造成既有使用者的檔案關聯/G-code Viewer 設定被視為全新設定，需要重新設定一次。** → Mitigation：這是品牌命名空間修正的必要代價，範圍僅限檔案關聯與 G-code Viewer 獨立設定檔，不影響主程式列印參數/profile；於 release note 中提醒。
- **[風險] 修改 D-Bus/URL scheme 註冊邏輯時，若疏漏任何一個仍引用舊字面量的呼叫點，會造成一部分程式碼用新命名、另一部分仍用舊命名，導致自我偵測失效。** → Mitigation：tasks.md 逐項列出所有已知呼叫點行號，QA 驗收步驟包含逐一開啟對應系統工具比對是否還有殘留 "orcaslicer" 字串。
- **[Trade-off] 本次修改觸及的檔案（`InstanceCheck.cpp`、`InstanceCheckMac.mm`）與 `phrozen-education-variant-branding` 有重疊，若兩個 change 的實作順序顛倒或部分回退，可能產生合併衝突。** → Mitigation：建議先完成並驗證 `phrozen-education-variant-branding` 的程式碼修改，再實作本次變更；若必須同時進行，兩邊改動的行號範圍需交叉確認。

## Migration Plan

1. 先確認 `phrozen-education-variant-branding` 的程式碼修改已完成（本 change 依賴其對 `SLIC3R_APP_NAME`/`SLIC3R_APP_KEY` 的尾綴邏輯，雖然本身不重新實作，但共用同一批檔案，順序上後做較不易衝突）。
2. 依 tasks.md 逐項修改 ProgID、URL scheme、桌面整合、D-Bus/通知命名、G-code Viewer 常數。
3. 建置後依 QA 驗收步驟（tasks.md 第4節）在 Windows/Linux/macOS（視可用環境）上分別驗證。
4. 無伺服器端遷移；使用者端影響僅限檔案關聯與 G-code Viewer 獨立設定檔的一次性重設，不需要額外的資料遷移腳本。

## Open Questions

- `PhrozenOrca-gcodeviewer.rc.in` 是否曾在更早的建置設定中被引用過（例如舊版 CMakeLists 曾經 configure_file 過、後來移除引用但忘記刪檔案）——需要在實作階段用 git blame/history 或全文搜尋二次確認後才能決定刪除。
