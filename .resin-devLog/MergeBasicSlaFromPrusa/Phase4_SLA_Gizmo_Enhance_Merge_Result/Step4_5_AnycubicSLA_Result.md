# Step 4.5: AnycubicSLA 格式支援 — 結果報告

**日期**: 2026-02-28
**狀態**: ✅ 編譯成功、執行成功

---

## 摘要

將 PrusaSlicer 的 AnycubicSLA 格式支援移植至 PhrozenOrca，
新增 Anycubic Photon Mono 系列印表機的 `.pwmo`、`.pwmx`、`.pwms` 輸出格式。

---

## 新增檔案

### 1. `PhrozenOrca/src/libslic3r/Format/AnycubicSLA.hpp`

**來源**: PrusaSlicer verbatim copy + PhrozenOrca 相容性修正

**主要內容**:
- 版本常數定義：`ANYCUBIC_SLA_FORMAT_VERSION_1 = 1`, `515`, `516`, `517`
- `AnycubicSLAArchive` 類別繼承 `SLAArchiveWriter`
- 工廠函數 `anycubic_sla_format()` / `anycubic_sla_format_versioned()`

**PhrozenOrca 相容性修正**:
```cpp
// PhrozenOrca: SLAArchiveWriter requires apply() implementation
void apply(const SLAPrinterConfig &cfg) override
{
    auto diff = m_cfg.diff(cfg);
    if (!diff.empty()) {
        m_cfg.apply_only(cfg, diff);
        m_layers = {};
    }
}
```

**原因**: PhrozenOrca 的 `SLAArchiveWriter` 新增了 `virtual void apply() = 0` 純虛函數（PrusaSlicer 沒有），所有子類別必須實作。採用與 `SL1Archive` 相同的 pattern。

---

### 2. `PhrozenOrca/src/libslic3r/Format/AnycubicSLA.cpp`

**來源**: PrusaSlicer verbatim copy + include path 修正

**Include 路徑修正**:
```cpp
// PrusaSlicer (原):
#include "LocalesUtils.hpp"

// PhrozenOrca (修正):
#include "libslic3r/LocalesUtils.hpp"
```

**主要實作內容**:
- RLE 編碼器（Anycubic 專用）
- 二進位格式結構體（intro / header / preview / layers）
- `fill_header()` — 填充印表機及列印參數
- `fill_preview()` — 縮圖 RLE 編碼
- `export_print()` — 完整輸出 `.pwmo`/`.pwmx`/`.pwms` 格式

---

## 修改檔案

### 3. `PhrozenOrca/src/libslic3r/Format/SLAArchiveFormatRegistry.cpp`

新增 include 及 3 個格式條目：

```cpp
#include "AnycubicSLA.hpp"

// Step 4.5: Anycubic photon printer formats
anycubic_sla_format("pwmo", "Photon Mono"),
anycubic_sla_format("pwmx", "Photon Mono X"),
anycubic_sla_format("pwms", "Photon Mono SE"),
```

### 4. `PhrozenOrca/src/libslic3r/CMakeLists.txt`

新增兩個檔案至編譯清單：

```cmake
Format/AnycubicSLA.hpp
Format/AnycubicSLA.cpp
```

---

## 建置錯誤修正記錄

### 錯誤: C2259 — AnycubicSLAArchive 無法實例化抽象類別

**原因**: PhrozenOrca 在 `SLAArchiveWriter` 中新增了純虛函數 `apply()`，PrusaSlicer 原始碼沒有此函數，移植後缺少實作。

**影響**: 4 個 C2259 錯誤 + 2 個 C2440 cascade 錯誤

**修正**: 在 `AnycubicSLA.hpp` 新增 `apply()` override（參照 `SL1Archive` pattern）

---

## 支援的格式（已啟用）

| 副檔名 | 印表機 | 格式版本 | 狀態 |
|--------|--------|----------|------|
| `.pwmo` | Photon Mono | v1 | ✅ 已啟用 |
| `.pwmx` | Photon Mono X | v1 | ✅ 已啟用 |
| `.pwms` | Photon Mono SE | v1 | ✅ 已啟用 |

---

## 使用者新增功能

### 切片輸出流程

1. 在 Printer 下拉選單選擇 Photon Mono 系列印表機
2. 切片完成後，選擇「Export」→ 直接輸出對應格式（`.pwmo` / `.pwmx` / `.pwms`）
3. 將輸出檔案傳至印表機 USB 即可列印，無需其他轉換工具

### 格式內建功能

