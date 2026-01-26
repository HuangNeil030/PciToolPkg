/** @file
  PciTool.c
  Interactive PCI Tool with Device List and Cursor-based Editing.
**/

#include <Uefi.h>
#include <Protocol/PciRootBridgeIo.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include <Library/ShellLib.h>

// -----------------------------------------------------------------------------
// 定義與結構
// -----------------------------------------------------------------------------
#define MAX_PCI_DEVICES 512
#define PCI_LIB_ADDRESS(Bus,Device,Function,Offset) \
  (((Offset) & 0xfff) | (((Function) & 0x07) << 8) | (((Device) & 0x1f) << 16) | (((Bus) & 0xff) << 24))

typedef struct {
  UINTN   Bus;
  UINTN   Dev;
  UINTN   Func;
  UINT16  VendorId;
  UINT16  DeviceId;
} PCI_DEVICE_ENTRY;

typedef enum {
  STATE_DEVICE_LIST,    // 初始畫面：選擇裝置
  STATE_VIEW_DUMP,      // 查看 Dump
  STATE_SELECT_OFFSET,  // 按下 W 後：移動游標選擇 Offset
  STATE_INPUT_VALUE     // 按下 Enter 後：輸入數值
} TOOL_STATE;

typedef enum {
  WIDTH_BYTE = 1,
  WIDTH_WORD = 2,
  WIDTH_DWORD = 4
} DATA_WIDTH;

// -----------------------------------------------------------------------------
// 全域變數
// -----------------------------------------------------------------------------
EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL  *gPciIo = NULL;
PCI_DEVICE_ENTRY  gDeviceList[MAX_PCI_DEVICES];
UINTN             gDeviceCount = 0;
UINTN             gListIndex = 0;        // 列表選擇的光標
UINTN             gTargetOffset = 0;     // Dump 畫面的光標 (0x00 - 0xFF)
DATA_WIDTH        gWidth = WIDTH_DWORD;  // 預設 Dword
TOOL_STATE        gState = STATE_DEVICE_LIST;
CHAR16            gInputBuffer[10];      // 輸入數值緩衝區
UINTN             gInputIndex = 0;

// -----------------------------------------------------------------------------
// 函數：掃描所有 PCI 裝置
// -----------------------------------------------------------------------------
VOID
ScanDevices (VOID)
{
  UINTN   Bus, Dev, Func;
  UINT64  Address;
  UINT32  VidDid;
  UINT8   HeaderType;
  
  gDeviceCount = 0;
  gListIndex = 0;

  for (Bus = 0; Bus < 256; Bus++) {
    for (Dev = 0; Dev < 32; Dev++) {
      // Check Function 0
      Address = PCI_LIB_ADDRESS(Bus, Dev, 0, 0);
      gPciIo->Pci.Read(gPciIo, EfiPciWidthUint32, Address, 1, &VidDid);
      
      if ((VidDid & 0xFFFF) == 0xFFFF) continue;

      // Check Multi-function
      Address = PCI_LIB_ADDRESS(Bus, Dev, 0, 0x0E);
      gPciIo->Pci.Read(gPciIo, EfiPciWidthUint8, Address, 1, &HeaderType);
      UINTN MaxFunc = ((HeaderType & 0x80) != 0) ? 8 : 1;

      for (Func = 0; Func < MaxFunc; Func++) {
        Address = PCI_LIB_ADDRESS(Bus, Dev, Func, 0);
        gPciIo->Pci.Read(gPciIo, EfiPciWidthUint32, Address, 1, &VidDid);

        if ((VidDid & 0xFFFF) != 0xFFFF && gDeviceCount < MAX_PCI_DEVICES) {
          gDeviceList[gDeviceCount].Bus = Bus;
          gDeviceList[gDeviceCount].Dev = Dev;
          gDeviceList[gDeviceCount].Func = Func;
          gDeviceList[gDeviceCount].VendorId = (UINT16)(VidDid & 0xFFFF);
          gDeviceList[gDeviceCount].DeviceId = (UINT16)(VidDid >> 16);
          gDeviceCount++;
        }
      }
    }
  }
}

