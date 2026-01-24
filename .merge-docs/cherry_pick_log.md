# Cherry-pick 記錄 - OrcaSlicer v2.3.1 → PhrozenOrca

## 第一批：關鍵崩潰修復（✅ 已完成）

### 1. f27381533c - Fix a crash issue when importing a 3MF file saved from version 2.3.1-alpha as geometry only

- **作者**: SoftFever <softfeverever@gmail.com>
- **日期**: 2025-09-21 16:13:45 +0800
- **優先級**: ⭐⭐⭐⭐⭐
- **類別**: 崩潰修復
- **衝突**: ✅ 有（已解決）
  - 檔案: `src/slic3r/GUI/Plater.cpp`
  - 原因: 版本檢查邏輯衝突
  - 解決: 整合 OrcaSlicer 的修復，將 "OrcaSlicer" 改為 "PhrozenOrca"
- **變更摘要**: 添加針對 2.3.1-alpha 版本的 infill rotation template 檢查和修復邏輯
- **PhrozenOrca commit**: 06c06ddcd9

---

### 2. 684f5b44ee - Fix crash when opening AMS humidity popup

- **作者**: Noisyfox <timemanager.rick@gmail.com>
- **日期**: 2025-08-21 00:18:17 +0800
- **優先級**: ⭐⭐⭐⭐⭐
- **類別**: 崩潰修復
- **衝突**: ✅ 有（已解決）
  - 檔案: `src/slic3r/GUI/DeviceTab/uiAmsHumidityPopup.cpp`
  - 原因: Unicode 字元處理方式不同
  - 解決: 採用 OrcaSlicer 的 `wxString::FromUTF8()` 方法
- **變更摘要**: 修復溫度符號（℃）的 Unicode 處理，避免崩潰
- **PhrozenOrca commit**: 6b00c65315

---

## 第二批：核心功能修復（✅ 部分完成）

### 3. b483dff617 - Enhance GCode handling for Z-axis movements

- **作者**: SoftFever <softfeverever@gmail.com>
- **日期**: 2025-09-21 22:03:54 +0800
- **優先級**: ⭐⭐⭐⭐⭐
- **類別**: G-code 生成改進
- **衝突**: ❌ 無
- **變更摘要**: 改進 Z 軸移動的 G-code 處理
- **PhrozenOrca commit**: 6d3f070058
- **狀態**: ✅ 成功

---

### 4. 78eb3b464f - Fix the bug where FillRectilinear generates an unoptimized toolpath

- **作者**: TBD
- **日期**: TBD
- **優先級**: ⭐⭐⭐⭐
- **類別**: 切片引擎修復
- **衝突**: ⚠️ 複雜衝突
  - 檔案: `src/libslic3r/Fill/Fill.cpp`
  - 原因: 參數重命名 (`lattice_angle` → `lateral_lattice_angle`) 和 multiline/angle 計算邏輯改變
  - 解決: **暫時跳過** - 衝突過於複雜，需要更仔細的手動合併
- **變更摘要**: 修復 FillRectilinear 生成未優化路徑的問題
- **PhrozenOrca commit**: ⚠️ 跳過
- **狀態**: ⏭️ 跳過（待後續處理）

---

### 5. 2f2018f9ee - Fix logic for precise_outer_wall condition in PerimeterGenerator

- **作者**: SoftFever <softfeverever@gmail.com>
- **日期**: 2025-09-12 00:24:03 +0800
- **優先級**: ⭐⭐⭐⭐
- **類別**: 邏輯錯誤修復
- **衝突**: ✅ 有（已解決）
  - 檔案: `src/libslic3r/PerimeterGenerator.cpp`
  - 原因: 條件檢查邏輯不同
  - 解決: 採用 OrcaSlicer 的修復，添加 `wall_sequence == WallSequence::InnerOuter` 條件
- **變更摘要**: 修復 PerimeterGenerator 中 precise_outer_wall 的條件邏輯，只在 wall_sequence 為 InnerOuter 時應用
- **PhrozenOrca commit**: 49232549d1
- **狀態**: ✅ 成功

