# SLA 補正參數實作記錄（Shrinkage & Tolerance Compensation）

**建立日期**: 2026-04-01（Shrinkage）、2026-04-02（Tolerance）
**分支**: phrozen-resin-dev
**涉及檔案**:
- [SLAPrint.hpp](../src/libslic3r/SLAPrint.hpp)
- [SLAPrint.cpp](../src/libslic3r/SLAPrint.cpp)
- [SLAPrintSteps.cpp](../src/libslic3r/SLAPrintSteps.cpp)（Tolerance 專用）

---

## 核心問題：FDM 參數在 SLA 中的孤兒困境

兩組補正參數皆定義在 `PrintObjectConfig`（FDM 用），而非 SLA typed struct（`SLAPrintObjectConfig` / `SLAMaterialConfig` / `SLAPrinterConfig`）。`SLAPrint::apply()` 只將 `DynamicPrintConfig` diff 到這四個 typed struct；不屬於任何 struct 的 key 會被**靜默忽略**，導致 UI 設定對 SLA 切片毫無影響。

**通用解法**：手動快取策略
1. 在 `SLAPrint` 加入 private member 快取製程值。
2. `apply()` 從 `DynamicPrintConfig` 動態讀取，偵測到變化時更新快取並失效相關步驟。
3. 切片步驟直接讀快取值。

| 功能 | 作用層面 | 失效步驟 | 需要更新 trafo？ |
|------|---------|---------|--------------|
| Shrinkage Compensation | 3D trafo（mesh scale） | `slaposObjectSlice` + 手動 `set_trafo()` | 是 |
| Tolerance Compensation | 2D slice 輪廓偏移 | `slaposObjectSlice` | 否 |

---

---

# 一、Shrinkage Compensation（縮放補正）

## 1.1 修改原因

`shrinkage_compensation_x/y/z` 已在 UI 顯示並儲存於 SLA 製程 profile，但 SLA 切片流程中完全無作用。根本原因有三：

1. **型別系統孤兒**：參數在 `PrintObjectConfig`（FDM），SLA apply 機制無法讀取。
2. **沒有讀取路徑**：`sla_trafo()` 只從 `relative_correction()` 取得縮放係數。
3. **Trafo 不觸發更新**：純 config 改變不會觸發 `set_trafo()` 重算。

## 1.2 參數說明

| 參數 | 所在層級 | Config struct | 說明 |
|------|---------|--------------|------|
| `relative_correction_x/y/z` | 機台（Printer） | `SLAPrinterConfig` | 機台硬體補正，直接乘數（1.0 = 不縮放） |
| `material_correction_x/y/z` | 材料（Material） | `SLAMaterialConfig` | 材料收縮補正 |
| `shrinkage_compensation_x/y/z` | 製程（Process） | `PrintObjectConfig`（FDM） | 製程層補正，百分比（100 = 不縮放） |
| `shrinkage_compensation` | 製程（Process） | `PrintObjectConfig`（FDM） | 開關，false 時以上三個值不生效 |

## 1.3 具體修改

### SLAPrint.hpp — 新增 4 個 private member（行 ~534）

```cpp
// Shrinkage compensation values from the process preset (percentage, 100 = no change).
// Stored separately because they live in PrintObjectConfig (FDM), not SLAPrintObjectConfig.
bool   m_shrinkage_compensation        = false;
double m_shrinkage_compensation_x      = 100.0;
double m_shrinkage_compensation_y      = 100.0;
double m_shrinkage_compensation_z      = 100.0;
```

### SLAPrint.cpp — sla_trafo() 讀取快取（行 ~187）

```cpp
Vec3d corr = this->relative_correction();

// Apply shrinkage compensation from process preset (cached in m_shrinkage_compensation*).
if (m_shrinkage_compensation) {
    corr.x() *= m_shrinkage_compensation_x / 100.0;
    corr.y() *= m_shrinkage_compensation_y / 100.0;
    corr.z() *= m_shrinkage_compensation_z / 100.0;
}
```

### SLAPrint.cpp — apply() 快取並手動更新 trafo（行 ~307）

```cpp
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
        update_apply_status(this->invalidate_step(slapsMergeSlicesAndEval));
        for (SLAPrintObject *obj : m_objects) {
            update_apply_status(obj->invalidate_all_steps());
            obj->set_trafo(sla_trafo(*obj->m_model_object),
                           obj->m_model_object->instances.front()->is_left_handed());
        }
    }
}
```

**為什麼需要手動呼叫 `set_trafo()`**：SLA apply 流程只在偵測到 instance 幾何變化（`sla_trafo_differs`）或新物件建立時才更新 trafo。純 config 改變不觸發此路徑，必須手動更新。

## 1.4 縮放串接關係

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

