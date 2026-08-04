## Context

`resin-education-variant-branding`（2026-07-30）在 `CMakeLists.txt:156-158` 讓 `PHROZEN_ORCA_ENABLE_RESIN=ON` 時，`SLIC3R_APP_KEY` 從 `"PhrozenOrca"` 變成 `"PhrozenOrca-Education"`，用來隔離兩個變體的使用者資料夾、安裝目錄、bundle ID 等。

同時，`src/slic3r/GUI/GUI_App.cpp`（`select_language()` / `load_language()`）與 `src/slic3r/GUI/Preferences.cpp`（Preference 語言清單）共 4 處呼叫，直接把 `SLIC3R_APP_KEY` 當成 `wxTranslations` 的 catalog domain 傳給 `GetAvailableTranslations()` / `GetBestTranslation()` / `AddCatalog()`。

但翻譯 `.mo` 檔的編譯規則（`CMakeLists.txt:714`，`SET(mo_file "${L10N_DIR}/${po_dir}/PhrozenOrca.mo")`）是寫死字面量，不受 `PHROZEN_ORCA_ENABLE_RESIN` 影響——所有變體共用同一份 `PhrozenOrca.mo`。這造成 Education/Resin build 下 domain 名稱（`"PhrozenOrca-Education"`）與磁碟檔名（`"PhrozenOrca.mo"`）對不上，翻譯完全查找失敗。

## Goals / Non-Goals

**Goals:**
- 讓 Education/Resin build 下的翻譯查找 domain 恢復與實際 `.mo` 檔名一致，修好 Preference 語言清單與翻譯載入。
- 讓翻譯 domain 名稱永久與 `SLIC3R_APP_KEY`（品牌識別字串）脫鉤，避免未來 `SLIC3R_APP_KEY` 又疊加新後綴時重演同一類回歸。
- 維持主線 FDM build 行為零改變（no-op）。

**Non-Goals:**
- 不重新設計翻譯檔的編譯/命名機制（`mo_file` 規則維持字面量寫死）。
- 不讓不同建置變體擁有各自獨立的翻譯內容——目前所有變體本來就該共用同一份翻譯。
- 不處理 `resin-education-variant-branding` 規格中其餘識別字串隔離需求（AppData、安裝目錄、視窗標題、bundle ID）——這些維持現狀，不受此次修正影響。

## Decisions

### 決策 1：用固定字面量常數取代 `SLIC3R_APP_KEY`，而非讓 `mo_file` 也依變體命名

**選擇**：在 `GUI_App.hpp` 新增 `L10N_CATALOG_DOMAIN = "PhrozenOrca"` 常數，4 個呼叫點改用此常數。

**替代方案（已否決）**：讓 `CMakeLists.txt` 的 `mo_file` 規則也依 `SLIC3R_APP_KEY` 命名（每個變體各自編譯一份 `.mo`）。

**理由**：翻譯內容（各語言字串）跟「是主線版還是 Education 版」無關，兩個變體顯示的介面文字應該完全相同，沒有理由維護兩份重複的翻譯檔案（增加建置複雜度與未來更新翻譯時漏改一份的風險）。反之，`SLIC3R_APP_KEY` 之所以需要尾綴，是為了隔離「使用者資料/OS 層級識別」（資料夾、登錄機碼、bundle ID），這跟「翻譯字典查找用的技術性 domain 名稱」是兩個不同維度的需求，本來就不該綁在同一個變數上。

### 決策 2：用單一具名常數，而非在 4 個呼叫點各自寫字面量

**選擇**：新增一個具名常數 `L10N_CATALOG_DOMAIN`，並在常數宣告旁加註解指向 `CMakeLists.txt:714` 的 `mo_file` 規則。

**理由**：4 處呼叫點分散在 `GUI_App.cpp`、`Preferences.cpp` 兩個檔案，若各自寫 `"PhrozenOrca"` 字面量，未來如果 PhrozenOrca 本體真的改名（rebrand），需要記得同步改 4 處＋`CMakeLists.txt` 共 5 處；用單一常數可以把 C++ 端要改的地方收斂成 1 處＋`CMakeLists.txt` 共 2 處，並用註解在常數旁提醒未來維護者兩處要一起改。

### 決策 3：不動 `resin-education-variant-branding` 既有的識別字串隔離需求，只新增一條排除規則

**選擇**：在該 capability 的 spec 新增 `## ADDED Requirements`，明確規定翻譯 catalog domain SHALL 維持固定、不受 Education 尾綴規則影響；不去 MODIFY 既有的「AppData/安裝目錄/視窗標題/bundle ID」等需求文字。

**理由**：既有需求描述的是「使用者/OS 可見識別字串」，翻譯 domain 屬於內部技術查找鍵值，語意上原本就不在既有需求的涵蓋範圍內——這是規格的**遺漏邊界**，不是既有規則寫錯，用 ADDED 補上邊界比用 MODIFIED 改寫既有需求更精確，也能在 archive 時保留原始需求的完整歷史。

## Risks / Trade-offs

- **[Risk]** `"PhrozenOrca"` 字面量從此同時存在於 `CMakeLists.txt:714`（`mo_file` 規則）與 `GUI_App.hpp`（`L10N_CATALOG_DOMAIN`）兩處，沒有機制強制同步。
  **→ Mitigation**：在 `L10N_CATALOG_DOMAIN` 常數宣告旁加註解，明確指向 `CMakeLists.txt:714`，降低未來 rebrand 時漏改其中一處的機率。這是純 C++ 端最小改動下無法完全消除的已知限制，已於 proposal 中揭露。

- **[Risk]** 修改範圍僅涵蓋目前已知的 4 個呼叫點；若未來有新程式碼再次直接引用 `SLIC3R_APP_KEY` 當作翻譯 domain（而非使用 `L10N_CATALOG_DOMAIN`），會重新引入同一類 bug。
  **→ Mitigation**：屬於程式碼審查層面的風險，非本次改動範圍可完全防範；已用具名常數＋註解降低誤用機率。

- **無回滾風險**：純 C++ 常數替換，不涉及資料格式、資料庫或使用者資料遷移，若需回滾，直接還原 4 個呼叫點的檔案即可，無 Migration Plan 必要性。

## Open Questions

（無）
