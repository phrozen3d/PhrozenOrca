@echo off
REM Phrozen FDM 工具號重映射測試 —— 建置並執行 tests\phrozen\ 底下的獨立測試檔。
REM
REM 這兩支測試（phrozen_tool_remap_tests.cpp / phrozen_tool_remap_apply_tests.cpp）
REM 刻意不掛進 tests\CMakeLists.txt 的 Catch2 測試樹（見各檔案開頭註解：
REM 「自帶極簡 harness，無 Catch2 相依」），必須各自獨立編譯成可執行檔再執行。
REM
REM 必須在「x64 Native Tools Command Prompt for VS 2022」底下執行，且必須先
REM 建過 deps（deps\build\PhrozenOrca_dep），本腳本不會自動觸發 deps 建置。
REM
REM /MD 是關鍵：libboost_nowide-vc144-mt-x64-1_84.lib 是用動態 CRT 建的
REM （檔名 "mt" 之後沒有 "s"，依 boost 命名慣例代表動態 CRT）。cl.exe 不加
REM /MT 或 /MD 時預設等同 /MT，會跟這顆 lib 的 CRT 不符，導致 LNK2038/LNK2005。
REM /utf-8 用來消除原始碼中文註解在字碼頁 950 下的 C4819 警告。

setlocal
cd /d "%~dp0..\.."

set BOOST_INC=deps\build\PhrozenOrca_dep\usr\local\include\boost-1_84
set BOOST_LIB=deps\build\PhrozenOrca_dep\usr\local\lib
set NOWIDE_LIB=libboost_nowide-vc144-mt-x64-1_84.lib
set OUT_DIR=build\phrozen_tests

if not exist "%BOOST_INC%" (
    echo [錯誤] 找不到 %BOOST_INC%，請先建置 deps（build_release_vs2022.bat deps）。
    exit /b 1
)
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

echo === Building phrozen_tool_remap_tests ===
cl /nologo /EHsc /std:c++17 /MD /utf-8 /DBOOST_ALL_NO_LIB /I"%BOOST_INC%" ^
   tests\phrozen\phrozen_tool_remap_tests.cpp ^
   /Fo"%OUT_DIR%\" /Fe"%OUT_DIR%\remap_tests.exe" ^
   /link /LIBPATH:"%BOOST_LIB%" %NOWIDE_LIB%
if errorlevel 1 goto :build_failed

echo.
echo === Building phrozen_tool_remap_apply_tests ===
cl /nologo /EHsc /std:c++17 /MD /utf-8 /DBOOST_ALL_NO_LIB /I"%BOOST_INC%" ^
   tests\phrozen\phrozen_tool_remap_apply_tests.cpp ^
   /Fo"%OUT_DIR%\" /Fe"%OUT_DIR%\remap_apply_tests.exe" ^
   /link /LIBPATH:"%BOOST_LIB%" %NOWIDE_LIB%
if errorlevel 1 goto :build_failed

echo.
echo === Running remap_tests ===
"%OUT_DIR%\remap_tests.exe"
set RC1=%errorlevel%

echo.
echo === Running remap_apply_tests ===
"%OUT_DIR%\remap_apply_tests.exe"
set RC2=%errorlevel%

echo.
if "%RC1%"=="0" if "%RC2%"=="0" (
    echo ALL TESTS PASSED
    exit /b 0
) else (
    echo TESTS FAILED: remap_tests=%RC1% remap_apply_tests=%RC2%
    exit /b 1
)

:build_failed
echo.
echo BUILD FAILED
exit /b 1