## 1.5 未涉及的修改（刻意不做）

| 項目 | 原因 |
|------|------|
| `SLAPrint::invalidate_state_by_config_options()` 的 `steps_full` | shrinkage 屬於 process 層，加入這裡反而錯誤 |
| `SLAPrintObject::invalidate_state_by_config_options()` | 此函數只處理 `SLAPrintObjectConfig` 中的 key |
| `PrintConfig.hpp` / `PrintConfig.cpp` | 參數本身已存在，UI 也已正確顯示 |

## 1.6 驗證方式

設定 `shrinkage_compensation = true`，`shrinkage_compensation_x = 50`（其餘為 100），切片並匯出 PRZ：
- X 方向的切層輪廓應縮小為原本的 50%
- Y、Z 方向輪廓不受影響

---

---

# 二、Tolerance Compensation（公差補償）

## 2.1 功能說明

公差補償對每一切層的 2D 輪廓做獨立的內孔／外輪廓偏移，補正機台或材料在孔徑與外形尺寸上的系統性偏差：

| 參數 | 符號 | 正值效果 | 負值效果 |
|------|------|---------|---------|
| `tolerance_compensation_a` | a（內孔） | 孔洞縮小（solid 長入孔） | 孔洞放大 |
| `tolerance_compensation_b` | b（外徑） | 外輪廓外擴 | 外輪廓收縮 |
| `bottom_tolerance_compensation_a` | a（底層內孔） | 同上，作用於底層 | 同上 |
| `bottom_tolerance_compensation_b` | b（底層外徑） | 同上，作用於底層 | 同上 |

「底層」定義為層索引 `< bottom_layer_count`（`coInt`，預設 6）。

## 2.2 PrintConfig.cpp — 參數定義（行 ~6954）

| 參數名稱 | 類型 | 預設值 |
|---------|------|--------|
| `tolerance_compensation` | `coBool` | `true` |
| `tolerance_compensation_a` | `coFloat` | `0` mm |
| `tolerance_compensation_b` | `coFloat` | `0` mm |
| `bottom_tolerance_compensation` | `coBool` | `true` |
| `bottom_tolerance_compensation_a` | `coFloat` | `0` mm |
| `bottom_tolerance_compensation_b` | `coFloat` | `0` mm |

## 2.3 SLAPrint.hpp — 7 個 private member（行 ~541）

```cpp
// Tolerance compensation (inner/outer diameter) from process preset.
// Stored separately because they live in PrintConfig (FDM), not SLA config.
bool   m_tolerance_compensation             = false;
double m_tolerance_compensation_a           = 0.0;
double m_tolerance_compensation_b           = 0.0;
bool   m_bottom_tolerance_compensation      = false;
double m_bottom_tolerance_compensation_a    = 0.0;
double m_bottom_tolerance_compensation_b    = 0.0;
int    m_tolerance_bottom_layer_count       = 0;
```

## 2.4 SLAPrint.cpp — apply() 快取與失效（行 ~336）

```cpp
// Tolerance compensation lives in PrintConfig (FDM), not SLA config → cache manually.
{
    bool   tc   = true,  btc  = true;   // match PrintConfig defaults (tolerance_compensation default = true)
    double tc_a = 0.0,  tc_b  = 0.0, btc_a = 0.0, btc_b = 0.0;
    int    blc  = 0;
    if (const auto *v = config.opt<ConfigOptionBool> ("tolerance_compensation"))          tc   = v->value;
    if (const auto *v = config.opt<ConfigOptionFloat>("tolerance_compensation_a"))        tc_a = v->value;
    if (const auto *v = config.opt<ConfigOptionFloat>("tolerance_compensation_b"))        tc_b = v->value;
    if (const auto *v = config.opt<ConfigOptionBool> ("bottom_tolerance_compensation"))   btc  = v->value;
    if (const auto *v = config.opt<ConfigOptionFloat>("bottom_tolerance_compensation_a")) btc_a = v->value;
    if (const auto *v = config.opt<ConfigOptionFloat>("bottom_tolerance_compensation_b")) btc_b = v->value;
    if (const auto *v = config.opt<ConfigOptionInt>  ("bottom_layer_count"))              blc  = v->value;
    if (tc   != m_tolerance_compensation            || tc_a  != m_tolerance_compensation_a  ||
        tc_b != m_tolerance_compensation_b          || btc   != m_bottom_tolerance_compensation ||
        btc_a!= m_bottom_tolerance_compensation_a  || btc_b != m_bottom_tolerance_compensation_b ||
        blc  != m_tolerance_bottom_layer_count) {
        m_tolerance_compensation            = tc;
        m_tolerance_compensation_a          = tc_a;
        m_tolerance_compensation_b          = tc_b;
        m_bottom_tolerance_compensation     = btc;
        m_bottom_tolerance_compensation_a   = btc_a;
        m_bottom_tolerance_compensation_b   = btc_b;
        m_tolerance_bottom_layer_count      = blc;
        update_apply_status(this->invalidate_step(slapsMergeSlicesAndEval));
        for (SLAPrintObject *obj : m_objects)
            update_apply_status(obj->invalidate_step(slaposObjectSlice));
    }
}
```

