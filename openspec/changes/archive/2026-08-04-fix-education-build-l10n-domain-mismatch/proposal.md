## Why

`resin-education-variant-branding`（2026-07-30 合併）讓 `PHROZEN_ORCA_ENABLE_RESIN=ON`（Education/Resin build）時所有使用者/OS 可見識別字串（含 `SLIC3R_APP_KEY`）都套上 `"-Education"` 尾綴，藉此隔離兩個變體的 AppData 資料夾、安裝目錄、bundle ID 等。但該次改動的範圍描述未涵蓋「翻譯字典查找用的 domain 名稱」——而程式碼裡剛好有 4 處把 `SLIC3R_APP_KEY` 直接當成 `wxTranslations` 的 catalog domain 使用。

翻譯 `.mo` 檔的編譯規則（`CMakeLists.txt` 的 `mo_file`）是寫死字面量 `"PhrozenOrca.mo"`，不隨建置變體變化（所有變體本來就該共用同一份翻譯內容，沒有分開的必要）。於是 Education/Resin build 下，執行期查找的 domain 變成 `"PhrozenOrca-Education"`，跟磁碟上實際存在的 `PhrozenOrca.mo` 對不上：`GetAvailableTranslations()` 回傳空清單，Preference 的語言選單只剩下程式手動塞入的 English 一項；`AddCatalog()` 也載入失敗，就算使用者手動選了其他語言，介面文字依然不會被翻譯。主線 FDM build（旗標 `OFF`）不受影響，因為該路徑下 `SLIC3R_APP_KEY` 仍等於 `"PhrozenOrca"`。

## What Changes

- 新增一個獨立於 `SLIC3R_APP_KEY` 的翻譯 catalog domain 常數（固定值 `"PhrozenOrca"`），讓翻譯查找從此不再跟隨品牌識別字串的變體尾綴。
- 把以下 4 個呼叫點的 `SLIC3R_APP_KEY` 改用這個新常數：
  - `src/slic3r/GUI/GUI_App.cpp` `select_language()` 內的 `GetAvailableTranslations()`
  - `src/slic3r/GUI/GUI_App.cpp` `load_language()` 內的 `GetBestTranslation()`
  - `src/slic3r/GUI/GUI_App.cpp` `load_language()` 內的 `AddCatalog()`
  - `src/slic3r/GUI/Preferences.cpp` Preference 對話框語言清單建立處的 `GetAvailableTranslations()`
- 不修改 `CMakeLists.txt` 的 `mo_file` 命名規則，也不修改任何其他使用 `SLIC3R_APP_KEY` 的識別字串（AppData 資料夾、安裝目錄、視窗標題、bundle ID、3mf metadata 等）——這些依 `resin-education-variant-branding` 的既有規格，本來就應該繼續反映 `-Education` 尾綴，不受此次修正影響。

已否決的替代方案：讓 `mo_file` 也依 `SLIC3R_APP_KEY` 命名（每個變體各自編譯一份翻譯檔）。翻譯內容本來就該在所有變體間共用，沒有理由拆成兩份；且此做法需要同時改 CMake 編譯規則與 4 個 C++ 呼叫點，改動範圍明顯大於單純固定 domain 名稱。

## Capabilities

### New Capabilities
(無)

### Modified Capabilities
- `resin-education-variant-branding`：新增一條「排除」需求，明確規定翻譯 catalog domain 名稱 SHALL 維持固定、不隨 Education 尾綴規則變化，補上原規格未涵蓋、因而導致此次回歸的邊界情境。

## Impact

- **受影響程式碼**：`src/slic3r/GUI/GUI_App.hpp`（新增常數宣告）、`src/slic3r/GUI/GUI_App.cpp`（3 處呼叫點）、`src/slic3r/GUI/Preferences.cpp`（1 處呼叫點）。
- **不受影響**：`CMakeLists.txt` 翻譯編譯規則、既有 `.mo` 檔案、`resin-education-variant-branding` 規格中其餘識別字串隔離需求（AppData、安裝目錄、視窗標題、bundle ID）。
- **受影響族群**：僅 `PHROZEN_ORCA_ENABLE_RESIN=ON`（Education/Resin build，例如 `build_resin_release_vs2022.bat`）的使用者；主線 FDM build 行為不變（no-op）。
- **印表機技術範圍**：純翻譯基礎設施修正，不涉及任何 FDM/SLA 判斷分支，兩者行為一致，無需 SLA-specific guard；不觸碰任何 `BUILD_PHROZEN_ORCA` / PhrozenConnect / PartPlateList / IMToolbar 客製化程式碼。
