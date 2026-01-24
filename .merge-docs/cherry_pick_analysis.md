# OrcaSlicer v2.3.1 Cherry-pick 分析

## 總覽

- **總 commits 數**: 157
- **包含 "fix" 的 commits**: ~80+
- **崩潰修復**: 3 個明確標記的
- **Profile 相關**: 多個

---

## 🔴 高優先級 - 必須合併

### 崩潰修復

| Commit | 描述 | 優先級 |
|--------|------|--------|
| `f27381533c` | Fix a crash issue when importing a 3MF file saved from version 2.3.1-alpha as geometry only | ⭐⭐⭐⭐⭐ |
| `684f5b44ee` | Fix crash when opening AMS humidity popup | ⭐⭐⭐⭐⭐ |

### 核心功能修復

| Commit | 描述 | 優先級 | 備註 |
|--------|------|--------|------|
| `737948be1f` | [Profiles] Fix bed_exclude_area excluding the whole bed on Anycubic Kobra 3 | ⭐⭐⭐⭐ | Profile 相關，但修復了核心邏輯 bug |
| `d55f016568` | Fix grid lines origin for multiple plates | ⭐⭐⭐⭐ | 多板功能修復 |
| `b483dff617` | Enhance GCode handling for Z-axis movements | ⭐⭐⭐⭐⭐ | G-code 生成改進 |
| `e5243be866` | fix a regression bug that wrong printer model for Prusa MK3S and MINI in 2.3.1 beta | ⭐⭐⭐⭐ | 回歸 bug 修復 |
| `78eb3b464f` | Fix the bug where FillRectilinear generates an unoptimized toolpath | ⭐⭐⭐⭐ | 切片引擎修復 |

### 邏輯錯誤修復

| Commit | 描述 | 優先級 |
|--------|------|--------|
| `2f2018f9ee` | Fix logic for precise_outer_wall condition in PerimeterGenerator | ⭐⭐⭐⭐ |
| `5e9570c946` | fix check_filament_compatible_printers | ⭐⭐⭐ |

---

## 🟡 中優先級 - 建議合併

### 程式碼品質改進

| Commit | 描述 | 優先級 | 備註 |
|--------|------|--------|------|
| `c228ab2da1` | Fixes 999 CMake Warnings | ⭐⭐⭐ | 建置系統改進 |
| `75ed995b00` | Fixes 50 Compiler Warnings: Add SYSTEM to CMakeLists.txt | ⭐⭐⭐ | 編譯器警告修復 |
| `7aa3ce8a4d` | Shellcheck everything | ⭐⭐ | 腳本品質改進 |

### UI/UX 改進

| Commit | 描述 | 優先級 | 備註 |
|--------|------|--------|------|
| `38ed01f61f` | gCode Legend Fixes / Improvements | ⭐⭐⭐ | UI 改進 |
| `cd9081b16d` | [QOL] Remember slider position for single layer mode in preview | ⭐⭐⭐ | 使用體驗改進 |

### 新功能

| Commit | 描述 | 優先級 | 備註 |
|--------|------|--------|------|
| `31869bfbd1` | [Feature] Add a new feature that allow user to insert extra solid infills | ⭐⭐⭐ | 新功能，需評估 |
| `266bfeb9e2` | Refactor infill rotation | ⭐⭐⭐ | 重構，可能改進效能 |

---

## 🟢 低優先級 - 可選合併

### 文件和翻譯

| Commit | 描述 | 優先級 | 備註 |
|--------|------|--------|------|
| `5cee8ed086` | Correct 4 Italian translations | ⭐ | PhrozenOrca 可能不需要 |
| `71c7944eab` | fix some german translations | ⭐ | PhrozenOrca 可能不需要 |
| `0eef794824` | [DOC] Fix typo "rotatation" | ⭐ | 文件修正 |
| `4faaa5e6ee` | Wiki Update 12 - Others | ⭐ | Wiki 更新 |

### 特定機台 Profile

