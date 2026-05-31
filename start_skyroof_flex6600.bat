@echo off
title SkyRoof + FlexRadio 6600 Launcher
color 0A

echo.
echo  ================================================
echo   SkyRoof + FlexRadio 6600 - EA5WA
echo   SoapyFlexRadio v2.0 by Claude/Anthropic
echo  ================================================
echo.

REM ── Configuracion ────────────────────────────────
set RADIO_IP=192.168.0.208
set DAX_CH=1
set RATE=192000
set FWD_PORT=5901
set CAT_PROXY_PORT=60010
set ROTCTLD_PORT=4533
set PSTROTATOR_IP=192.168.0.50
set IQTRANSFER=C:\Users\reigc\Downloads\flexlib-go.NOV-2020\flexlib-go\Win64\smartsdr-iqtransfer.exe
set SKYROOF=C:\RADIO\SkyRoof\SkyRoof.exe
set PROXY=%~dp0cat_proxy.py
set HAMLIB=C:\hamlib\bin
REM ─────────────────────────────────────────────────

REM Verificar Python
echo [1/5] Verificando Python...
python --version >nul 2>&1
if errorlevel 1 (
    echo       ERROR: Python no encontrado.
    echo       Descargalo desde https://www.python.org/downloads/
    pause
    exit /b 1
)
echo       Python OK

REM Verificar que SmartSDR CAT esta corriendo
echo [2/5] Verificando SmartSDR CAT...
netstat -ano | findstr ":60001" | findstr "LISTENING" >nul 2>&1
if errorlevel 1 (
    echo       AVISO: SmartSDR CAT no detectado en puerto 60001.
    echo       Asegurate de que SmartSDR y SmartSDR CAT esten abiertos.
    echo.
    pause
)

REM Verificar que PstRotator esta corriendo
echo [2/5] Verificando PstRotator...
netstat -ano | findstr ":4533" | findstr "LISTENING" >nul 2>&1
if errorlevel 1 (
    echo       AVISO: PstRotator no detectado en puerto 4533.
    echo       Asegurate de que PstRotator esta abierto y tiene
    echo       activado "rotctld hamlib server" en Setup.
    echo.
    pause
)

REM Arrancar CAT Proxy
echo [3/5] Iniciando CAT Proxy...
if not exist "%PROXY%" (
    echo       ERROR: cat_proxy.py no encontrado en %PROXY%
    pause
    exit /b 1
)
start "CAT Proxy" python "%PROXY%" --radio %RADIO_IP% --listen %CAT_PROXY_PORT% --debug
timeout /t 2 /nobreak >nul
echo       CAT Proxy iniciado en puerto %CAT_PROXY_PORT%

REM Arrancar rotctld
echo [3/5] Iniciando rotctld...
if not exist "%HAMLIB%\rotctld.exe" (
    echo       ERROR: rotctld.exe no encontrado en %HAMLIB%
    pause
    exit /b 1
)
start "rotctld" "%HAMLIB%\rotctld.exe" -m 3 -r %PSTROTATOR_IP%:%ROTCTLD_PORT%
timeout /t 2 /nobreak >nul
echo       rotctld iniciado en puerto %ROTCTLD_PORT%

REM Arrancar smartsdr-iqtransfer
echo [4/5] Iniciando smartsdr-iqtransfer...
if not exist "%IQTRANSFER%" (
    echo       ERROR: No se encontro smartsdr-iqtransfer.exe en %IQTRANSFER%
    pause
    exit /b 1
)
start "smartsdr-iqtransfer" "%IQTRANSFER%" --RADIO=%RADIO_IP% --MYUDP=5999 --RATE=%RATE% --CH=%DAX_CH% --FWD=127.0.0.1:%FWD_PORT%
echo       Esperando conexion IQ con el radio...
timeout /t 4 /nobreak >nul

REM Arrancar SkyRoof
echo [5/5] Iniciando SkyRoof...
if not exist "%SKYROOF%" (
    echo       ERROR: No se encontro SkyRoof.exe en %SKYROOF%
    pause
    exit /b 1
)
start "" "%SKYROOF%"

echo.
echo  ================================================
echo   Todo iniciado correctamente
echo  ================================================
echo.
echo   CAT Proxy    : puerto %CAT_PROXY_PORT%
echo   rotctld      : puerto %ROTCTLD_PORT% -^> PstRotator %PSTROTATOR_IP%
echo   IQ Transfer  : UDP %FWD_PORT% a SkyRoof
echo   Radio IP     : %RADIO_IP%
echo.
echo   Orden de arranque previo:
echo     1. SmartSDR + SmartSDR CAT
echo     2. PstRotator (con rotctld hamlib server activo)
echo     3. Este bat
echo.
echo  Para parar: cierra SkyRoof y luego cierra las ventanas
echo  de CAT Proxy, rotctld y smartsdr-iqtransfer.
echo.
pause
