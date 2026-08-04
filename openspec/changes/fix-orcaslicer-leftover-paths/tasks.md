## 1. Windows 檔案關聯與 URL scheme

- [ ] 1.1 `src/slic3r/GUI/GUI_App.cpp`（`associate_files`，約6844-6867行）：`prog_id = L" Orca.Slicer.1"` 改為 PhrozenOrca 專屬值（例如 `L"Phrozen.Orca.1"`），`prog_desc = L"OrcaSlicer"` 改為對應 `SLIC3R_APP_NAME`。
- [ ] 1.2 `GUI_App.cpp`（`disassociate_files`，約6869-6899行）：同步改成解除/操作新的 ProgID，不再殘留對舊 `Orca.Slicer.1` 的引用。
- [ ] 1.3 `GUI_App.cpp:2482,5788`：`associate_url(L"orcaslicer")` 改為註冊 PhrozenOrca 自己的 scheme（與 macOS `Info.plist.in` 已宣告的 `phrozenorca` 一致）。

## 2. Linux 桌面整合與 .desktop 檔案

- [ ] 2.1 `src/slic3r/GUI/DesktopIntegrationDialog.cpp`（約212, 293-360, 382行）：圖示複製目標與 `.desktop` 檔案的 `Name=`/`Icon=` 改用 `SLIC3R_APP_NAME`/`SLIC3R_APP_KEY` 變數，移除寫死的 "OrcaSlicer" 字面量。
- [ ] 2.2 `DesktopIntegrationDialog.cpp`（約390-419行，G-code viewer 變體）：同步改用變數。
- [ ] 2.3 `DesktopIntegrationDialog.cpp`（約534-604行，URL protocol `.desktop`）：`Name=`、檔名、`xdg-mime default` 目標檔名同步改用變數。
- [ ] 2.4 `src/dev-utils/platform/unix/PhrozenOrca.desktop:5,8,12`：`Exec=orca-slicer %U` 改為與 `SLIC3R_APP_CMD` 一致；`MimeType` 內的 `x-scheme-handler/orcaslicer` 與 `StartupWMClass=orca-slicer` 同步改為 PhrozenOrca 對應值。

## 3. 單一實例偵測命名空間

- [ ] 3.1 `src/slic3r/GUI/InstanceCheck.cpp`（約240,243,554,566,592,612-613,641行）：D-Bus 匯流排/物件名稱由 `com.softfever3d.orca-slicer...`/`OrcaSlicer` 改為 PhrozenOrca 命名空間（例如 `com.phrozen3d.phrozen-orca...`），保留原有的 `version`/`instance_hash` 後綴機制不變。
- [ ] 3.2 `src/slic3r/GUI/InstanceCheckMac.mm:15-18,59,68`：`"OtherOrcaSlicerInstanceMessage"`/`"OtherOrcaSlicerInstanceClosing"` 改為 PhrozenOrca 對應命名（例如 `"OtherPhrozenOrcaInstanceMessage"`），hash 後綴機制不變。

## 4. G-code Viewer 品牌常數

- [ ] 4.1 `src/libslic3r/libslic3r.h:6-8`：`GCODEVIEWER_APP_NAME`（"OrcaSlicer G-code Viewer" → PhrozenOrca 對應命名）、`GCODEVIEWER_APP_KEY`（"OrcaSlicerGcodeViewer" → PhrozenOrca 對應命名）。
- [ ] 4.2 確認 `AboutDialog.cpp:46,215`、`SysInfoDialog.cpp:41,43,85,107`、`Plater.cpp:11209`、`libslic3r/utils.cpp:1170` 這些 UI 曝光點在常數改名後正確反映新名稱（多數應自動繼承，僅需確認無額外硬編碼字面量）。

## 5. 低優先/待確認項

- [ ] 5.1 確認 `src/dev-utils/platform/msw/PhrozenOrca-gcodeviewer.rc.in` 是否真的無任何 `configure_file()`/CMake target 引用（可用 `git grep`/全文搜尋檔名確認）；若確認無引用則刪除此檔案，否則於此標記待觀察並說明原因。
- [ ] 5.2 `src/slic3r/Utils/Process.cpp` 的 OpenGL 檢查視窗類別名稱 `"OrcaSlicer_opengl_version_check"` 改為 PhrozenOrca 對應命名（純品牌字修正，無功能風險，不需額外驗證步驟）。

## 6. QA 手動驗收步驟（提交測試人員驗收用）

> 建議在 `phrozen-education-variant-branding` 的程式碼修改完成並驗證通過後，再進行本次驗收，避免兩批修改互相干擾判讀。以下步驟中標示「兩個變體」的項目，建議分別用主線版與 resin/education 版各測一次。

