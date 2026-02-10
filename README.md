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

> **Note**: 本工具僅供學習與調試使用。隨意寫入 PCI Config Space 可能導致系統不穩定或當機，請在虛擬機或測試機上使用。

---
cd /d D:\BIOS\MyWorkSpace\edk2

edksetup.bat Rebuild

chcp 65001

set PYTHONUTF8=1

set PYTHONIOENCODING=utf-8

rmdir /s /q Build\PciToolPkg

build -p PciToolPkg\PciToolPkg.dsc -a X64 -t VS2019 -b DEBUG