---

### 6. d55f016568 - Fix grid lines origin for multiple plates

- **作者**: yw4z <ywsyildiz@gmail.com>
- **日期**: 2025-09-23 04:30:29 +0300
- **優先級**: ⭐⭐⭐⭐
- **類別**: UI 修復
- **衝突**: ❌ 無
- **變更摘要**: 修復多板情況下的網格線原點
- **PhrozenOrca commit**: 5f74b54f52
- **狀態**: ✅ 成功

---

### 7. 737948be1f - [Profiles] Fix bed_exclude_area excluding the whole bed on Anycubic Kobra 3

- **作者**: TBD
- **日期**: TBD
- **優先級**: ⭐⭐⭐⭐
- **類別**: Profile 修復
- **衝突**: ⚠️ 檔案刪除衝突
  - 檔案: `resources/profiles/Anycubic/machine/*.json`
  - 原因: PhrozenOrca 刪除了 Anycubic 機台 profiles（非 Phrozen 機台）
  - 解決: **跳過** - 此修復僅針對 Anycubic 機台，對 PhrozenOrca 無影響
- **變更摘要**: 修復 Anycubic Kobra 3 的 bed_exclude_area 問題
- **PhrozenOrca commit**: ⏭️ 跳過
- **狀態**: ⏭️ 跳過（非 Phrozen 機台）

---

### 8. e5243be866 - fix a regression bug that wrong printer model for Prusa MK3S and MINI in 2.3.1 beta

- **作者**: TBD
- **日期**: TBD
- **優先級**: ⭐⭐⭐⭐
- **類別**: 回歸 bug 修復
- **衝突**: ⚠️ 檔案刪除衝突
  - 檔案: `resources/profiles/Prusa/*.json`
  - 原因: PhrozenOrca 刪除了 Prusa 機台 profiles（非 Phrozen 機台）
  - 解決: **跳過** - 此修復僅針對 Prusa 機台，對 PhrozenOrca 無影響
- **變更摘要**: 修復 Prusa MK3S 和 MINI 機台型號錯誤的回歸 bug
- **PhrozenOrca commit**: ⏭️ 跳過
- **狀態**: ⏭️ 跳過（非 Phrozen 機台）

---

## 統計摘要

### 總體進度

- **總計劃 commits**: 8（第一批 2 個 + 第二批 6 個）
- **已成功**: 5
- **已跳過**: 3（1 個複雜衝突 + 2 個非 Phrozen 機台）
- **完成率**: 62.5%（5/8）

### 衝突統計

- **遇到衝突**: 5
- **成功解決**: 3
- **跳過**: 2
- **解決率**: 60%

### 變更統計

- **總檔案變更**: 7
- **總插入行數**: ~50
- **總刪除行數**: ~15

---

## 跳過的 Commits 說明

### 1. 78eb3b464f - FillRectilinear 優化

**跳過原因**: 
- 涉及複雜的參數重命名和邏輯重構
- 需要更深入的理解和測試
- 建議在單獨的 PR 中處理

**後續處理建議**:
- 手動審查 OrcaSlicer 的變更
- 測試對 PhrozenOrca 切片品質的影響
- 如果重要，可以單獨 cherry-pick 並仔細測試

### 2. 737948be1f & e5243be866 - Profile 修復

**跳過原因**:
- 僅針對非 Phrozen 機台（Anycubic, Prusa）
- PhrozenOrca 已刪除這些機台的 profiles
- 對 Phrozen Arco 機台無影響

**後續處理建議**:
- 無需處理，這些修復與 PhrozenOrca 無關

---

## 注意事項

1. ✅ 每個成功的 cherry-pick 都已記錄 commit hash
2. ⚠️ 跳過的 commit 78eb3b464f 可能需要後續評估
3. ✅ 所有衝突都已妥善處理或記錄跳過原因
4. 📝 建議在合併到主分支前進行完整測試
