# Step 3.1：SupportTreeStrategies.hpp + 策略模式基礎

## 完成狀態：✅

---

## 修改摘要

### 命名決策

**SLA enum 命名衝突 → 採用 PrusaSlicer 命名**：

| 層次 | 選擇 | 理由 |
|------|------|------|
| Config/序列化層 | 保留 `SLASupportTreeType` (`sla_stt_Default`/`sla_stt_Branching`) | 避免宏系統（CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS）的複雜度 |
| 內部 SLA 演算法層 | 使用 `sla::SupportTreeType` (`Default`/`Branching`/`Organic`) | PrusaSlicer 命名，Step 3.3 可直接對接 |
| 轉換點 | `SLAPrint.cpp::make_support_cfg()` | switch 映射，一處轉換 |

---

## 新增 / 修改的檔案

### 1. 新增：SupportTreeStrategies.hpp（18 行）

**路徑**：`PhrozenOrca/src/libslic3r/SLA/SupportTreeStrategies.hpp`

定義 `sla::SupportTreeType` enum class，與 PrusaSlicer 完全一致：

```cpp
enum class SupportTreeType { Default, Branching, Organic };
```

注意：`PillarConnectionMode` 已在 `SupportTree.hpp` 定義，此檔案不重複定義。

---

### 2. 修改：SupportTree.hpp

**`#include "SupportTreeStrategies.hpp"` 加入**（include 區塊末尾）

**`SupportTreeConfig` struct 新增兩個欄位**：

```cpp
// Step 3.1: Support tree building strategy.
SupportTreeType tree_type = SupportTreeType::Default;

// ...（原有欄位）...

// Step 3.1: Max weight on model support (Branching strategy).
double max_weight_on_model_support = 10.;
```

---

### 3. 修改：PrintConfig.cpp（support_tree_type 定義）

**補全空白 labels，取消 label/tooltip 註解**：

```cpp
// 修改前：
def->enum_labels.push_back(" ");  // ← 空白
def->enum_labels.push_back(" ");  // ← 空白

// 修改後：
def->label = L("Support tree type");
def->tooltip = L("Support tree building strategy...");
def->enum_labels.push_back(L("Default"));
def->enum_labels.push_back(L("Branching"));
```

---

### 4. 修改：SLAPrint.cpp — make_support_cfg()

**重構前（單一路徑，無策略選擇）**：
```cpp
scfg.enabled = c.supports_enable.getBool();
scfg.head_front_radius_mm = 0.5*c.support_head_front_diameter.getFloat();
// ... 所有參數讀 Default support_* key ...
```

**重構後（策略 switch）**：

```cpp
// 1. 新增 helper function（檔案頂層，make_support_cfg 之前）
static sla::PillarConnectionMode map_pillar_connection_mode(int m) { ... }

// 2. make_support_cfg() 內部
// a. 讀取並映射 tree_type
switch (c.support_tree_type.value) {
case sla_stt_Branching: scfg.tree_type = sla::SupportTreeType::Branching; break;
default:                scfg.tree_type = sla::SupportTreeType::Default; break;
}

// b. 依策略讀取對應參數
switch (scfg.tree_type) {
case sla::SupportTreeType::Default:
default: { /* support_* 參數 */ }
case sla::SupportTreeType::Branching:
case sla::SupportTreeType::Organic: { /* branchingsupport_* 參數 */ }
}
```

**新增**：`scfg.max_weight_on_model_support` 在兩個分支都有填入。

---

### 5. 修改：SupportTree.cpp — SupportTree::create()

**重構前（單一路徑）**：
```cpp
SupportTreeBuildsteps::execute(*builder, sm);
```

**重構後（策略 switch）**：
```cpp
switch (sm.cfg.tree_type) {
case SupportTreeType::Branching:
case SupportTreeType::Organic:
    BOOST_LOG_TRIVIAL(warning) << "[SLA] Branching/Organic not yet implemented, falling back to Default.";
    [[fallthrough]];
case SupportTreeType::Default:
default:
    SupportTreeBuildsteps::execute(*builder, sm);
    break;
}
```

---

## 架構說明

```
Config 層 (SLASupportTreeType)          內部 SLA 層 (sla::SupportTreeType)
  sla_stt_Default  ──────────────────►  SupportTreeType::Default
  sla_stt_Branching ─────────────────►  SupportTreeType::Branching
       ↑                                        ↑
  PrintConfig.cpp                        make_support_cfg() 轉換點
  (序列化/反序列化)                      SupportTree.cpp 使用
```

---

## FDM 影響

**零影響**：
- `SupportTreeStrategies.hpp` 是新增檔案，不改變任何現有 include
- `SupportTree.hpp` 只新增欄位（有預設值），不影響 FDM code path
- `SLAPrint.cpp` 修改完全在 SLA-only 路徑（`SLAPrintObjectConfig` 是 SLA 專用）
- `SupportTree.cpp` 修改完全在 SLA-only 路徑（FDM 使用不同支撐系統）

---

## 修改清單

| 檔案 | 操作 | 修改內容 |
|------|:----:|---------|
| `PhrozenOrca/src/libslic3r/SLA/SupportTreeStrategies.hpp` | 新增 | `enum class SupportTreeType` |
| `PhrozenOrca/src/libslic3r/SLA/SupportTree.hpp` | 修改 | include + `tree_type` + `max_weight_on_model_support` 欄位 |
| `PhrozenOrca/src/libslic3r/PrintConfig.cpp` | 修改 | `support_tree_type` label/tooltip + enum labels 補全 |
| `PhrozenOrca/src/libslic3r/SLAPrint.cpp` | 修改 | `make_support_cfg()` 重構：helper + tree_type 映射 + 策略 switch |
| `PhrozenOrca/src/libslic3r/SLA/SupportTree.cpp` | 修改 | `SupportTree::create()` 策略 switch 框架 |

---

## 測試結果

| # | 測試項目 | 結果 |
|---|----------|:----:|
| T1 | 編譯（無錯誤） | ✅ |
| T2 | SLA printer 模式下執行切層 | ✅ |
| T3 | 輸出 .sl1 檔案完整流程 | ✅ |

---

## 下一步

- **Step 3.2**：從 PrusaSlicer 複製 `BranchingTree/` 目錄（4 個檔案 + CMake）
- **Step 3.3**：移植 `BranchingTreeSLA.hpp/cpp`，將 `SupportTree.cpp` 中的 fallback 替換為實際呼叫
