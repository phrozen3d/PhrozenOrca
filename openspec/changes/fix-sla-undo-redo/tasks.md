## 0. Build 環境前置修正（首次啟用測試時執行）

> 這些是 PhrozenOrca fork 時遺留的 build 問題，與本次修改無關，但必須修正才能執行測試。

- [x] 0.1 在根 `CMakeLists.txt` 補上 `orcaslicer_copy_dlls` macro alias（原名稱在 fork 時被改為 `phrozenorca_copy_dlls`，tests 目錄仍用舊名）
- [x] 0.2 在根 `CMakeLists.txt` 開啟 `SLIC3R_BUILD_TESTS=ON`，確認不影響主程式 target
- [x] 0.3 在 `tests/CMakeLists.txt` 確認 `add_subdirectory(libslic3r)` 已啟用
- [x] 0.4 在 `tests/libslic3r/CMakeLists.txt` 補上 `OpenSSL::Crypto`、`bcrypt.lib`、`Setupapi.lib` link（pre-existing 缺失，`libslic3r` 用 PRIVATE 連結 OpenSSL 不繼承）

**PowerShell 啟用測試 build 的完整步驟（含繁體中文亂碼處理）：**

```powershell
# Step 1：開啟測試並重新 configure（只需執行一次）
cd C:\Dev\PhrozenOrca\build-dbginfo
cmake .. -DSLIC3R_BUILD_TESTS=ON 2>&1 | Out-File -Encoding utf8 cmake_config.txt
Get-Content cmake_config.txt | Select-Object -Last 5

# Step 2：編譯測試目標（設 VSLANG=1033 避免中文亂碼）
$env:VSLANG = "1033"
cmake --build . --target libslic3r_tests --config RelWithDebInfo 2>&1 | Out-File -Encoding utf8 build_log.txt
Get-Content build_log.txt | Select-Object -Last 10

# Step 3：執行 Layer 1 測試
.\tests\libslic3r\RelWithDebInfo\libslic3r_tests.exe "[SLA][UndoRedo][L1]" --order rand
```

## 1. Layer 1 測試 — SLA 資料序列化（先於實作撰寫）

> 測試位置：`tests/libslic3r/test_sla_undo_redo_data.cpp`（非 sla_print，原有 sla_print tests 已損壞）
> `DynamicPrintConfig` 因 cereal polymorphic 限制無法直接測試，改以 `sla::DrainHoles` 為主。

- [x] 1.1 在 `tests/libslic3r/CMakeLists.txt` 加入 `test_sla_undo_redo_data.cpp`
- [x] 1.2 建立 `tests/libslic3r/test_sla_undo_redo_data.cpp`，加入 `sla_drain_holes` cereal round-trip 測試（單洞 pos/normal/radius/height 驗證）
- [x] 1.3 加入多洞 count 與順序保留測試
- [x] 1.4 加入空 vector round-trip 測試（`hollowing config` 因 cereal polymorphic 問題暫不測試）
- [x] 1.5 執行測試確認全數通過（3 test cases，16 assertions，all passed）

## 2. ~~Layer 2 測試 — Gizmo 序列化合約~~（已捨棄）

> **捨棄原因：** stub struct 測試的是我們自己寫的 stub，不是 gizmo 實際程式碼，永遠 pass，效益不足。
> gizmo on_save/on_load 正確性改由 task 3.4 靜態 review 和 task 8 手動驗證確保。

## 3. GLGizmoHollow — 修正 on_save / on_load

- [ ] 3.1 在 `GLGizmoHollow.hpp` 確認 `m_pending_offset`、`m_pending_quality`、`m_pending_closing_d`、`m_enable_hollowing` 四個成員存在且型別為 `float`、`float`、`float`、`bool`
- [ ] 3.2 修改 `GLGizmoHollow.cpp::on_save`：移除假值，改為 `ar(m_pending_offset, m_pending_quality, m_pending_closing_d, m_enable_hollowing)`（4 個欄位）
- [ ] 3.3 修改 `GLGizmoHollow.cpp::on_load`：移除假值，改為 `ar(m_pending_offset, m_pending_quality, m_pending_closing_d, m_enable_hollowing)`，結尾加上 `m_pending_owner = nullptr`
- [ ] 3.4 靜態 review：on_save 與 on_load 欄位型別與順序完全一致（不依賴自動化測試）

