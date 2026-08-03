# OrcaSlicer 測試指南

## 測試框架

- **框架**：Catch2 v2（`tests/catch2/catch.hpp`）
- **自訂 Reporter**：`VerboseConsoleReporter`（定義於 `tests/catch_main.hpp`）— 彩色輸出、計時顯示
- **CMake 整合**：`catch_discover_tests()` 自動發現所有 TEST_CASE
- **測試資料目錄**：`tests/data/`，透過 `TEST_DATA_DIR` 巨集存取

---

## 可編譯的測試套件

下方四個套件預設已啟用（`tests/CMakeLists.txt`）。

### 1. `libslic3r_tests` — 核心引擎測試

**連結庫**：`libslic3r`  
**測試資料**：`tests/data/test_stl/`、`tests/data/test_3mf/`、`tests/data/test_config/`

| 測試檔 | 涵蓋範圍 | 主要 Tag |
|--------|----------|----------|
| `test_geometry.cpp` | 點、線、多邊形、凸包、簡化、Rotcalip | `[Geometry]` `[Rotcalip]` |
| `test_voronoi.cpp` | Voronoi 圖、骨架、偏移、邊緣案例 | `[Voronoi]` `[VoronoiOffset]` |
| `test_indexed_triangle_set.cpp` | 三角網格分割、簡化、邊坍縮 | `[its]` `[its_split]` |
| `test_mutable_priority_queue.cpp` | Skip-heap 優先佇列 | `[MutableSkipHeapPriorityQueue]` |
| `test_clipper_utils.cpp` | Clipper 多邊形裁切遍歷 | `[ClipperUtils]` |
| `test_clipper_offset.cpp` | Clipper 偏移操作 | — |
| `test_config.cpp` | 設定檔解析、序列化驗證 | — |
| `test_3mf.cpp` | 3MF 格式解析 | `[SL1Import]` |
| `test_stl.cpp` | STL 格式解析 | — |
| `test_polygon.cpp` | 多邊形工具函式 | `[Polygon]` |
| `test_mutable_polygon.cpp` | 可變多邊形操作 | — |
| `test_elephant_foot_compensation.cpp` | 象腳補償演算法 | — |
| `test_aabbindirect.cpp` | AABB 樹射線偵測與最近點查詢 | `[AABBIndirect]` |
| `test_meshboolean.cpp` | CGAL 網格布林運算 | `[MeshBoolean]` |
| `test_optimizers.cpp` | 暴力最佳化演算法 | `[Opt]` |
| `test_placeholder_parser.cpp` | G-code 樣板解析 | — |
| `test_timeutils.cpp` | 時間工具函式 | — |

**條件編譯（需 OpenVDB）**：
- `test_hollowing.cpp` — 網格挖空

**已停用（CMakeLists 中以 `#` 註解）**：
- `test_marchingsquares.cpp` — Marching Squares 演算法
- `test_png_io.cpp` — PNG 讀寫

---

### 2. `fff_print_tests` — FFF 熔融堆積列印測試

**連結庫**：`libslic3r`

| 測試檔 | 涵蓋範圍 |
|--------|----------|
| `test_extrusion_entity.cpp` | 擠出路徑物件 |
| `test_fill.cpp` | 填充路徑生成（填充路徑長度）|
| `test_flow.cpp` | 擠出流量計算 |
| `test_gcode.cpp` | G-code 解析與驗證 |
| `test_gcodewriter.cpp` | G-code 輸出生成 |
| `test_model.cpp` | 3D 模型物件處理 |
| `test_print.cpp` | 列印流程編排 |
| `test_printgcode.cpp` | 列印轉 G-code 整合 |
| `test_printobject.cpp` | PrintObject 切片處理 |
| `test_skirt_brim.cpp` | 裙邊與邊緣生成 |
| `test_support_material.cpp` | 支撐材料生成（含 raft）|
| `test_trianglemesh.cpp` | 三角網格處理 |

---

### 3. `libnest2d_tests` — 2D 排版演算法測試

**連結庫**：`libnest2d`  
**測試資料**：`tests/libnest2d/printer_parts.cpp`（內建樣本幾何）

涵蓋：角度/面積計算、凸包、NFP（無碰撞多邊形）、零件排列、裝箱演算法。

| 主要 Tag | 說明 |
|----------|------|
| `[Geometry]` | 幾何計算 |
| `[Nesting]` | 排列與裝箱 |
| `[NotWorking]` | 已知失敗，預設跳過 |

---

