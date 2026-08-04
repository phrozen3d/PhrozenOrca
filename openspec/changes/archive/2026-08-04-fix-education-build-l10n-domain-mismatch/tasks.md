## 1. 新增固定 catalog domain 常數

- [x] 1.1 在 `src/slic3r/GUI/GUI_App.hpp` 新增 `L10N_CATALOG_DOMAIN` 常數（固定值 `"PhrozenOrca"`），並加註解指向 `CMakeLists.txt` 的 `mo_file` 命名規則（提醒兩處需手動同步）

## 2. 替換 4 個呼叫點

- [x] 2.1 `src/slic3r/GUI/GUI_App.cpp` `select_language()`：`GetAvailableTranslations(SLIC3R_APP_KEY)` 改為 `GetAvailableTranslations(L10N_CATALOG_DOMAIN)`
- [x] 2.2 `src/slic3r/GUI/GUI_App.cpp` `load_language()`：`GetBestTranslation(SLIC3R_APP_KEY, wxLANGUAGE_ENGLISH)` 改為 `GetBestTranslation(L10N_CATALOG_DOMAIN, wxLANGUAGE_ENGLISH)`
- [x] 2.3 `src/slic3r/GUI/GUI_App.cpp` `load_language()`：`m_wxLocale->AddCatalog(SLIC3R_APP_KEY)` 改為 `m_wxLocale->AddCatalog(L10N_CATALOG_DOMAIN)`
- [x] 2.4 `src/slic3r/GUI/Preferences.cpp` Preference 語言清單建立處：`GetAvailableTranslations(SLIC3R_APP_KEY)` 改為 `GetAvailableTranslations(L10N_CATALOG_DOMAIN)`

## 3. 驗證

- [x] 3.1 確認 4 個呼叫點修改後不再有任何翻譯相關程式碼直接引用 `SLIC3R_APP_KEY`（`grep` 確認；唯一殘留的一筆是既有的註解掉程式碼 `GUI_App.cpp:5274`，非執行路徑，維持原樣）
- [x] 3.2 請使用者以 `build_resin_release_vs2022.bat`（`PHROZEN_ORCA_ENABLE_RESIN=ON`）編譯驗證：Preference 語言清單恢復完整、切換非英文語言後介面正確翻譯（使用者已確認：編譯完成、執行起來重新抓到翻譯字串）
- [x] 3.3 請使用者以 `build_release_vs2022.bat`（主線 FDM build）編譯驗證：語言清單與翻譯行為與修改前一致（no-op 確認）（使用者已確認：FDM release build 編譯完成，成功抓到翻譯字串列表）