## 4. GLGizmoDrill — 新增成員變數

- [ ] 4.1 在 `GLGizmoDrill.hpp` 新增 `float m_radius_before_change = 0.f`
- [ ] 4.2 在 `GLGizmoDrill.hpp` 新增 `float m_height_before_change = 0.f`
- [ ] 4.3 在 `GLGizmoDrill.hpp` 新增 `sla::DrainHoles m_holes_before_change`（型別與 `mo->sla_drain_holes` 一致）

## 5. GLGizmoDrill — 實作 begin_size_change / apply_size_change

- [ ] 5.1 在 `GLGizmoDrill.hpp` 宣告 `begin_size_change(float old_radius, float old_height)` 和 `apply_size_change(const std::string& snapshot_name)`
- [ ] 5.2 在 `GLGizmoDrill.cpp` 實作 `begin_size_change`：若 `m_radius_before_change == 0.f && m_height_before_change == 0.f`，儲存舊值並複製所有洞至 `m_holes_before_change`
- [ ] 5.3 在 `GLGizmoDrill.cpp` 實作 `apply_size_change`：
  - 若無暫存值則直接返回
  - 備份新值 → 還原 `mo->sla_drain_holes` 為 `m_holes_before_change` → `TakeSnapshot` → 重套新值至選取洞 → `set_as_dirty()` → 清除暫存

## 6. GLGizmoDrill — 接入 Diameter slider 與 InputFloat

- [ ] 6.1 在 diameter slider 首次啟動時呼叫 `begin_size_change(m_new_hole_radius, m_new_hole_height)`
- [ ] 6.2 在 diameter slider 的 `deactivated_after_edit` 呼叫 `apply_size_change("Change hole radius")`
- [ ] 6.3 在 diameter InputFloat 的 `IsItemActivated()` 呼叫 `begin_size_change(m_new_hole_radius, m_new_hole_height)`
- [ ] 6.4 在 diameter InputFloat 的 `IsItemDeactivatedAfterEdit()` 分支套用新值後呼叫 `apply_size_change("Change hole radius")`

## 7. GLGizmoDrill — 接入 Depth slider 與 InputFloat

- [ ] 7.1 在 depth slider 首次啟動時呼叫 `begin_size_change(m_new_hole_radius, m_new_hole_height)`
- [ ] 7.2 在 depth slider 的 `deactivated_after_edit` 呼叫 `apply_size_change("Change hole depth")`
- [ ] 7.3 在 depth InputFloat 的 `IsItemActivated()` 呼叫 `begin_size_change(m_new_hole_radius, m_new_hole_height)`
- [ ] 7.4 在 depth InputFloat 的 `IsItemDeactivatedAfterEdit()` 分支套用新值後呼叫 `apply_size_change("Change hole depth")`

## 8. 自動化測試回歸確認

**PowerShell 執行指令（含繁體中文亂碼處理）：**
```powershell
$env:VSLANG = "1033"
cmake --build . --target libslic3r_tests --config RelWithDebInfo 2>&1 | Out-File -Encoding utf8 regression_log.txt
.\tests\libslic3r\RelWithDebInfo\libslic3r_tests.exe "[SLA][UndoRedo][L1]" --order rand
```

- [ ] 8.1 執行上述指令，確認 Layer 1 的 3 個測試在實作後仍全數通過（3 test cases，16 assertions）
- [ ] 8.2 確認 `libslic3r_tests.exe` 輸出包含 `[SLA][UndoRedo][L1]` tag 的測試名稱

## 9. 手動行為驗證

- [ ] 9.1 在 Hollow gizmo 中按 Hollow 兩次，undo 兩次，確認 hollowing 參數正確還原
- [ ] 9.2 在 Hollow gizmo 中按 Hollow 後離開至 prepare，執行 undo，確認 config 還原且 SLA 重算
- [ ] 9.3 在 Drill gizmo 中調整 diameter slider，放開後 undo，確認所有選取洞的 radius 還原
- [ ] 9.4 在 Drill gizmo 中連續修改 diameter 和 depth，undo 兩次，確認各自獨立還原
- [ ] 9.5 確認 add/delete/move 洞的 undo 行為未受影響
- [ ] 9.6 在 SlaSupports gizmo 執行 undo/redo，確認現有功能未退化
