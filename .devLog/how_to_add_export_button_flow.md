# Export Button 流程分析與新增做法

## 一、Export G-code（SLA → .sl1）完整執行流程

### 1-1 事件鏈

```
[使用者] 點擊 m_print_btn（label: "Export G-code file"）
    │  m_print_select == eExportGcode
    ▼
[MainFrame.cpp:1640-1641]
    wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_EXPORT_GCODE))
    │
    ▼  event 傳到 Plater
[Plater.cpp:3391]  Bind(EVT_GLTOOLBAR_EXPORT_GCODE, &priv::on_action_export_gcode)
    │
    ▼
[Plater.cpp:7720]  Plater::priv::on_action_export_gcode()
    → q->export_gcode(false)
    │
    ▼
[Plater.cpp:11991]  Plater::export_gcode(bool prefer_removable)
    ├─ 前置驗證：objects 非空 + process 未出錯
    ├─ update_restart_background_process()      // 同步後台切片狀態
    ├─ background_process.output_filepath_for_project("")
    │     → 取得預設檔名（副檔名由 Archive writer 決定，SLA = .sl1）
    ├─ printer_technology() 判斷 Dialog 標題 / filter：
    │     ptFFF → title: "Save G-code file as:"  filter: FT_GCODE (.gcode)
    │     ptSLA → title: "Save SLA file as:"     filter: FT_SL1  (.sl1, .sl1s)
    ├─ wxFileDialog.ShowModal()                  // 彈出儲存視窗
    └─ p->export_gcode(output_path, path_on_removable_media)
          → background_process 執行實際寫檔
          → notification_manager 發出 export 通知
          → appconfig.update_last_output_dir()   // 記住上次路徑
```

### 1-2 FileType / Wildcard 系統

**定義位置**：

| 檔案 | 內容 |
|------|------|
| `src/slic3r/GUI/GUI_App.hpp:91` | `enum FileType { FT_STEP, FT_STL, ... FT_SL1, FT_PRZ, FT_SIZE }` |
| `src/slic3r/GUI/GUI_App.cpp:514` | `file_wildcards_by_type[]` 陣列，index 必須對齊 enum 順序 |
| `src/slic3r/GUI/GUI_App.cpp:543` | `file_wildcards(FileType, custom_ext)` — 組裝 wxFileDialog wildcard 字串 |

**陣列對應（節錄）**：

```cpp
/* FT_GCODE */ { "G-code files"sv,       { ".gcode"sv } },
/* FT_SL1  */ { "Masked SLA files"sv,   { ".sl1"sv, ".sl1s"sv } },
/* FT_PRZ  */ { "Phrozen LCD files"sv,  { ".prz"sv } },
```

---

## 二、新增 Export .prz Button — 實作清單

### 2-1 所有需要修改的位置（6 個）

| # | 檔案 | 位置 | 說明 |
|---|------|------|------|
| 1 | `GUI_App.hpp` | `enum FileType` | 新增 `FT_PRZ`（在 `FT_SL1` 後、`FT_SIZE` 前） |
| 2 | `GUI_App.cpp` | `file_wildcards_by_type[]` | 對齊 enum 新增一項 `.prz` wildcard |
| 3 | `GLToolbar.hpp / .cpp` | event 宣告 / 定義 | `wxDECLARE/DEFINE_EVENT(EVT_GLTOOLBAR_EXPORT_PRZ, SimpleEvent)` |
| 4 | `MainFrame.hpp` | `enum PrintSelectType` | 新增 `eExportPrz = 10` |
| 5 | `MainFrame.cpp` | `m_print_btn Bind` 與 `m_print_option_btn Bind` | 加入 `eExportPrz` 分支與 SideButton |
| 6 | `Plater.cpp` | handler + `export_prz()` | 新增 `on_action_export_prz()` 與實際輸出邏輯 |

---

### 2-2 各位置詳細做法

#### ① `GUI_App.hpp` — 新增 enum 值

