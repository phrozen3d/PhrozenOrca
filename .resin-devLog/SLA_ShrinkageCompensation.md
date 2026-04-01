# SLA Shrinkage Compensation 實作記錄

**建立日期**: 2026-04-01
**分支**: phrozen-resin-dev
**涉及檔案**:
- [SLAPrint.hpp](../src/libslic3r/SLAPrint.hpp)
- [SLAPrint.cpp](../src/libslic3r/SLAPrint.cpp)

---

## 一、修改原因

`shrinkage_compensation_x/y/z` 這組參數已在 UI 中顯示，並儲存於 SLA 製程 profile（`s_Preset_sla_print_options`），但在 SLA 切片流程中完全無作用——切出的 PRZ 影像尺寸不受任何影響。

根本原因有三：

1. **型別系統孤兒**：這三個參數定義在 `PrintObjectConfig`（FDM 用），而非任何 SLA typed struct（`SLAPrintObjectConfig` / `SLAMaterialConfig` / `SLAPrinterConfig`）。`SLAPrint::apply()` 只將 `DynamicPrintConfig` diff 到這四個 typed struct；不屬於任何 struct 的 key 會被靜默忽略。

2. **沒有讀取路徑**：`sla_trafo()` 只從 `relative_correction()`（讀取 `relative_correction_x/y/z` 和 `material_correction_x/y/z`）取得縮放係數，沒有地方讀取 shrinkage 值。

3. **Trafo 不觸發更新**：當製程 config 改變時，`set_trafo()` 只在物件幾何改變（`sla_trafo_differs` 偵測）或首次建立時才重新計算，純 config 改變不會觸發更新。

---

## 二、參數說明

這組參數與 `relative_correction_x/y/z` 功能相同（縮放補正），但層級不同：

| 參數 | 所在層級 | Config struct | 說明 |
|------|---------|--------------|------|
| `relative_correction_x/y/z` | 機台（Printer） | `SLAPrinterConfig` | 機台硬體補正，直接乘數（1.0 = 不縮放） |
| `material_correction_x/y/z` | 材料（Material） | `SLAMaterialConfig` | 材料收縮補正 |
| `shrinkage_compensation_x/y/z` | 製程（Process） | `PrintObjectConfig`（FDM） | 製程層補正，百分比（100 = 不縮放） |
| `shrinkage_compensation` | 製程（Process） | `PrintObjectConfig`（FDM） | 開關，false 時以上三個值不生效 |

`relative_correction()` 將機台與材料兩層補正合併為 `Vec3d`，`sla_trafo()` 用此 Vec3d 對物件做 scale。本次修改在這個 Vec3d 上再乘以 shrinkage 補正，達成三層串接。

---

## 三、修改方式

由於 shrinkage 參數不屬於任何 SLA typed struct，無法用 typed struct diff 機制偵測變化，採用**手動快取**策略：

1. 在 `SLAPrint` 加入 4 個 private member 變數，快取製程 preset 中的 shrinkage 值。
2. `apply()` 中從 `DynamicPrintConfig` 動態讀取（`opt<T>()`），偵測到變化時更新快取並手動重算所有物件的 trafo。
3. `sla_trafo()` 讀快取成員，對 `corr` Vec3d 做額外 scale。

---

## 四、具體修改

### SLAPrint.hpp — 新增 4 個 private member

**位置**：`SLAPrint` class 的 private 區段，`m_default_object_config` 之後（行 ~532）

```cpp
// Shrinkage compensation values from the process preset (percentage, 100 = no change).
// Stored separately because they live in PrintObjectConfig (FDM), not SLAPrintObjectConfig.
bool   m_shrinkage_compensation        = false;
double m_shrinkage_compensation_x      = 100.0;
double m_shrinkage_compensation_y      = 100.0;
double m_shrinkage_compensation_z      = 100.0;
```

---

### SLAPrint.cpp — sla_trafo() 讀取快取

**位置**：`SLAPrint::sla_trafo()`（行 ~187），在 `relative_correction()` 之後立即加入

