# Design：SLA 孤島偵測視覺化與局部支撐生成

## UI 設計

### 入口：GLGizmoLcdOverhangDetection 面板

面板佈局（由上至下）：

```
┌─────────────────────────────────────┐
│  偵測精度:  ○ Low  ● Middle  ○ High │
├─────────────────────────────────────┤
│  模型:  ◀  [ModelObject Name]  ▶   │
│  孤島數量:  12 個  （未偵測：--）   │
├─────────────────────────────────────┤
│  [Detect All]  [Detect Selected]    │
├─────────────────────────────────────┤
│  [Add Overhang Support]             │
└─────────────────────────────────────┘
```

### 控制項行為

#### 偵測精度（Detection Accuracy）
- 選項：`Low` / `Middle` / `High`
- 對應 `PrepareSupportConfig::discretize_overhang_step`：
  - Low → 4.0mm（快速，粗略）
  - Middle → 2.0mm（預設）
  - High → 0.8mm（精確，較慢）
- 變更後不自動重新偵測，需使用者手動觸發

#### 模型選擇器（Model Selector）
- 預設顯示進入 gizmo 時選中的 `ModelObject`
- `◀` / `▶` 切換場景中的其他 `ModelObject`
- 切換後顯示該模型的偵測結果（若有），否則顯示空狀態（`--`）

#### 孤島數量（Overhang Indicator）
- 來源：`IslandDetectionService` 完成後回傳的 `IslandResult.total_island_count`
- 尚未偵測：顯示 `--`
- 偵測完成：顯示整數（各層孤島數量總和）

#### Detect All
- 對場景中所有 `ModelObject` 依序執行 `IslandDetectionService::detect()`
- 偵測在背景執行（`std::async` 或 worker thread），完成後更新 UI 與渲染
- 執行中顯示進度提示

#### Detect Selected
- 僅對目前選中的 `ModelObject` 執行 `IslandDetectionService::detect()`
- 完成後立即更新 island 輪廓渲染

#### Add Overhang Support
- 若目前模型已有偵測結果：
  1. 呼叫 `IslandSupportGenerator::generate()`，僅對 island 區域生成支撐點
  2. 呼叫 `TreeSupportBuilder::build()`，建構支撐樹 mesh
  3. 渲染支撐 mesh（疊加在 island 輪廓上）
- 若目前模型**尚未偵測**：
  1. 自動先執行 `IslandDetectionService::detect()`
  2. 完成後接續執行步驟 1–3

---

## 系統架構

### 模組職責

```
GLGizmoLcdOverhangDetection
    │
    ├── IslandDetectionService          （libslic3r/SLA/）
    │     職責：從 ModelObject 切片資料建構跨層連通性，識別 island
    │     輸入：ModelObject*, PrepareSupportConfig（精度設定）
    │     輸出：IslandResult（每層 ExPolygon 輪廓 + island 數量）
    │     複用：prepare_generator_data() → SupportPointGeneratorData
    │
    ├── IslandVisualizationRenderer     （GLGizmoLcdOverhangDetection 內部）
    │     職責：將 IslandResult 轉換為 GLModel mesh，管理渲染生命週期
    │     輸入：IslandResult
    │     輸出：GLModel（填滿三角網格，Z+0.05mm 偏移）
    │     複用：triangulate_expolygon_3d()、GLModel::init_from()
    │
    ├── IslandSupportGenerator          （libslic3r/SLA/）
    │     職責：對 island ExPolygon 生成支撐點（不調用完整自動支撐管線）
    │     輸入：IslandResult, SupportPointGeneratorConfig（密度）
    │     輸出：sla::SupportPoints
    │     複用：uniform_support_island()、move_on_mesh_surface()
    │
    └── TreeSupportBuilder              （GLGizmoLcdOverhangDetection 內部）
          職責：對支撐點建構支撐樹 mesh，管理渲染生命週期
          輸入：sla::SupportPoints, SLASupportTreeConfig
          輸出：TriangleMesh（支撐結構 3D mesh）
          複用：SupportTreeBuildsteps（直接實例化）
```

### 關鍵資料結構