| Commit | 描述 | 優先級 | 備註 |
|--------|------|--------|------|
| `504b89af03` | [PROFILE] fix for Ender 3 V3 KE | ⭐⭐ | 非 Phrozen 機台 |
| `756129fff2` | Fix Anycubic Kobra 2 Neo Machine Profile | ⭐ | 非 Phrozen 機台 |
| `8e6d69dc9f` | [Profiles] Optimize profiles for BLOCKS RF50 printer | ⭐ | 非 Phrozen 機台 |
| `6d0933d27c` | [Profile] Fix start_gcode for FlyingBear machines | ⭐ | 非 Phrozen 機台 |
| `90a6c53ad5` | Feat/profiles cubicon xceler i | ⭐ | 非 Phrozen 機台 |

### 新 Profile/Vendor

| Commit | 描述 | 優先級 | 備註 |
|--------|------|--------|------|
| `c219600a17` | Add Official overture preset | ⭐ | 新 vendor，PhrozenOrca 可能不需要 |

---

## ⚫ 不建議合併

### 大型重構或可能影響品牌化

這些需要仔細評估，可能會與 PhrozenOrca 的客製化衝突：

- 任何涉及應用程式名稱或品牌的修改
- 大型 UI 重構
- 建置系統的重大變更

---

## 📋 建議的 Cherry-pick 順序

### 第一批：關鍵崩潰和安全修復（立即執行）

```bash
# 1. 崩潰修復
git cherry-pick f27381533c  # 3MF import crash
git cherry-pick 684f5b44ee  # AMS humidity popup crash
```

### 第二批：核心功能修復（高優先級）

```bash
# 2. G-code 和切片引擎
git cherry-pick b483dff617  # Z-axis GCode handling
git cherry-pick 78eb3b464f  # FillRectilinear toolpath fix
git cherry-pick 2f2018f9ee  # precise_outer_wall logic fix

# 3. 多板和 UI 修復
git cherry-pick d55f016568  # Grid lines for multiple plates
git cherry-pick 737948be1f  # bed_exclude_area fix

# 4. 回歸 bug
git cherry-pick e5243be866  # Printer model regression
```

### 第三批：程式碼品質和建置改進（中優先級）

```bash
# 5. 建置系統改進
git cherry-pick c228ab2da1  # CMake warnings
git cherry-pick 75ed995b00  # Compiler warnings
```

### 第四批：UI/UX 改進（可選）

```bash
# 6. 使用體驗改進
git cherry-pick 38ed01f61f  # gCode Legend improvements
git cherry-pick cd9081b16d  # Remember slider position (需確認此 commit 存在)
```

### 第五批：新功能（評估後決定）

```bash
# 7. 新功能（需要測試和評估）
git cherry-pick 31869bfbd1  # Extra solid infills
git cherry-pick 266bfeb9e2  # Infill rotation refactor
```

---

## ⚠️ 預期的衝突點

根據 PhrozenOrca 的客製化，以下 commits 可能會有衝突：

1. **任何涉及 CMakeLists.txt 的修改**
   - PhrozenOrca 已經修改了應用程式名稱
   - 需要手動合併，保留 "PhrozenOrca" 名稱

2. **UI 相關的修改**
   - 需要檢查是否與 Phrozen 的 UI 客製化衝突
   - 特別是顏色、佈局相關的修改

3. **翻譯檔案**
   - PhrozenOrca 使用 PhrozenOrca_*.po
   - OrcaSlicer 使用 OrcaSlicer_*.po
   - 可能需要手動合併翻譯內容

---

## 🎯 執行計畫

### 階段一：執行第一批（今天）
- 2 個崩潰修復
- 預估時間：1-2 小時
- 風險：低

### 階段二：執行第二批（明天）
- 6 個核心功能修復
- 預估時間：3-4 小時
- 風險：中（可能有衝突）

### 階段三：執行第三批（後天）
- 2 個建置改進
- 預估時間：2-3 小時
- 風險：中（CMakeLists.txt 可能衝突）

### 階段四：評估和測試（第四天）
- 編譯測試
- 功能測試
- 決定是否繼續第四、五批

---

## 📝 注意事項

1. **每個 cherry-pick 後都要編譯測試**
2. **遇到衝突時，優先保留 PhrozenOrca 的品牌化**
3. **記錄每個 cherry-pick 的結果**
4. **如果某個 commit 衝突太複雜，可以跳過**
5. **完成一批後再進行下一批**