// -----------------------------------------------------------------------------
// 畫面繪製：裝置列表
// -----------------------------------------------------------------------------
VOID
DrawDeviceList (VOID)
{
  UINTN i;
  UINTN StartRow = 0;
  UINTN MaxRows = 20; // 一頁顯示幾行

  // 簡單的捲動邏輯
  if (gListIndex >= MaxRows) {
    StartRow = gListIndex - MaxRows + 1;
  }

  Print(L"=== PCI Device List (Found: %d) ===\n", gDeviceCount);
  Print(L"Use UP/DOWN to select, ENTER to view, Q to Quit\n\n");
  Print(L"Bus  Device  Function   VenderID   DeviceID\n");
  Print(L"------------------------------------------------\n");

  for (i = StartRow; i < gDeviceCount && i < StartRow + MaxRows; i++) {
    // 高亮選中的行
    if (i == gListIndex) {
      gST->ConOut->SetAttribute(gST->ConOut, EFI_BACKGROUND_GREEN | EFI_WHITE);
      Print(L" --> ");
    } else {
      gST->ConOut->SetAttribute(gST->ConOut, EFI_BACKGROUND_BLACK | EFI_LIGHTGRAY);
      Print(L"     ");
    }

    Print(L"%02x  %02x  %02x  %04x  %04x \n", 
      gDeviceList[i].Bus, gDeviceList[i].Dev, gDeviceList[i].Func, 
      gDeviceList[i].VendorId, gDeviceList[i].DeviceId);
    
    // 恢復顏色
    gST->ConOut->SetAttribute(gST->ConOut, EFI_BACKGROUND_BLACK | EFI_LIGHTGRAY);
  }
}

// -----------------------------------------------------------------------------
// 畫面繪製：Hex Dump (包含游標邏輯)
// -----------------------------------------------------------------------------
VOID
DrawDumpView (VOID)
{
  EFI_STATUS                            Status;
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_WIDTH PciWidth;
  UINT64                                Address;
  UINTN                                 Count;
  UINT8                                 Buffer[256];
  UINTN                                 i;
  PCI_DEVICE_ENTRY                      *Target = &gDeviceList[gListIndex];

  // 1. 讀取數據
  if (gWidth == WIDTH_BYTE) { PciWidth = EfiPciWidthUint8; Count = 256; }
  else if (gWidth == WIDTH_WORD) { PciWidth = EfiPciWidthUint16; Count = 128; }
  else { PciWidth = EfiPciWidthUint32; Count = 64; }

  ZeroMem(Buffer, sizeof(Buffer));
  Address = PCI_LIB_ADDRESS(Target->Bus, Target->Dev, Target->Func, 0);
  Status = gPciIo->Pci.Read (gPciIo, PciWidth, Address, Count, Buffer);

  // 2. 顯示標頭
  Print(L"Viewing: [%02x:%02x:%02x] VID:%04x DID:%04x\n", 
    Target->Bus, Target->Dev, Target->Func, Target->VendorId, Target->DeviceId);
  
  if (gState == STATE_VIEW_DUMP) {
    Print(L"[W]: Write Mode  [Space]: Width(%s)  [Esc]: Back\n", 
      (gWidth==1)?L"8":(gWidth==2)?L"16":L"32");
  } else if (gState == STATE_SELECT_OFFSET) {
    gST->ConOut->SetAttribute(gST->ConOut, EFI_BACKGROUND_BLUE | EFI_WHITE);
    Print(L"[Mode: Select Offset] Arrows: Move  Enter: Edit  Esc: Cancel\n");
    gST->ConOut->SetAttribute(gST->ConOut, EFI_BACKGROUND_BLACK | EFI_LIGHTGRAY);
  } else if (gState == STATE_INPUT_VALUE) {
    gST->ConOut->SetAttribute(gST->ConOut, EFI_BACKGROUND_RED | EFI_WHITE);
    Print(L"[Mode: Input Value] Offset: 0x%02X  Enter: Confirm\n", gTargetOffset);
    gST->ConOut->SetAttribute(gST->ConOut, EFI_BACKGROUND_BLACK | EFI_LIGHTGRAY);
  }
  
  Print(L"--------------------------------------------------------\n");
  Print(L"     00  01  02  03  04  05  06  07  08  09  0A  0B  0C  0D  0E  0F\n");
  Print(L"--------------------------------------------------------\n");

  // 3. 繪製 Hex Grid
  for (i = 0; i < Count; i++) {
    // 顯示 Row Offset (每 16 Bytes 一行)
    UINTN CurrentByteAddr = i * gWidth;
    
    if (CurrentByteAddr % 16 == 0) {
      Print(L"%02X | ", CurrentByteAddr);
    }

    // --- 游標邏輯 ---
    // 如果處於「選擇 Offset」或「輸入數值」模式，且當前格子的位址等於 gTargetOffset
    // 則高亮顯示該格子
    BOOLEAN IsSelected = FALSE;
    if ((gState == STATE_SELECT_OFFSET || gState == STATE_INPUT_VALUE) && 
        CurrentByteAddr == gTargetOffset) {
      IsSelected = TRUE;
      gST->ConOut->SetAttribute(gST->ConOut, EFI_BACKGROUND_CYAN | EFI_BLACK);
    }

    // 根據寬度印出數值
    if (gWidth == WIDTH_BYTE) {
      Print(L"%02X ", Buffer[i]);
    } else if (gWidth == WIDTH_WORD) {
      Print(L"%04X ", ((UINT16*)Buffer)[i]);
    } else {
      Print(L"%08X ", ((UINT32*)Buffer)[i]);
    }

    // 恢復顏色
    if (IsSelected) {
      gST->ConOut->SetAttribute(gST->ConOut, EFI_BACKGROUND_BLACK | EFI_LIGHTGRAY);
    }

    // 換行
    if ((CurrentByteAddr + gWidth) % 16 == 0) {
      Print(L"\n");
    }
  }

  // 4. 輸入區 (僅在 Input Value 模式下顯示)
  if (gState == STATE_INPUT_VALUE) {
    Print(L"\n> Enter Value (Hex): %s_\n", gInputBuffer);
  } else {
    Print(L"\n\n");
  }
}