```cpp
// IslandDetectionService 輸出
struct IslandResult {
    // 每層的 island ExPolygon，key = print_z（mm）
    std::vector<std::pair<float, ExPolygons>> layers;
    int total_island_count = 0;
};

// Gizmo 內部狀態（per-ModelObject）
struct ModelIslandState {
    std::optional<IslandResult>  detection_result;
    GLModel                      contour_mesh;       // 視覺化網格
    sla::SupportPoints           support_points;     // 局部支撐點
    TriangleMesh                 support_mesh;       // 支撐樹 mesh
    GLModel                      support_gl_model;   // support mesh 的 GL 表示
};

// Gizmo 成員
std::unordered_map<ObjectID, ModelIslandState> m_island_states;
int                                             m_selected_object_idx = 0;
DetectionAccuracy                               m_accuracy = DetectionAccuracy::Middle;
```

---

## 資料流

### 偵測流程

```
使用者點擊 "Detect Selected"
    │
    ▼
GLGizmoLcdOverhangDetection::run_detection(ModelObject*)
    │
    ▼
IslandDetectionService::detect(ModelObject*, PrepareSupportConfig)
    │
    ├─ 取得 ModelObject 的切片資料（slaposObjectSlice 必須已完成）
    ├─ 呼叫 prepare_generator_data()  ← 複用現有函式
    │       → 建立 LayerParts + prev_parts 跨層連結
    │
    ├─ 迭代所有層：收集 prev_parts.empty() 的 part.shape
    │       → 填入 IslandResult.layers
    │
    └─ 回傳 IslandResult
    │
    ▼
IslandVisualizationRenderer::build_mesh(IslandResult)
    │
    ├─ 對每層每個 ExPolygon 呼叫 triangulate_expolygon_3d(shape, z + 0.05f)
    └─ 合併為單一 indexed_triangle_set → GLModel::init_from()
    │
    ▼
GLGizmoLcdOverhangDetection::on_render() → 渲染 contour_mesh
```

### 支撐生成流程

```
使用者點擊 "Add Overhang Support"
    │
    ├─ 若無偵測結果 → 先執行偵測流程（同上）
    │
    ▼
IslandSupportGenerator::generate(IslandResult, ModelObject*, config)
    │
    ├─ 對每層每個 island ExPolygon：
    │       呼叫 uniform_support_island(*shape, {}, island_config)
    │       → 收集 SupportIslandPoints
    │
    ├─ 呼叫 move_on_mesh_surface(layer_pts, emesh, allowed_move)
    │       → 將點投影到實際 mesh 表面
    │
    └─ 輸出 sla::SupportPoints（type = SupportPointType::island）
    │
    ▼
TreeSupportBuilder::build(SupportPoints, ModelObject*, SLASupportTreeConfig)
    │
    ├─ 建立 SupportData（emesh）
    ├─ 實例化 SupportTreeBuildsteps  ← 直接複用
    ├─ 執行 make_pillar_only_heads() + create_ground_pillar()
    └─ 輸出 TriangleMesh（支撐結構）→ GLModel
    │
    ▼
GLGizmoLcdOverhangDetection::on_render() → 渲染 support_gl_model
```

---

## 舊版偵測實作保留策略

commit `9a07fe61b9` 的實作：
- **保留原始程式碼**，不刪除
- 以 `#ifdef LEGACY_ISLAND_DETECTION` 或獨立函式名稱區隔
- 新流程（`IslandDetectionService`）與舊流程可在面板中透過隱藏的 debug 開關切換
- 目的：對照新舊偵測結果的差異，確認新流程的覆蓋率與正確性

---

## 邊界條件與錯誤處理

| 情境 | 處理方式 |
|------|---------|
| `slaposObjectSlice` 尚未完成 | 停用 Detect 按鈕，顯示提示「請先切片」 |
| 模型沒有 island | `total_island_count = 0`，顯示「無孤島」，Add Overhang Support 停用 |
| 偵測執行中再次觸發 | 忽略新請求或取消舊任務（視實作難度決定） |
| 支撐點為空（全被過濾） | 不呼叫 TreeSupportBuilder，顯示警告 |
| FDM 機種 | 整個 GLGizmoLcdOverhangDetection 不啟動 |

---

## 渲染規格

| 物件 | 顏色 | Alpha | Z 偏移 |
|------|------|-------|--------|
| Island 輪廓網格 | 亮黃 `(1.0, 0.95, 0.0)` | 0.45 | +0.05mm |
| 支撐樹 mesh | 半透明藍灰 `(0.5, 0.7, 1.0)` | 0.6 | 0（實際幾何） |

- Island 輪廓：`glDisable(GL_DEPTH_TEST)` 確保永遠可見
- 支撐樹 mesh：正常深度測試，與模型互動
