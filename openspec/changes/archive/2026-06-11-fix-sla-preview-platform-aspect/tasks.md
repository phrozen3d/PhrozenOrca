## 1. 前置確認（不改 code）

- [x] 1.1 重讀 `src/slic3r/GUI/SLASlice2DCanvas.cpp` 的 `render_texture_letterbox()`（約 593-668）與 `platform_aspect_w_over_h()`（約 106-128），確認 `aspect_img` 目前計算式與函式開頭早退守衛位置
- [x] ✅ 驗證 1.1：grep 確認 `aspect_img` 僅 line 600 單點賦值、無第二推導處；`platform_aspect_w_over_h` 已被 portal 路徑(434/472/523/785)共用；`<cmath>` 已 include（std::isfinite 可用）

## 2. 防禦性 fallback 與守衛（先建立安全底線）

- [x] 2.1 在 `render_texture_letterbox()` 內、計算 `aspect_img` 之前，新增區域變數 `aspect_img_fallback` 保留「現行材質像素比公式」：`portrait_lcd ? double(m_tex_h)/double(m_tex_w) : double(m_tex_w)/double(m_tex_h)`（保留既有 `portrait_lcd` 分支語意）
- [x] ✅ 驗證 2.1（靜態）：無 build 樹可隔離編譯（build/ 僅 CMakeCache、未生成 VS 專案），改以靜態驗證——新變數皆被引用無 unused 警告、型別相容；兩 ternary 分支皆回 fallback，行為與改動前完全相同
- [x] 2.2 加入退避守衛：`platform_ar = (m_print!=nullptr) ? platform_aspect_w_over_h(m_print) : 0.0;`、`use_platform_ar = std::isfinite(platform_ar) && platform_ar > 0.0;`，涵蓋 null／非有限／≤0 三種退化（Stage 3 才把正常路徑 true-branch 接到 `platform_ar`）
- [x] ✅ 驗證 2.2（靜態）：退避涵蓋三種異常；函式開頭早退（`m_tex_id==0 || m_tex_w<=0 || m_tex_h<=0 || portal_w<=0 || portal_h<=0`，line 595）未動，無除零；`git diff --name-only` 僅 `SLASlice2DCanvas.cpp`

## 3. 比例公式替換（接線正常路徑）

- [x] 3.1 將 `aspect_img` 三元 true 分支由 `aspect_img_fallback` 改為 `platform_ar`（= `platform_aspect_w_over_h(m_print)`）：`use_platform_ar ? platform_ar : aspect_img_fallback`（line 613）
- [x] ✅ 驗證 3.1（靜態）：正常路徑現取 `platform_ar`；Revo 16K → `platform_ar = 211.68/118.37 = 1.788`（非 2.427）；方形像素機 → `platform_ar == 材質比`，no-op。註：真正局部編譯待第 4 階段完整 build 驗證
- [x] 3.2 正常路徑已收斂為單一 `platform_ar`，不再依 portrait/landscape 分支；`portrait_lcd` 分支僅保留於 fallback（line 609-610）與下方方位 UV 區塊（其原始用途）
- [x] ✅ 驗證 3.2：`git diff` 確認方位 UV 區塊（`flip_v`/`flip_h`/`mirror_x`/`mirror_y`/`ROTATE`/`render_sub_texture`）零 +/- 變動，未被觸碰；diff 僅落在 `aspect_img` 相關行

## 4. 編譯與零回歸驗證

- [x] 4.1 完整編譯 GUI 模組（slicer 目標），確認無新增警告/錯誤
- [x] ✅ 驗證 4.1：外部 VS 2022 IDE 建置成功、無錯誤；diff 範圍僅限 `SLASlice2DCanvas.cpp` 單一函式
- [x] 4.2 方形像素機種回歸測試：載入 Phrozen Sonic Mega 8K S/V2，切片後檢視逐層 raster 預覽
- [x] ✅ 驗證 4.2：使用者確認 Mega 8K S/V2 預覽圖正確（no-op）、PRZ 輸出正確
- [x] 4.3 非方形像素機種修正驗證：載入 Phrozen Sonic Mighty Revo 16K，切片後檢視逐層 raster 預覽
- [x] ✅ 驗證 4.3：使用者確認 Revo 16K 預覽圖正確（不再變扁）
- [x] 4.4 兩路徑一致性驗證：於 Revo 16K 比對「向量預覽」與「raster 縮圖預覽」同一層的顯示比例
- [x] ✅ 驗證 4.4：使用者確認比例一致

## 5. Fallback 與邊界驗證

- [x] 5.1 觸發 fallback 路徑驗證：在無有效 `m_print`/raster 之狀態（如尚未切片即開啟預覽）下開啟 2D 預覽
- [x] ✅ 驗證 5.1（靜態路徑追蹤）：入口守衛(595)保證 tex 維度>0；`m_print==null` → `platform_ar=0` → `use_platform_ar=false` → `aspect_img=aspect_img_fallback`(>0)；`m_print!=null` 時 helper 退化亦回 1.0(finite>0)。`aspect_img` 全路徑恆有限正值，下游除法(`portal_w/aspect_img`)無除零、不黑屏、不丟例外
- [x] 5.2 鐵律回歸確認：確認本次改動未觸碰 PRZ 匯出（`PhrozenPRZ*`）、rasterizer（`RasterToCvMat`）、raster 快照（`SLAPrintSteps::rasterize`）、快取（`RasterCache`）與底層切片格式
- [x] ✅ 驗證 5.2：`git diff --name-only -- src/` 僅 `src/slic3r/GUI/SLASlice2DCanvas.cpp`（14+/2−）；grep PRZ/RasterToCvMat/RasterCache/SLAPrintSteps/Format → 零命中

## 6. 收尾

- [x] 6.1 `openspec validate fix-sla-preview-platform-aspect` 通過
- [x] ✅ 驗證 6.1：validate 無錯誤；spec 三場景皆有對應已驗證任務 — 非方形修正(4.3)／方形 no-op(4.2)／fallback(5.1)