| 功能 | 說明 |
|------|------|
| RLE 壓縮 | 每層切片影像使用 Anycubic 專用 RLE 編碼，縮小檔案大小 |
| 縮圖嵌入 | 縮圖影像寫入檔案頭，印表機 UI 可預覽模型 |
| 列印參數寫入 | 曝光時間、抬升速度、底部層數等參數內嵌於檔案 |
| 多版本格式 | 版本常數 v515/v516/v517 已定義，可後續擴充支援更多機型 |

---

## 未來擴充：啟用更多機型

版本常數已全部定義於 `AnycubicSLA.hpp`，啟用新機型只需完成以下兩步。

### 步驟一：`SLAArchiveFormatRegistry.cpp` 新增格式條目

```cpp
// 版本 v1（僅需 ANYCUBIC_SLA_FORMAT_VERSION_1）
anycubic_sla_format_versioned("pws",  "Photon / Photon S", ANYCUBIC_SLA_FORMAT_VERSION_1),
anycubic_sla_format_versioned("pw0",  "Photon Zero",       ANYCUBIC_SLA_FORMAT_VERSION_1),
anycubic_sla_format_versioned("pwx",  "Photon X",          ANYCUBIC_SLA_FORMAT_VERSION_1),
anycubic_sla_format_versioned("dlp",  "Photon Ultra",      ANYCUBIC_SLA_FORMAT_VERSION_1),
anycubic_sla_format_versioned("pmsq", "Photon Mono SQ",    ANYCUBIC_SLA_FORMAT_VERSION_1),

// 版本 v515（需 ANYCUBIC_SLA_FORMAT_VERSION_515）
anycubic_sla_format_versioned("pwma", "Photon Mono 4K",           ANYCUBIC_SLA_FORMAT_VERSION_515),
anycubic_sla_format_versioned("pm3",  "Photon M3",                ANYCUBIC_SLA_FORMAT_VERSION_515),
anycubic_sla_format_versioned("pm3m", "Photon M3 Max",            ANYCUBIC_SLA_FORMAT_VERSION_515),
anycubic_sla_format_versioned("pwmb", "Photon Mono X 6K / M3 Plus", ANYCUBIC_SLA_FORMAT_VERSION_515),
anycubic_sla_format_versioned("dl2p", "Photon D2",                ANYCUBIC_SLA_FORMAT_VERSION_515),
anycubic_sla_format_versioned("pmx2", "Photon Mono X2",           ANYCUBIC_SLA_FORMAT_VERSION_515),
anycubic_sla_format_versioned("pm3r", "Photon M3 Premium",        ANYCUBIC_SLA_FORMAT_VERSION_515),
```

> **注意**：各機型實際支援的版本範圍請參考 `AnycubicSLA.cpp` 內的 `fill_header()` 邏輯，
> 或對照 PrusaSlicer 原始 `SLAArchiveFormatRegistry.cpp` 的 commented-out 清單。

### 步驟二：新增對應印表機 Preset 檔案

每個新機型需在 vendor bundle 中新增：
- **Printer preset**（機型解析度、bed size、曝光時間預設值）
- **Process profiles**（層高選項：0.025 / 0.05 / 0.1 mm）
- **Material profiles**（對應樹脂材料）

> 參考路徑：`PhrozenOrca/resources/profiles/`（比照 PrusaResearchSLA.json 結構）

### 各格式版本對應機型速查

| 版本常數 | 值 | 適用機型 |
|----------|:--:|---------|
| `ANYCUBIC_SLA_FORMAT_VERSION_1` | 1 | Photon、Photon S、Photon Zero、Photon X、Photon Mono、Photon Mono SE、Photon Mono SQ、Photon Ultra |
| `ANYCUBIC_SLA_FORMAT_VERSION_515` | 515 | Photon Mono 4K、M3、M3 Max、Mono X 6K、M3 Plus、D2、Mono X2、M3 Premium |
| `ANYCUBIC_SLA_FORMAT_VERSION_516` | 516 | 部分 v515 機型的較新韌體 |
| `ANYCUBIC_SLA_FORMAT_VERSION_517` | 517 | 部分 v516 機型的最新韌體 |

---

## FDM 安全性確認

- ✅ 所有修改限定於 `libslic3r/Format/` 路徑下的 SLA 格式程式碼
- ✅ `SLAArchiveFormatRegistry.cpp` 僅新增 SLA 格式條目，不影響 FFF 路徑
- ✅ `CMakeLists.txt` 新增為獨立編譯單元，無 FDM 相依

---

## 驗證結果

- ✅ 編譯成功（無 C2259/C2440 錯誤）
- ✅ 執行成功

---

## 後續步驟

- Step 4.6（如有）或 Phase 4 完成總結
- 如需啟用更多 Anycubic 機型，依照「未來擴充」章節操作即可（核心實作已就緒）
