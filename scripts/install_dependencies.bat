@echo off
setlocal EnableExtensions

for %%I in ("%~dp0..") do set "ROOT_DIR=%%~fI"
set "VENDORS_DIR=%ROOT_DIR%\vendors"

if not exist "%VENDORS_DIR%" mkdir "%VENDORS_DIR%"

echo ============================================================================
echo Installing NoxMcp dependencies into:
echo   %VENDORS_DIR%
echo ============================================================================

:: ----------------------------------------------------------------------------
:: 1. cpp-httplib (header-only)
:: ----------------------------------------------------------------------------
set "HTTPLIB_DIR=%VENDORS_DIR%\cpp-httplib"
if not exist "%HTTPLIB_DIR%\httplib.h" (
    echo [1/2] Cloning cpp-httplib into "%HTTPLIB_DIR%"...
    if exist "%HTTPLIB_DIR%" rmdir /s /q "%HTTPLIB_DIR%"
    git clone --depth 1 https://github.com/yhirose/cpp-httplib.git "%HTTPLIB_DIR%"
    if errorlevel 1 (
        echo ERROR: Failed to clone cpp-httplib.
        goto :fail
    )
) else (
    echo [1/2] cpp-httplib already present, skipping.
)

:: ----------------------------------------------------------------------------
:: 2. nlohmann/json single header (v3.11.3)
:: ----------------------------------------------------------------------------
set "JSON_DIR=%VENDORS_DIR%\nlohmann"
if not exist "%JSON_DIR%\json.hpp" (
    echo [2/2] Downloading nlohmann/json.hpp into "%JSON_DIR%"...
    if not exist "%JSON_DIR%" mkdir "%JSON_DIR%"
    curl -fSL "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp" -o "%JSON_DIR%\json.hpp"
    if errorlevel 1 (
        echo ERROR: Failed to download nlohmann/json.hpp.
        goto :fail
    )
) else (
    echo [2/2] nlohmann/json.hpp already present, skipping.
)

echo.
echo All NoxMcp dependencies are ready in "%VENDORS_DIR%".
if /i not "%NRI_NO_PAUSE%"=="1" if /i not "%NOXMCP_NO_PAUSE%"=="1" pause
exit /b 0

:fail
echo.
echo Dependency installation failed.
if /i not "%NRI_NO_PAUSE%"=="1" if /i not "%NOXMCP_NO_PAUSE%"=="1" pause
exit /b 1