**預設值陷阱**：local bool 必須初始化為 `true`（與 PrintConfig default 一致）。若誤用 `false`，當 key 不在 `DynamicPrintConfig` 中時，快取會錯誤地記為 `false`，導致即使有非零 a/b 值也永遠不作用。

## 2.5 SLAPrintSteps.cpp — apply_printer_corrections() 內的實作（行 ~121）

### Lambda：apply_tc_layer

```cpp
auto apply_tc_layer = [](ExPolygons &layer_slices, coord_t ca, coord_t cb) {
    if (ca == 0 && cb == 0) return;
    ExPolygons result;
    result.reserve(layer_slices.size());
    for (const ExPolygon &ep : layer_slices) {
        Polygons new_contour = offset(ep.contour, float(cb));
        Polygons hole_solids;
        for (Polygon h : ep.holes) {
            h.reverse();                                    // CW hole → CCW solid area
            append(hole_solids, offset(h, float(-ca)));     // positive a = shrink hole area
        }
        append(result, diff_ex(new_contour, hole_solids));
    }
    layer_slices = std::move(result);
};
```

**設計細節**：
- `offset(ep.contour, float(cb))`：外輪廓 CCW → 正 delta = 外擴。
- hole 先 `reverse()` 轉為 CCW solid，再 `offset(h, float(-ca))`（注意負號）：a > 0 → solid 縮小 → 孔洞縮小。
- `diff_ex(new_contour, {})` 空 clip 情形：Clipper ctDifference 中所有 subject 邊 WindCnt2=0，全部貢獻輸出，即正確回傳 subject 本身。

### 一般層（行 ~141，層索引 ≥ blc）

```cpp
const int blc = m_print->m_tolerance_bottom_layer_count;
{
    coord_t ca = scaled(m_print->m_tolerance_compensation_a);
    coord_t cb = scaled(m_print->m_tolerance_compensation_b);
    if (m_print->m_tolerance_compensation && (ca != 0 || cb != 0)) {
        for (size_t i = (size_t)std::max(0, blc); i < po.m_slice_index.size(); ++i) {
            size_t idx = po.m_slice_index[i].get_slice_idx(o);
            if (idx < slices.size())
                apply_tc_layer(slices[idx], ca, cb);
        }
    }
}
```

### 底層（行 ~154，層索引 < blc）

```cpp
{
    coord_t ca = scaled(m_print->m_bottom_tolerance_compensation_a);
    coord_t cb = scaled(m_print->m_bottom_tolerance_compensation_b);
    if (m_print->m_bottom_tolerance_compensation && (ca != 0 || cb != 0)) {
        size_t n = std::min((size_t)std::max(0, blc), po.m_slice_index.size());
        for (size_t i = 0; i < n; ++i) {
            size_t idx = po.m_slice_index[i].get_slice_idx(o);
            if (idx < slices.size())
                apply_tc_layer(slices[idx], ca, cb);
        }
    }
}
```

## 2.6 apply_printer_corrections() 完整執行順序

```
apply_printer_corrections(po, o)
    │
    ├─ 1. absolute_correction          （全層均勻外擴/收縮）
    ├─ 2. tolerance_compensation       （一般層，layers >= blc）← 本次新增
    ├─ 3. bottom_tolerance_compensation（底層，layers < blc）  ← 本次新增
    ├─ 4. elephant_foot_compensation   （faded layers）
    └─ 5. zcorrection                  （僅 soModel）
```

> TC 在 EFC 之前執行：EFC 疊加在 TC 調整後的輪廓上計算。

## 2.7 驗證方式

| 測試 | 設定 | 預期結果（2D Layer View） |
|------|------|------------------------|
| 一般層外擴 | `tolerance_compensation_b = 0.5mm` | 層 6 以上外輪廓外擴 0.5mm |
| 底層外擴 | `bottom_tolerance_compensation_b = 0.5mm` | 層 0-5 外輪廓外擴 0.5mm |
| 孔洞縮小 | `tolerance_compensation_a = 0.3mm` | 一般層孔洞直徑縮小 0.6mm |
| Bool 開關 | `tolerance_compensation = false` | 即使 b = 0.5mm 也無效果 |
| 負值 | `tolerance_compensation_b = -0.5mm` | 外輪廓收縮 0.5mm |
