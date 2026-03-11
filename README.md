# PciToolPkg
---

# UEFI PCI Tool (PciTool) 開發筆記與使用手冊

這是一個基於 UEFI Shell 的互動式 PCI 裝置檢測工具。它模仿了 Linux `lspci` 的列表風格，並結合了互動式的 Hex Editor 功能，允許使用者在 UEFI 環境下查看並修改 PCI Configuration Space。

## 📋 功能特色 (Features)

1. **裝置掃描 (Device Enumeration)**:
* 自動遍歷 Bus 0-255, Device 0-31, Function 0-7。
* 自動識別多功能裝置 (Multi-function Device)。
* 過濾無效裝置 (Vendor ID = 0xFFFF)。


2. **名稱解析 (Name Resolution)**:
* **廠商識別**: 內建常見廠商資料庫 (Intel, AMD, NVIDIA 等)，將 Vendor ID 轉為名稱。
* **分類識別**: 解析 Base Class 與 Sub Class，顯示詳細裝置類型 (如 `Storage (NVMe)`, `Display (VGA)`)。


3. **互動式介面 (Interactive UI)**:
* **列表模式**: 類似 `lspci` 的清單，支援上下鍵選擇。
* **檢視模式**: 16進制 (Hex Dump) 查看 Config Space。
* **編輯模式**: 游標式移動 (Cursor) 選擇 Offset，直接輸入數值寫入。


4. **顯示控制**:
* 支援 Byte (8-bit), Word (16-bit), Dword (32-bit) 三種寬度切換。



---

## 🛠️ 開發環境與編譯 (Build)

### 1. 檔案結構

將程式碼放置於 EDK2 的 `PciToolPkg` 或 `AppPkg` 中：

```text
edk2/
  └── PciToolPkg/
       ├── PciToolPkg.dsc      (描述檔，定義 Library 依賴)
       └── Applications/
            └── PciTool/
                 ├── PciTool.c   (核心原始碼)
                 └── PciTool.inf (模組定義檔)

```

### 2. 關鍵依賴 (Library Classes)

在 `.dsc` 中必須包含以下關鍵 Library：

* `PciRootBridgeIoProtocol`: 用於底層 PCI 存取。
* `ShellLib`: 用於 `ShellHexStrToUintn` (字串轉數值)。
* `PrintLib`, `UefiLib`: 用於螢幕輸出。

### 3. 編譯指令

```bash
build -p PciToolPkg\PciToolPkg.dsc -a X64 -t VS2019 -b DEBUG

```

---

## 🎮 使用指南 (User Guide)

程式採用 **狀態機 (State Machine)** 設計，操作流程如下：

### 1. 裝置列表畫面 (Device List)

* **[↑] / [↓]**: 移動選擇光標。
* **[Enter]**: 進入選定裝置的詳細檢視 (Dump)。
* **[Q]**: 退出程式。

### 2. 詳細檢視畫面 (View Dump)

* **[Space]**: 切換顯示寬度 (Byte / Word / Dword)。
* **[W]**: 進入 **寫入模式 (Write Mode)**。
* **[Esc]**: 返回裝置列表。

### 3. 寫入模式 (Write / Edit)

* **[↑/↓/←/→]**: 移動 **青色游標** 選擇要修改的 Offset。
* **[Enter]**: 確認位置，開始輸入數值。
* **[輸入 Hex]**: 輸入 0-9, A-F (例如 `1234`)。
* **[Enter]**: 執行寫入 (Write) 並自動刷新畫面。

---

## 📚 函數筆記與實作細節 (Function Notes)

這部分是核心程式碼的學習筆記，記錄了每個關鍵函數的用途與實作邏輯。

### 1. 核心邏輯：`ScanDevices`

* **用途**: 掃描全機 PCI 裝置並建立快取清單 (`gDeviceList`)。
* **關鍵技術**:
* **三層迴圈**: Bus (0-255) -> Dev (0-31) -> Func (0-7)。
* **存在檢查**: 讀取 VID/DID，若為 `0xFFFFFFFF` 則跳過。
* **多功能判斷**: 讀取 Header Type (Offset 0x0E)，若 Bit 7 為 1，則繼續掃描 Func 1-7。
* **Class Code 讀取**: 讀取 Offset 0x08，並透過位元位移 (Shift) 取出 Base Class 與 Sub Class。

### 2. 名稱解析：`VendorName` & `GetFullClassName`

* **用途**: 將冰冷的 ID 轉換為人類可讀的字串。
* **實作方式**:
* **查表法 (Lookup Table)**: 建立 `gVendorTable` 結構陣列，遍歷比對 ID。
* **Switch-Case**: 針對 Class Code 使用兩層 `switch` (先判斷 Base，再判斷 Sub)，以回傳最精確的字串 (如 `Storage (NVMe)` vs `Storage (SATA)`).

### 3. 畫面繪製：`DrawDeviceList`

