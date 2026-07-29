## Why

PhrozenOrca 從 OrcaSlicer fork 而來，但品牌重命名並未完全落實：程式碼中多處仍寫死 "orcaslicer"/"OrcaSlicer" 字面量，用於 Windows 檔案關聯 ProgID、URL scheme 註冊、Linux 桌面整合功能寫出的檔案、Linux D-Bus 匯流排名稱、macOS distributed notification 名稱，以及 G-code Viewer 的品牌顯示文字。這些字串會讓 PhrozenOrca 在使用者電腦上跟真正的 OrcaSlicer（或其他 Orca 系 fork）共用系統資源 —— 例如安裝 PhrozenOrca 會覆蓋掉 `.3mf`/`.stl` 檔案原本關聯到 OrcaSlicer 的設定，或 Linux 上「桌面整合」功能寫出的 `.desktop` 檔案直接與 OrcaSlicer 衝突。

此問題與 resin/education 變體品牌區隔（`phrozen-education-variant-branding`，已完成規劃）**無關且獨立**：不論建置的是主線 FDM 版還是 resin/education 版，只要還在使用這些 "orcaslicer" 字面量，兩者都會跟外部真正的 OrcaSlicer 撞在一起，這是本次要修的既有缺陷，而非因先前變更引入。

## What Changes

- Windows 檔案關聯 ProgID：`GUI_App.cpp` 的 `associate_files`/`disassociate_files` 目前使用 `" Orca.Slicer.1"`/`"OrcaSlicer"`，改為 PhrozenOrca 專屬 ProgID 與描述文字。
- Windows URL scheme 註冊：`associate_url(L"orcaslicer")` 改為註冊 PhrozenOrca 自己的 scheme（macOS `Info.plist.in` 已宣告過的 `phrozenorca`/`phrozenorcaopen`，但從未在 Windows 執行期實際註冊）。
- Linux 桌面整合功能（`DesktopIntegrationDialog.cpp`，執行期由使用者觸發）：目前寫出的圖示/`.desktop` 檔案字面量寫死 "OrcaSlicer"，改為使用 `SLIC3R_APP_NAME`/`SLIC3R_APP_KEY` 變數，順帶自動繼承 `phrozen-education-variant-branding` 已完成的 education 尾綴分流。
- 建置期 Linux `.desktop` 檔案（`PhrozenOrca.desktop`）：`Exec=orca-slicer` 與實際執行檔名稱（`SLIC3R_APP_CMD`）不一致的既有 bug 一併修正。
- Linux D-Bus 匯流排/物件名稱（`InstanceCheck.cpp`）與 macOS distributed notification 名稱（`InstanceCheckMac.mm`）：品牌字本身（"softfever3d"/"orca-slicer"/"OrcaSlicer"）改為 PhrozenOrca 命名空間（注意：不同 build 變體之間的分流已由 `phrozen-education-variant-branding` 透過路徑 hash 處理，本次只改品牌字，不重複處理分流邏輯）。
- G-code Viewer 品牌字：`libslic3r.h` 的 `GCODEVIEWER_APP_NAME`/`GCODEVIEWER_APP_KEY` 改為 PhrozenOrca 命名，消除設定檔檔名與 UI（關於視窗、系統資訊視窗）上殘留的 "OrcaSlicer G-code Viewer" 曝光。
- 低優先項：確認 `PhrozenOrca-gcodeviewer.rc.in` 是否真為無引用死代碼（若確認無引用則刪除，否則列為待觀察）；`Process.cpp` 的 OpenGL 檢查視窗類別名稱字面量順手修正（純品牌字殘留，無功能風險）。
- **明確不修改**（刻意保留的相容性/上游依賴字串，詳見 design.md）：G-code 檔頭相容性偵測、`PresetUpdater.cpp` 的上游 profile repo URL、Obico/SimplyPrint 第三方 OAuth client_id、多引擎 URL scheme 辨識正規表示式、`AboutDialog.cpp` 血緣揭露文字。

## Capabilities

### New Capabilities
- `orcaslicer-identity-cleanup`: 定義 PhrozenOrca 在檔案關聯、URL scheme、Linux 桌面整合、D-Bus/macOS 實例通知、G-code Viewer 品牌顯示等使用者/系統可見識別上，必須使用自己的品牌命名，不得與外部 OrcaSlicer（或其他 Orca 系 fork）共用或衝突；同時明確排除已知的刻意保留相容性字串，確保這些功能不受影響。

### Modified Capabilities
（無 —— `openspec/specs/` 目前沒有既有 spec 涵蓋檔案關聯/URL scheme/桌面整合品牌識別。）

## Impact

- **原始碼**：`src/slic3r/GUI/GUI_App.cpp`（ProgID、URL scheme 註冊）、`src/slic3r/GUI/DesktopIntegrationDialog.cpp`（Linux 桌面整合）、`src/slic3r/GUI/InstanceCheck.cpp`（D-Bus 命名）、`src/slic3r/GUI/InstanceCheckMac.mm`（macOS 通知命名）、`src/libslic3r/libslic3r.h`（G-code Viewer 品牌常數）、`src/slic3r/Utils/Process.cpp`（低優先，OpenGL 檢查視窗類別）。
- **建置/資源檔**：`src/dev-utils/platform/unix/PhrozenOrca.desktop`（`Exec=`/`StartupWMClass`/MimeType 修正）、`src/dev-utils/platform/msw/PhrozenOrca-gcodeviewer.rc.in`（待確認是否刪除）。
- **不受影響**：`BUILD_PHROZEN_ORCA`、`PhrozenConnect`、`PartPlateList`、`IMToolbar`、`BBS_TOOLBAR_ON_TOP` 等 PhrozenOrca 專屬客製化程式碼；任何 FDM-only 參數/UI/邏輯；`phrozen-education-variant-branding` 已完成規劃的 education 尾綴分流邏輯（本次改動會自動繼承，不重複實作）；`PresetUpdater.cpp` 的上游 profile 更新機制、Obico/SimplyPrint 第三方登入、多引擎拖放辨識、G-code 相容性偵測、`AboutDialog.cpp` 血緣文字 —— 這些明確排除，不予修改。