- [ ] 6.1 **Windows 檔案關聯不衝突**：在測試機上先安裝一份真正的 OrcaSlicer（或建置前的舊版 PhrozenOrca 作為對照），對 `.3mf` 檔案按右鍵設定關聯。接著安裝本次修改後的 PhrozenOrca，在其設定內開啟「檔案關聯」選項。
  - 開啟 `regedit`，導覽至 `HKEY_CURRENT_USER\Software\Classes\`，確認存在一個新的 ProgID 機碼（例如 `Phrozen.Orca.1`），且原本 OrcaSlicer 註冊的 `Orca.Slicer.1` 機碼內容未被覆蓋或竄改
  - 對 `.3mf` 檔案按右鍵 →「開啟方式」，確認清單中「PhrozenOrca」與「OrcaSlicer」是兩個獨立可分辨的選項，選擇 PhrozenOrca 開啟時能正確啟動且不顯示 "OrcaSlicer" 字樣

- [ ] 6.2 **URL scheme 註冊**：在 PhrozenOrca 內觸發 URL scheme 關聯設定後，開啟 `regedit`，導覽至 `HKEY_CURRENT_USER\Software\Classes\`，確認新增的機碼名稱是 PhrozenOrca 自己的 scheme（例如 `phrozenorca`），而不是 `orcaslicer`。可在瀏覽器網址列輸入對應 `phrozenorca://` 連結測試是否正確喚起 PhrozenOrca。

- [ ] 6.3 **Linux 桌面整合功能**（需要 Linux 測試環境）：在 PhrozenOrca 內執行「桌面整合」偏好設定功能。
  - 檢查 `~/.local/share/applications/` 底下產生的 `.desktop` 檔案，確認檔名與內容的 `Name=`/`Icon=` 反映正確品牌（主線版顯示 "PhrozenOrca"，education 版顯示 "PhrozenOrca-Education"），且**不出現任何 "OrcaSlicer" 字樣**
  - 檢查對應圖示檔案是否也以正確品牌名稱命名
  - 若系統上另外裝有真正的 OrcaSlicer 並執行過同樣的桌面整合，確認兩者的 `.desktop`/圖示檔案並存、檔名不同、互不覆蓋

- [ ] 6.4 **建置期 .desktop 檔案可正確啟動**：透過 FHS 安裝方式安裝 PhrozenOrca 後，從應用程式選單/啟動器點擊 PhrozenOrca 捷徑。
  - 確認軟體能正確啟動（不因 `Exec=` 指向錯誤的執行檔名稱而失敗或跳出「找不到指令」錯誤）
  - 用文字編輯器開啟安裝後的 `.desktop` 檔案（一般在 `/usr/share/applications/` 或對應安裝路徑），確認 `Exec=`、`MimeType`、`StartupWMClass` 都指向正確的 PhrozenOrca 執行檔名稱與 scheme，不含 "orca-slicer"/"orcaslicer" 字樣

- [ ] 6.5 **D-Bus 匯流排命名**（需要 Linux 測試環境）：啟動 PhrozenOrca 後，開啟終端機執行 `busctl --user list` 或 `dbus-monitor --session`（或使用 `d-feet` 圖形化工具），搜尋是否存在包含 "orca-slicer"/"softfever3d" 字樣的匯流排/物件名稱。
  - 預期結果：找不到任何 "orca-slicer"/"softfever3d" 字樣，應能看到 PhrozenOrca 對應命名空間的服務
  - 額外測試：同時啟動兩個 PhrozenOrca 實例（同一版本），確認「帶到前景」的單一實例偵測行為仍然正常運作

- [ ] 6.6 **macOS distributed notification 命名**（需要 macOS 測試環境）：開啟「主控台」(Console.app)，篩選條件輸入 "OrcaSlicer" 或 "PhrozenOrca"，啟動兩個 PhrozenOrca 實例觸發單一實例偵測。
  - 預期結果：Console.app 中看到的通知名稱不含 "OrcaSlicer" 字樣，應顯示 PhrozenOrca 對應命名
  - 確認「帶到前景」行為仍正常運作

- [ ] 6.7 **G-code Viewer 品牌顯示**：開啟 G-code Viewer 獨立視窗（可透過開啟 `.gcode` 檔案或軟體內對應功能觸發）。
  - 觀察視窗標題列文字，確認顯示 PhrozenOrca 對應命名，不含 "OrcaSlicer" 字樣
  - 開啟「關於」對話框與「系統資訊」對話框，確認軟體名稱欄位同樣不含 "OrcaSlicer" 字樣
  - 開啟 `%APPDATA%\PhrozenOrca\`（或 education 版對應資料夾），確認 G-code Viewer 設定檔的檔名反映新的 `GCODEVIEWER_APP_KEY`（不再是 `OrcaSlicerGcodeViewer.ini`/`.conf`）

- [ ] 6.8 **回歸測試：刻意保留項目仍正常運作**：
  - 觸發印表機/耗材 profile 更新檢查，確認能正常連線並完成更新（驗證 `PresetUpdater.cpp` 的上游 repo URL 未被誤改）
  - 若測試環境有 Obico/SimplyPrint 帳號，測試登入/連動流程是否正常完成（驗證 OAuth client_id 未被誤改）
  - 拖曳一個由其他相容軟體（PrusaSlicer/BambuStudio/Cura）產生的專案檔到 PhrozenOrca 視窗，確認仍能正確識別來源並提示/開啟（驗證多引擎 URL scheme 辨識正規表示式未被誤改）
  - 開啟「關於」對話框，確認 "based on OrcaSlicer" 血緣揭露文字仍然存在、文字內容未被誤刪

- [ ] 6.9 **低優先項確認**：若 5.1 決定刪除 `PhrozenOrca-gcodeviewer.rc.in`，確認建置流程（Windows/Linux/macOS）都能正常完成，不因缺少此檔案而報錯。
