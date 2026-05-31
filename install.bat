@echo off
title SoapyFlexRadio Installer v2.0
color 0B

echo.
echo  ================================================
echo   SoapyFlexRadio v2.0 - Installer
echo   Plugin SoapySDR para FlexRadio 6600 + SkyRoof
echo   Autor: EA5WA / Claude (Anthropic)
echo  ================================================
echo.
echo  Este instalador copiara SoapyFlexRadio.dll en las
echo  carpetas correctas para que SkyRoof lo detecte.
echo.

REM ── Verificar permisos de administrador ──────────
net session >nul 2>&1
if errorlevel 1 (
    echo  ERROR: Ejecuta este instalador como Administrador.
    echo  Clic derecho en el archivo .bat y selecciona
    echo  "Ejecutar como administrador".
    echo.
    pause
    exit /b 1
)

REM ── Buscar SkyRoof ───────────────────────────────
echo  Buscando instalacion de SkyRoof...
set SKYROOF_DIR=

if exist "C:\RADIO\SkyRoof\SkyRoof.exe"           set SKYROOF_DIR=C:\RADIO\SkyRoof
if exist "C:\Program Files\SkyRoof\SkyRoof.exe"   set SKYROOF_DIR=C:\Program Files\SkyRoof
if exist "%LOCALAPPDATA%\SkyRoof\SkyRoof.exe"     set SKYROOF_DIR=%LOCALAPPDATA%\SkyRoof

if "%SKYROOF_DIR%"=="" (
    echo  No se encontro SkyRoof automaticamente.
    set /p SKYROOF_DIR=  Introduce la ruta de instalacion de SkyRoof: 
)

if not exist "%SKYROOF_DIR%\SkyRoof.exe" (
    echo  ERROR: SkyRoof.exe no encontrado en %SKYROOF_DIR%
    pause
    exit /b 1
)

echo  SkyRoof encontrado en: %SKYROOF_DIR%
echo.

REM ── Crear carpeta de modulos ─────────────────────
set MODULES_DIR=%SKYROOF_DIR%\lib\SoapySDR\modules0.8
echo  Creando carpeta de modulos...
mkdir "%MODULES_DIR%" 2>nul

REM ── Copiar DLL ───────────────────────────────────
echo  Copiando SoapyFlexRadio.dll...
set DLL_SRC=%~dp0SoapyFlexRadio.dll

if not exist "%DLL_SRC%" (
    echo  ERROR: SoapyFlexRadio.dll no encontrado junto al instalador.
    echo  Asegurate de que SoapyFlexRadio.dll esta en la misma
    echo  carpeta que este archivo install.bat.
    pause
    exit /b 1
)

copy /Y "%DLL_SRC%" "%MODULES_DIR%\" >nul
if errorlevel 1 (
    echo  ERROR: No se pudo copiar el dll.
    pause
    exit /b 1
)

echo  DLL instalado en: %MODULES_DIR%
echo.

REM ── Verificar instalacion ────────────────────────
if exist "%MODULES_DIR%\SoapyFlexRadio.dll" (
    echo  [OK] Instalacion completada correctamente.
) else (
    echo  [ERROR] La instalacion fallo.
    pause
    exit /b 1
)

echo.
echo  ================================================
echo   INSTRUCCIONES DE USO
echo  ================================================
echo.
echo  Requisitos previos:
echo    1. SmartSDR corriendo y conectado al FLEX-6600
echo    2. SmartSDR CAT v4.x corriendo
echo    3. smartsdr-iqtransfer.exe (incluido en este pack)
echo.
echo  Para arrancar:
echo    Ejecuta start_skyroof_flex6600.bat
echo    (edita primero la IP de tu radio si es necesario)
echo.
echo  Configuracion en SkyRoof:
echo    - RX CAT: Host=127.0.0.1, Port=60001, Enabled=True
echo    - TX CAT: Host=127.0.0.1, Port=60001, Enabled=True
echo.
pause