* **用途**: 繪製類似 `lspci` 的列表。
* **UI 技巧**:
* **分頁捲動 (Scrolling)**: 利用 `StartRow` 與 `MaxRows` 控制顯示範圍，當 `gListIndex` 超出範圍時自動調整 `StartRow`。
* **高亮 (Highlight)**: 透過 `gST->ConOut->SetAttribute` 切換背景顏色 (綠底黑字) 來標示當前選中的項目。

### 4. 畫面繪製：`DrawDumpView`

* **用途**: 顯示 Hex Dump 網格。
* **UI 技巧**:
* **動態寬度**: 根據 `gWidth` 決定 `Pci.Read` 的參數與 `Print` 的格式 (`%02X`, `%04X`, `%08X`)。
* **游標繪製**: 在迴圈中判斷 `CurrentAddr == gTargetOffset`，若成立則改變背景色 (青色)，實現「游標」效果。

### 5. 寫入執行：`ExecuteWrite`

* **用途**: 將使用者輸入寫入硬體。
* **流程**:
1. `ShellHexStrToUintn`: 將輸入緩衝區的字串轉為整數。
2. `PCI_LIB_ADDRESS`: 結合 Bus/Dev/Func 與選定的 Offset 計算目標位址。
3. `gPciIo->Pci.Write`: 呼叫 UEFI Protocol 執行實際寫入。
4. 寫入後自動切換回 `STATE_VIEW_DUMP`，因為畫面重繪時會重新讀取硬體，使用者能立刻看到結果。

### 6. 主程式：`ShellAppMain` (狀態機)

* **設計模式**: **State Machine (狀態機)**。
* **邏輯**:
* 使用 `while(TRUE)` 進行主迴圈。
* 使用 `gBS->WaitForEvent` 等待按鍵 (避免吃滿 CPU)。
* 使用 `switch(gState)` 區分不同模式下的按鍵反應 (例如：在列表模式下 `Enter` 是進入，在寫入模式下 `Enter` 是確認)。

---

## 📝 學習重點摘要

1. **Protocol 使用**: 熟練 `EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL` 的 `Pci.Read` 和 `Pci.Write`。
2. **PCI 位址結構**: 理解 Bus, Device, Function, Offset 如何組合成 UEFI 的 64-bit Address。
3. **Config Space 結構**: 熟悉 Offset 0x00 (ID), 0x08 (Class/Rev), 0x0E (Header Type) 的定義。
4. **UEFI UI 開發**: 學習如何使用 `SetAttribute` 做顏色高亮，以及如何處理鍵盤事件 (`ReadKeyStroke`)。

---

### 一、 系統架構圖 (System Architecture)

展示程式如何透過 UEFI 提供的介面與底層硬體進行溝通。

```text
=======================================================================
                        [ PciTool 系統架構圖 ]
=======================================================================

    +-------------------------------------------------------------+
    |                  PciTool.efi (UEFI Application)             |
    |                                                             |
    |  +----------------+  +----------------+  +---------------+  |
    |  |  UI 渲染引擎   |  |  狀態機控制器  |  |  PCI 資料庫   |  |
    |  | (Draw/Display) |  | (State Machine)|  | (Lookup DB)   |  |
    |  +-------+--------+  +--------+-------+  +-------+-------+  |
    |          |                    |                  |          |
    |  +-------v--------+  +--------v-------+          |          |
    |  | Console In/Out |  | PciRootBridgeIo| <--------+          |
    |  | (鍵盤/螢幕)    |  | (PCI 讀寫核心) |                     |
    |  +-------+--------+  +--------+-------+                     |
    +----------|--------------------|-----------------------------+
               | (gST->ConIn/Out)   | (gPciIo->Pci.Read/Write)
               |                    |
    +----------v--------------------v-----------------------------+
    |                    UEFI Firmware (韌體層)                   |
    |    [ Boot Services ]                   [ DXE Core ]         |
    +---------------------------------------|---------------------+
                                            | (Hardware Access)
    +---------------------------------------v---------------------+
    |                        硬體層 (Hardware)                    |
    |  [ PCI/PCIe Bus ] -> [ Device Configuration Space (暫存器)] |
    +-------------------------------------------------------------+

```

---

### 二、 主選單與狀態機流程圖 (State Machine Flow)

展示程式的四個核心狀態 (`TOOL_STATE`) 以及使用者按鍵如何觸發狀態轉換。

