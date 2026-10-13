@echo off
rem Build Tally on Windows with Visual Studio's compiler (run from a "Developer Command Prompt").
cl /nologo /std:c++17 /O2 /EHsc /utf-8 /bigobj /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS src\*.cpp /Fe:tally.exe shell32.lib