```cpp
// 在 FT_SL1 後、FT_SIZE 前插入
FT_SL1,
FT_PRZ,   // ← 新增
FT_SIZE,
```

#### ② `GUI_App.cpp` — 新增 wildcard 陣列項目

```cpp
// 陣列 index 必須嚴格對齊 enum 順序
/* FT_SL1 */ { "Masked SLA files"sv,  { ".sl1"sv, ".sl1s"sv } },
/* FT_PRZ */ { "Phrozen LCD files"sv, { ".prz"sv } },   // ← 新增
```

#### ③ `GLToolbar.hpp / GLToolbar.cpp` — 事件宣告 / 定義

```cpp
// GLToolbar.hpp
wxDECLARE_EVENT(EVT_GLTOOLBAR_EXPORT_PRZ, SimpleEvent);

// GLToolbar.cpp
wxDEFINE_EVENT(EVT_GLTOOLBAR_EXPORT_PRZ, SimpleEvent);
```

#### ④ `MainFrame.hpp` — PrintSelectType enum

```cpp
enum PrintSelectType {
    ePrintAll            = 0,
    ePrintPlate          = 1,
    eExportSlicedFile    = 2,
    eExportGcode         = 3,
    eSendGcode           = 4,
    eSendToPrinter       = 5,
    eSendToPrinterAll    = 6,
    eUploadGcode         = 7,
    eExportAllSlicedFile = 8,
    ePrintMultiMachine   = 9,
    eExportPrz           = 10,   // ← 新增
};
```

#### ⑤ `MainFrame.cpp` — 兩處修改

**A. m_print_btn Bind（主按鈕點擊執行動作）**

```cpp
// 在 eExportGcode 分支後新增
else if (m_print_select == eExportGcode)
    wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_EXPORT_GCODE));
else if (m_print_select == eExportPrz)               // ← 新增
    wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_EXPORT_PRZ));  // ← 新增
```

**B. m_print_option_btn Bind（下拉 popup 內的 SideButton）**

```cpp
// 限 ptSLA 時才顯示（放在 Phrozen vendor 的 SLA 區塊）
if (preset_bundle->printers.get_edited_preset().printer_technology() == ptSLA) {
    SideButton* export_prz_btn = new SideButton(p, _L("Export .prz file"), "");
    export_prz_btn->SetCornerRadius(0);
    export_prz_btn->Bind(wxEVT_BUTTON, [this, p](wxCommandEvent&) {
        m_print_btn->SetLabel(_L("Export .prz file"));
        m_print_select = eExportPrz;
        m_print_enable = get_enable_print_status();
        m_print_btn->Enable(m_print_enable);
        this->Layout();
        p->Dismiss();
    });
    p->append_button(export_prz_btn);
}
```

**C. get_enable_print_status()（讓按鈕 enable 判斷涵蓋 eExportPrz）**

```cpp
// 確認 eExportGcode 所在的 enable 條件也包含 eExportPrz
else if (m_print_select == eExportGcode || m_print_select == eExportPrz)
    // ... enable 邏輯
```

#### ⑥ `Plater.cpp` — handler + 輸出函式

**A. priv 宣告（與其他 handler 同區）**

```cpp
void on_action_export_prz(SimpleEvent&);
```

**B. Bind（與 EVT_GLTOOLBAR_EXPORT_GCODE 同區）**

```cpp
q->Bind(EVT_GLTOOLBAR_EXPORT_PRZ, &priv::on_action_export_prz, this);
```

**C. handler 實作**

```cpp
void Plater::priv::on_action_export_prz(SimpleEvent&)
{
    if (q != nullptr)
        q->export_prz(false);
}
```

**D. `export_prz()` 公開函式（Dialog 部分 — 已完成）**