```cpp
Vec3d corr = this->relative_correction();

// Apply shrinkage compensation from process preset (cached in m_shrinkage_compensation*).
if (m_shrinkage_compensation) {
    corr.x() *= m_shrinkage_compensation_x / 100.0;
    corr.y() *= m_shrinkage_compensation_y / 100.0;
    corr.z() *= m_shrinkage_compensation_z / 100.0;
}
```

`relative_correction()` 原本回傳的 Vec3d 已包含機台和材料兩層補正；此處再乘以製程層的 shrinkage，三層串接後傳入後續的 `trafo.scale(corr)`。

---

### SLAPrint.cpp — apply() 偵測變化並更新 trafo

**位置**：`apply()` 中，`m_default_object_config.apply_only()` 之後，`if (m_printer) m_printer->apply(...)` 之前（行 ~307）

```cpp
// Shrinkage compensation lives in PrintObjectConfig (FDM), not SLAPrintObjectConfig,
// so we read it from the full DynamicPrintConfig and cache it manually.
{
    bool   sc   = false;
    double sc_x = 100.0, sc_y = 100.0, sc_z = 100.0;
    if (const auto *v = config.opt<ConfigOptionBool>("shrinkage_compensation"))
        sc = v->value;
    if (const auto *v = config.opt<ConfigOptionFloat>("shrinkage_compensation_x"))
        sc_x = v->value;
    if (const auto *v = config.opt<ConfigOptionFloat>("shrinkage_compensation_y"))
        sc_y = v->value;
    if (const auto *v = config.opt<ConfigOptionFloat>("shrinkage_compensation_z"))
        sc_z = v->value;
    if (sc != m_shrinkage_compensation || sc_x != m_shrinkage_compensation_x ||
        sc_y != m_shrinkage_compensation_y || sc_z != m_shrinkage_compensation_z) {
        m_shrinkage_compensation   = sc;
        m_shrinkage_compensation_x = sc_x;
        m_shrinkage_compensation_y = sc_y;
        m_shrinkage_compensation_z = sc_z;
        // Force trafo recalculation for all objects on next process().
        update_apply_status(this->invalidate_step(slapsMergeSlicesAndEval));
        for (SLAPrintObject *obj : m_objects) {
            update_apply_status(obj->invalidate_all_steps());
            obj->set_trafo(sla_trafo(*obj->m_model_object),
                           obj->m_model_object->instances.front()->is_left_handed());
        }
    }
}
```

**為什麼需要手動呼叫 `set_trafo()`**：`SLAPrint::apply()` 的標準流程只在偵測到 instance 幾何變化（`sla_trafo_differs`）或新物件建立時才更新 trafo。純 config 改變不會觸發這條路徑，因此必須在偵測到 shrinkage 值改變時手動更新所有現有物件的 trafo。

新物件（在此 block 執行後才加入的）不受影響——建立時會呼叫 `sla_trafo()`，此時 `m_shrinkage_compensation*` 快取已是最新值。

---

## 五、縮放串接關係

完整縮放計算流程（`sla_trafo()` 內）：

```
relative_correction()
    = relative_correction_x/y/z (SLAPrinterConfig)
    * material_correction_x/y/z (SLAMaterialConfig)
        ↓
corr *= shrinkage_compensation_x/y/z / 100.0   ← 本次新增
（僅當 shrinkage_compensation == true 時）
        ↓
trafo.scale(corr)
```

---

## 六、未涉及的修改（刻意不做）

| 項目 | 原因 |
|------|------|
| `SLAPrint::invalidate_state_by_config_options()` 的 `steps_full` | 此函數處理 printer/material config 層的 key，shrinkage 屬於 process 層，加入這裡反而錯誤 |
| `SLAPrintObject::invalidate_state_by_config_options()` | 此函數只處理 `SLAPrintObjectConfig` 中的 key；shrinkage 不在該 struct，永遠不會被 diff 進來 |
| `PrintConfig.hpp` / `PrintConfig.cpp` | 參數本身已存在（定義在 `PrintObjectConfig`）；UI 也已正確顯示，無需修改 |

---

## 七、驗證方式

設定 `shrinkage_compensation = true`，`shrinkage_compensation_x = 50`（其餘為 100），切片並匯出 PRZ：
- X 方向的切層輪廓應縮小為原本的 50%
- Y、Z 方向輪廓不受影響