```text
=======================================================================
                      [ PciTool 狀態機流程圖 ]
=======================================================================

       (啟動 PciTool.efi)
              │
              ▼
   +----------------------+
   |  執行 ScanDevices()  | ---> 讀取全機 PCI 裝置存入 gDeviceList
   +----------+-----------+
              │
              ▼
   ╔══════════════════════╗
   ║  STATE_DEVICE_LIST   ║ <─────────────────────────────────┐
   ║  (裝置列表模式)      ║                                   │
   ╚══════════════════════╝                                   │
      │   ├─ [↑] [↓] : 移動光標選擇裝置                       │
      │   ├─ [Q]     : 離開程式                               │
      │                                                       │
      └─ [Enter] 選擇裝置                                     │ [Esc] / [Q] 
              │                                               │ 返回列表
              ▼                                               │
   ╔══════════════════════╗                                   │
   ║   STATE_VIEW_DUMP    ║ ──────────────────────────────────┤
   ║  (檢視 Hex Dump)     ║                                   │
   ╚══════════════════════╝                                   │
      │   ├─ [Space] : 切換寬度 (Byte/Word/Dword)             │
      │                                                       │
      └─ [W] 鍵: 進入寫入準備                                 │ [Esc]
              │                                               │ 取消
              ▼                                               │
   ╔══════════════════════╗                                   │
   ║ STATE_SELECT_OFFSET  ║ ──────────────────────────────────┤
   ║ (游標移動選擇位址)   ║                                   │
   ╚══════════════════════╝                                   │
      │   ├─ [↑][↓][←][→] : 移動青色游標選定 Offset         │
      │                                                       │
      └─ [Enter] 確認目標位址                                 │ [Esc]
              │                                               │ 取消
              ▼                                               │
   ╔══════════════════════╗                                   │
   ║  STATE_INPUT_VALUE   ║ ──────────────────────────────────┘
   ║  (輸入寫入數值)      ║ 
   ╚══════════════════════╝ 
          ├─ [0-9, A-F] : 輸入 Hex 數值
          ├─ [Backspace]: 刪除輸入
          │
          └─ [Enter] 執行 ExecuteWrite() 
                 │
                 └─> (寫入底層硬體，並自動跳回 STATE_VIEW_DUMP 刷新畫面)

```

---

### 三、 程式碼模組樹狀圖 (Module Tree Diagram)

展示 `PciTool.c` 原始碼內部的函數分類與資料結構關聯，方便快速尋找對應的 Code。

```text
=======================================================================
                       [ PciTool 模組樹狀圖 ]
=======================================================================

PciTool.c
 ├── 1. 定義與結構 (Definitions & Structures)
 │   ├── PCI_LIB_ADDRESS        (巨集：計算 PCI 實際位址)
 │   ├── PCI_DEVICE_ENTRY       (結構體：儲存掃描到的單一裝置快取)
 │   ├── TOOL_STATE             (列舉：定義四大畫面狀態)
 │   └── DATA_WIDTH             (列舉：定義 8/16/32-bit 顯示與寫入寬度)
 │
 ├── 2. 靜態資料庫 (Static Lookup Tables)
 │   ├── gVendorTable           (陣列：存放 Vendor ID 與廠商名稱對應)
 │   └── gBaseClassNames        (陣列：存放 Base Class 基礎分類名稱)
 │
 ├── 3. 全域變數 (Global Variables)
 │   ├── *gPciIo                (指標：存取硬體的核心 Protocol)
 │   ├── gDeviceList[]          (陣列：全機 PCI 裝置清單)
 │   └── gState, gTargetOffset, gWidth... (狀態機與 UI 狀態變數)
 │
 ├── 4. 輔助解析模組 (Helper Modules)
 │   ├── VendorName()           (函數：輸入 VID -> 輸出廠商名稱字串)
 │   └── GetFullClassName()     (函數：輸入 Base/Sub Class -> 輸出詳細分類字串)
 │
 ├── 5. 核心硬體操作 (Hardware Operations)
 │   ├── ScanDevices()          (函數：三層迴圈遍歷 Bus/Dev/Func，建立快取清單)
 │   └── ExecuteWrite()         (函數：將字串轉 Hex 並呼叫 gPciIo->Pci.Write)
 │
 ├── 6. 畫面渲染引擎 (UI Rendering)
 │   ├── DrawDeviceList()       (函數：繪製狀態0的 lspci 風格表格與分頁)
 │   └── DrawDumpView()         (函數：繪製狀態1,2,3的 Hex 網格與游標邏輯)
 │
 └── 7. 主程式控制 (Main Entry Point)
     └── ShellAppMain()
         ├── LocateProtocol()   (取得 RootBridgeIo Protocol)
         ├── ScanDevices()      (觸發初始掃描)
         └── while(TRUE)        (主迴圈)
             ├── gBS->WaitForEvent (等待鍵盤事件，不佔用 CPU)
             ├── ReadKeyStroke     (讀取使用者按鍵)
             └── switch (gState)   (狀態機事件分發路由)

```
---

cd /d D:\BIOS\MyWorkSpace\edk2

edksetup.bat Rebuild

chcp 65001

set PYTHONUTF8=1

set PYTHONIOENCODING=utf-8

rmdir /s /q Build\PciToolPkg

build -p PciToolPkg\PciToolPkg.dsc -a X64 -t VS2019 -b DEBUG