```cpp
void Plater::export_prz(bool prefer_removable)
{
    if (p->model.objects.empty()) return;
    if (p->process_completed_with_error == p->partplate_list.get_curr_plate_index()) return;

    fs::path default_output_file;
    // ... update_restart_background_process + output_filepath_for_project ...

    fs::path output_path;
    {
        wxFileDialog dlg(this, _L("Save Phrozen SLA file as:"),
            start_dir,
            from_path(default_output_file.filename()),
            GUI::file_wildcards(FT_PRZ, ".prz"),   // ← .prz filter
            wxFD_SAVE | wxFD_OVERWRITE_PROMPT
        );
        if (dlg.ShowModal() == wxID_OK)
            output_path = into_path(dlg.GetPath());
        // ... illegal filename check ...
    }

    if (!output_path.empty()) {
        // TODO: 串接實際 .prz 寫檔邏輯（見下方「待完成項目」）
    }
}
```

---

## 三、目前實作狀態

| 項目 | 狀態 |
|------|------|
| `FT_PRZ` enum + wildcard | ✅ 已完成 |
| `EVT_GLTOOLBAR_EXPORT_PRZ` 宣告 / 定義 | ✅ 已完成 |
| `eExportPrz` enum 值 | ✅ 已完成 |
| `on_action_export_prz()` handler | ✅ 已完成 |
| `export_prz()` Dialog 部分 | ✅ 已完成 |
| SideButton in popup | ✅ 已完成 |
| `m_print_btn` Bind 分支 | ✅ 已完成 |
| **實際 .prz 寫檔邏輯** | ❌ **待完成（`#if 0` 中）** |

---

## 四、待完成：.prz 寫檔串接

`export_prz()` 的 `#if 0` 區塊需要確認如何觸發 .prz 格式輸出。

**參考路徑（.sl1 的做法）**：

```
p->export_gcode(output_path, path_on_removable_media)
    → BackgroundSlicingProcess::export_gcode()
    → SLAPrint → SLAArchiveWriter（依 archive_format 設定決定輸出格式）
```

**關鍵問題**：`.prz` 是獨立 archive format，還是 `.sl1` 的重命名？
- 若是重命名：直接呼叫 `p->export_gcode(output_path.replace_extension(".prz"), ...)`
- 若是新格式：需要在 `SLAArchiveWriter` 或對應子類中新增 `.prz` 輸出邏輯

**完成後需補上的程式碼（取代 `#if 0` 區塊）**：

```cpp
if (!output_path.empty()) {
    bool path_on_removable_media =
        removable_drive_manager.set_and_verify_last_save_path(output_path.string());
    p->notification_manager->new_export_began(path_on_removable_media);
    p->exporting_status = path_on_removable_media
        ? ExportingStatus::EXPORTING_TO_REMOVABLE
        : ExportingStatus::EXPORTING_TO_LOCAL;
    p->last_output_path      = output_path.string();
    p->last_output_dir_path  = output_path.parent_path().string();
    p->export_gcode(output_path, path_on_removable_media);  // 或 .prz 專用呼叫
    appconfig.update_last_output_dir(output_path.parent_path().string(),
                                      path_on_removable_media);
}
```

---

## 五、相關檔案索引

| 檔案 | 說明 |
|------|------|
| `src/slic3r/GUI/GUI_App.hpp:91` | `FileType` enum（FT_PRZ 位置） |
| `src/slic3r/GUI/GUI_App.cpp:514` | `file_wildcards_by_type[]` 陣列 |
| `src/slic3r/GUI/GLToolbar.hpp:37` / `.cpp:37` | `EVT_GLTOOLBAR_EXPORT_PRZ` |
| `src/slic3r/GUI/MainFrame.hpp:236` | `PrintSelectType` enum（eExportPrz） |
| `src/slic3r/GUI/MainFrame.cpp:1656` | `m_print_btn` Bind — eExportPrz 分支 |
| `src/slic3r/GUI/MainFrame.cpp:1906` | SideButton popup — Export .prz button |
| `src/slic3r/GUI/Plater.cpp:7730` | `on_action_export_prz()` handler |
| `src/slic3r/GUI/Plater.cpp:12101` | `export_prz()` 實作（Dialog 完成，寫檔待串接） |
| `src/slic3r/GUI/Plater.cpp:11991` | `export_gcode()` 參考實作 |