### 4. `slic3rutils_tests` — HTTP 工具測試

**連結庫**：`libslic3r_gui` + `libslic3r`（MSVC 需額外連結 `Setupapi.lib`）

涵蓋：SSL 憑證路徑驗證、HTTP Digest 認證、HTTP Basic 認證。

| 主要 Tag | 說明 |
|----------|------|
| `[Http]` | HTTP 功能 |
| `[NotWorking]` | 目前停用的測試 |

---

## 已停用的測試套件

| 套件目錄 | 狀態 | 原因 |
|----------|------|------|
| `tests/sla_print/` | ❌ 已停用 | `CMakeLists.txt` 中以 `#` 註解 |
| `tests/cpp17/` | ⚠️ 排除預設建置 | `EXCLUDE_FROM_ALL` |
| `tests/example/` | ❌ 已停用 | `CMakeLists.txt` 中以 `#` 註解 |

---

## 建置與執行

### 建置測試

```bash
# 建置所有啟用的測試套件
cd build && make

# 只建置特定套件
cd build && make libslic3r_tests
cd build && make fff_print_tests
cd build && make libnest2d_tests
cd build && make slic3rutils_tests
```

### 執行所有測試

```bash
cd build && ctest
cd build && ctest --output-on-failure   # 失敗時顯示詳細輸出
```

### 執行單一套件

```bash
# 建議加上 --order rand --warn NoAssertions
./tests/libslic3r/libslic3r_tests --order rand --warn NoAssertions
./tests/fff_print/fff_print_tests   --order rand --warn NoAssertions
./tests/libnest2d/libnest2d_tests   --order rand --warn NoAssertions
./tests/slic3rutils/slic3rutils_tests --order rand --warn NoAssertions
```

### 依 Tag 篩選

```bash
# 只跑 Geometry 相關
./tests/libslic3r/libslic3r_tests "[Geometry]"

# 只跑 Voronoi 相關
./tests/libslic3r/libslic3r_tests "[Voronoi]"

# 排除已知失敗
./tests/libnest2d/libnest2d_tests "~[NotWorking]"

# 列出所有可用測試名稱
./tests/libslic3r/libslic3r_tests --list-tests

# 列出所有 Tag
./tests/libslic3r/libslic3r_tests --list-tags
```

### CI 輸出格式

```bash
# JUnit XML（適合 CI 系統）
./tests/libslic3r/libslic3r_tests --reporter junit --out results.xml

# TAP 格式
./tests/libslic3r/libslic3r_tests --reporter tap

# TeamCity 格式
./tests/libslic3r/libslic3r_tests --reporter teamcity
```

---

## 撰寫測試的注意事項

### 1. SECTION 在迴圈中不可重複名稱

```cpp
// ❌ 錯誤：同名 SECTION
for (int i = 0; i < 3; ++i) {
    SECTION("same name") { ... }
}

// ✅ 正確：使用 DYNAMIC_SECTION
for (int i = 0; i < 3; ++i) {
    DYNAMIC_SECTION("item " << i) { ... }
}
```

### 2. Catch2 斷言不是 Thread-safe

不可在子執行緒中呼叫 `REQUIRE` / `CHECK`，請在主執行緒收集結果後再做斷言。

### 3. 浮點數比較

```cpp
// ❌ 已棄用
REQUIRE(value == Catch::Approx(expected));

// ✅ 使用 Matchers
REQUIRE_THAT(value, WithinAbs(expected, 0.001));
REQUIRE_THAT(value, WithinRel(expected, 0.01));
```

### 4. 存取測試資料

```cpp
std::string path = std::string(TEST_DATA_DIR) + "/test_stl/20mmbox.stl";
```

---

## 目錄結構速覽

```
tests/
├── CMakeLists.txt          ← 主設定，決定哪些套件啟用
├── catch_main.hpp          ← 自訂 VerboseConsoleReporter
├── catch2/catch.hpp        ← Catch2 v2 框架
├── data/                   ← 測試資料 (STL / 3MF / 設定檔)
├── libslic3r/              ← libslic3r_tests  ✅
├── fff_print/              ← fff_print_tests  ✅
├── libnest2d/              ← libnest2d_tests  ✅
├── slic3rutils/            ← slic3rutils_tests ✅
├── sla_print/              ← sla_print_tests  ❌ (停用)
├── cpp17/                  ← cpp17 tests      ⚠️ (EXCLUDE_FROM_ALL)
└── example/                ← example tests    ❌ (停用)
```