// -----------------------------------------------------------------------------
// 功能：執行寫入
// -----------------------------------------------------------------------------
VOID
ExecuteWrite (VOID)
{
  EFI_STATUS                            Status;
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_WIDTH PciWidth;
  UINT64                                Address;
  UINTN                                 Value;
  PCI_DEVICE_ENTRY                      *Target = &gDeviceList[gListIndex];

  Value = ShellHexStrToUintn(gInputBuffer);

  if (gWidth == WIDTH_BYTE) PciWidth = EfiPciWidthUint8;
  else if (gWidth == WIDTH_WORD) PciWidth = EfiPciWidthUint16;
  else PciWidth = EfiPciWidthUint32;

  Address = PCI_LIB_ADDRESS(Target->Bus, Target->Dev, Target->Func, gTargetOffset);
  
  Status = gPciIo->Pci.Write(gPciIo, PciWidth, Address, 1, &Value);
  
  // 寫入後清空輸入
  ZeroMem(gInputBuffer, sizeof(gInputBuffer));
  gInputIndex = 0;
  
  // 回到瀏覽模式 (Dump 會自動更新)
  gState = STATE_VIEW_DUMP;
}

// -----------------------------------------------------------------------------
// 主程式入口
// -----------------------------------------------------------------------------
INTN
EFIAPI
ShellAppMain (
  IN UINTN Argc,
  IN CHAR16 **Argv
  )
{
  EFI_STATUS      Status;
  EFI_INPUT_KEY   Key;
  UINTN           EventIndex;
  BOOLEAN         Refresh = TRUE;
  
  // 1. Locate Protocol
  Status = gBS->LocateProtocol (&gEfiPciRootBridgeIoProtocolGuid, NULL, (VOID **)&gPciIo);
  if (EFI_ERROR (Status)) {
    Print(L"Error: PciRootBridgeIo not found.\n");
    return 1;
  }

  // 2. 初始掃描
  ScanDevices();

  // 3. 主迴圈
  while (TRUE) {
    if (Refresh) {
      gST->ConOut->ClearScreen(gST->ConOut);
      
      if (gState == STATE_DEVICE_LIST) {
        DrawDeviceList();
      } else {
        DrawDumpView();
      }
      Refresh = FALSE;
    }

    gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &EventIndex);
    Status = gST->ConIn->ReadKeyStroke(gST->ConIn, &Key);

    if (!EFI_ERROR(Status)) {
      Refresh = TRUE; // 任何按鍵都重繪

      // --- 全域按鍵 ---
      if (gState != STATE_INPUT_VALUE && (Key.UnicodeChar == L'q' || Key.UnicodeChar == L'Q')) {
         if (gState == STATE_DEVICE_LIST) break; // Quit App
         else {
             gState = STATE_DEVICE_LIST; // Back to List
             continue;
         }
      }

      // --- 狀態機處理 ---
      switch (gState) {
        
        // 1. 列表選擇模式
        case STATE_DEVICE_LIST:
          if (Key.ScanCode == SCAN_UP && gListIndex > 0) gListIndex--;
          if (Key.ScanCode == SCAN_DOWN && gListIndex < gDeviceCount - 1) gListIndex++;
          if (Key.UnicodeChar == 0x0D) { // Enter
             gState = STATE_VIEW_DUMP;
             gTargetOffset = 0; // 重置 Dump 游標
          }
          break;

        // 2. 瀏覽模式
        case STATE_VIEW_DUMP:
          if (Key.ScanCode == SCAN_ESC) gState = STATE_DEVICE_LIST;
          if (Key.UnicodeChar == L' ') { // Space 切換寬度
             if (gWidth == WIDTH_BYTE) gWidth = WIDTH_WORD;
             else if (gWidth == WIDTH_WORD) gWidth = WIDTH_DWORD;
             else gWidth = WIDTH_BYTE;
          }
          if (Key.UnicodeChar == L'w' || Key.UnicodeChar == L'W') {
             gState = STATE_SELECT_OFFSET; // 進入游標選擇模式
          }
          break;

        // 3. 游標選擇 Offset 模式
        case STATE_SELECT_OFFSET:
          if (Key.ScanCode == SCAN_ESC) gState = STATE_VIEW_DUMP; // Cancel
          
          // 方向鍵移動游標
          if (Key.ScanCode == SCAN_UP) {
             if (gTargetOffset >= 16) gTargetOffset -= 16;
          }
          if (Key.ScanCode == SCAN_DOWN) {
             if (gTargetOffset <= 0xF0 - 16) gTargetOffset += 16;
          }
          if (Key.ScanCode == SCAN_LEFT) {
             if (gTargetOffset >= gWidth) gTargetOffset -= gWidth;
          }
          if (Key.ScanCode == SCAN_RIGHT) {
             if (gTargetOffset <= 0xFF - gWidth) gTargetOffset += gWidth;
          }

          // Enter 確認位置，進入輸入模式
          if (Key.UnicodeChar == 0x0D) {
             gState = STATE_INPUT_VALUE;
             ZeroMem(gInputBuffer, sizeof(gInputBuffer));
             gInputIndex = 0;
          }
          break;

        // 4. 輸入數值模式
        case STATE_INPUT_VALUE:
          if (Key.ScanCode == SCAN_ESC) gState = STATE_VIEW_DUMP; // Cancel
          else if (Key.UnicodeChar == 0x0D) { // Enter 執行寫入
             ExecuteWrite();
          } 
          else if (Key.UnicodeChar == 0x08) { // Backspace
             if (gInputIndex > 0) {
               gInputIndex--;
               gInputBuffer[gInputIndex] = 0;
             }
          }
          else if (gInputIndex < 8) { // 輸入 Hex 數字
             CHAR16 Char = Key.UnicodeChar;
             if ((Char >= L'0' && Char <= L'9') || 
                 (Char >= L'a' && Char <= L'f') || 
                 (Char >= L'A' && Char <= L'F')) {
                 gInputBuffer[gInputIndex] = Char;
                 gInputIndex++;
             }
          }
          break;
      }
    }
  }

  return 0;
}