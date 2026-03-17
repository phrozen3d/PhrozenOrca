# Print Button 流程分析

## 背景

`MainFrame` 右上角的輸出按鈕群組由兩個元件組成：

- `m_print_btn`：主按鈕，顯示當前選擇的動作 label
- `m_print_option_btn`（`▾`）：展開 `SidePopup`，讓使用者選擇動作

使用者從 popup 選擇動作後，`m_print_select` 與 `m_print_btn` 的 label 會同步更新。之後點擊主按鈕即執行對應動作。

---

## Vendor 類型判讀

`get_current_vendor_type()` 的判讀邏輯：

```
Selected printer preset
    │
    ▼
config->opt_string("printer_model")      // e.g. "Mega 8K S", "Phrozen Arco"
    │
    ▼
vendors map  (key = JSON 檔案 stem)
    │  迭代找出哪個 vendor 的 models 包含此 printer_model
    ▼
vendor_name string
    ├─ "BBL"      → VendorType::Marlin_BBL
    ├─ "Phrozen"  → VendorType::Phrozen
    └─ 其他       → VendorType::Unknown
```

### Resource 對應表

| JSON 檔案 | vendor_name 鍵 | printer_model 例 | 判讀結果 |
|-----------|---------------|-----------------|---------|
| `Phrozen.json` | `"Phrozen"` | Phrozen Arco | `VendorType::Phrozen` |
| `PhrozenSLA.json` | `"PhrozenSLA"` | Mega 8K S | `VendorType::Unknown` ⚠️ |
| `PrusaResearchSLA.json` | `"PrusaResearchSLA"` | Original Prusa SL1 | `VendorType::Unknown` ⚠️ |

> ⚠️ `PhrozenSLA` 不匹配 `"Phrozen"`，導致 `is_phrozen_vendor()` 回傳 false，SLA 機台走進 ThirdParty 路徑，popup 只顯示 "Export G-code file"。

---

## `print_plate_btn` 執行流程

**動作**：`m_print_select = ePrintPlate`

```
popup 點擊 "Print plate"
    → m_print_select = ePrintPlate
    → m_print_btn->SetLabel("Print plate")

主按鈕點擊
    → apply_background_progress()       // 確認後台切片狀態
    → get_enable_print_status()         // 驗證可否列印
    → wxPostEvent(EVT_GLTOOLBAR_PRINT_PLATE)
    → on_action_print_plate()
        ├─ use_bbl_network()   → SelectMachineDialog        // BBL 雲端機台選擇
        ├─ is_phrozen_vendor() → PhrozenSelectMachineDialog // PhrozenConnect 機台選擇
        └─ else                → send_gcode_legacy()        // 第三方 print host 直接上傳
```

---

## `send_to_printer_btn` 執行流程

**動作**：`m_print_select = eSendToPrinter`

```
popup 點擊 "Send"
    → m_print_select = eSendToPrinter
    → m_print_btn->SetLabel("Send")

主按鈕點擊
    → wxPostEvent(EVT_GLTOOLBAR_SEND_TO_PRINTER)   // 無前置驗證
    → on_action_export_to_sdcard()
    → send_to_printer(false)
    → on_action_send_to_printer(false)
    → SendToPrinterDialog::prepare(curr_plate_index)
    → ShowModal()                                  // 選擇本地儲存路徑
```

---

## 核心差異對照

| | `print_plate_btn` | `send_to_printer_btn` |
|---|---|---|
| **傳輸方式** | 網路（雲端 / PhrozenConnect） | 本地（SD card / USB） |
| **Dialog** | `SelectMachineDialog` / `PhrozenSelectMachineDialog` | `SendToPrinterDialog` |
| **前置驗證** | `apply_background_progress()` + `get_enable_print_status()` | 無 |
| **場景** | 印表機連網，遠端送出列印工作 | 印表機離線，手動插卡列印 |
| **事件** | `EVT_GLTOOLBAR_PRINT_PLATE` | `EVT_GLTOOLBAR_SEND_TO_PRINTER` |
| **Handler** | `on_action_print_plate()` | `on_action_export_to_sdcard()` |

---

## 為什麼需要兩種

- **`print_plate`（線上列印）**：印表機已透過 BBL Cloud 或 PhrozenConnect 配對。切片完後直接從軟體送出 job，印表機遠端接收並開始列印。需驗證切片狀態才能送出。

- **`send_to_printer`（匯出到儲存裝置）**：印表機不連網。將切片檔存到 SD card 或 USB，使用者手動插到印表機。只需選擇儲存路徑，不需驗證列印 enable 狀態。

PhrozenSLA MSLA 光固化機台幾乎都以 SD card 離線列印為主，因此 `send_to_printer_btn` 是 SLA 使用者的主要工作流程。

---

## 所有 `m_print_select` 動作一覽

| enum 值 | 事件 | Handler | 說明 |
|---------|------|---------|------|
| `ePrintPlate` | `EVT_GLTOOLBAR_PRINT_PLATE` | `on_action_print_plate()` | 網路列印（單板） |
| `ePrintAll` | `EVT_GLTOOLBAR_PRINT_ALL` | `on_action_print_all()` | 網路列印（全部板） |
| `ePrintMultiMachine` | `EVT_GLTOOLBAR_PRINT_MULTI_MACHINE` | `on_action_send_to_multi_machine()` | 多機台列印 |
| `eExportGcode` | `EVT_GLTOOLBAR_EXPORT_GCODE` | `on_action_export_gcode()` → `export_gcode()` | 匯出 G-code 檔 |
| `eSendGcode` | `EVT_GLTOOLBAR_SEND_GCODE` | `on_action_send_gcode()` → `send_gcode_legacy()` | 上傳 G-code 至 host |
| `eExportSlicedFile` | `EVT_GLTOOLBAR_EXPORT_SLICED_FILE` | `on_action_export_sliced_file()` → `export_gcode_3mf()` | 匯出切片 3mf（單板） |
| `eExportAllSlicedFile` | `EVT_GLTOOLBAR_EXPORT_ALL_SLICED_FILE` | `on_action_export_all_sliced_file()` → `export_gcode_3mf(true)` | 匯出切片 3mf（全部） |
| `eSendToPrinter` | `EVT_GLTOOLBAR_SEND_TO_PRINTER` | `on_action_export_to_sdcard()` → `SendToPrinterDialog` | 匯出至 SD card（單板） |
| `eSendToPrinterAll` | `EVT_GLTOOLBAR_SEND_TO_PRINTER_ALL` | `on_action_export_to_sdcard_all()` → `SendToPrinterDialog` | 匯出至 SD card（全部） |

---

## 相關檔案

| 檔案 | 說明 |
|------|------|
| `src/slic3r/GUI/MainFrame.cpp` | `create_side_tools()`、`m_print_btn` Bind、`update_side_preset_ui()` |
| `src/slic3r/GUI/Plater.cpp` | 各 `on_action_*` handler 實作 |
| `src/libslic3r/PresetBundle.cpp` | `get_current_vendor_type()`、`load_vendor_configs_from_json()` |
| `resources/profiles/*.json` | Vendor 名稱來源（檔案 stem = vendor_name 鍵） |
| `src/slic3r/GUI/Widgets/SideMenuPopup.cpp` | `SidePopup` 實作 |
