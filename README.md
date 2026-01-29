# PciToolPkg
cd /d D:\BIOS\MyWorkSpace\edk2

edksetup.bat Rebuild

chcp 65001

set PYTHONUTF8=1

set PYTHONIOENCODING=utf-8

rmdir /s /q Build\PciToolPkg

build -p PciToolPkg\PciToolPkg.dsc -a X64 -t VS2019 -b DEBUG
