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
set IQTRANSFER=C:\Users\reigc\Downloads\flexlib-go.NOV-2020\flexlib-go\Win64\smartsdr-iqtransfer.exe
set SKYROOF=C:\RADIO\SkyRoof\SkyRoof.exe
set PROXY=%~dp0cat_proxy.py
REM ─────────────────────────────────────────────────

REM Verificar Python
echo [1/4] Verificando Python...
python --version >nul 2>&1
if errorlevel 1 (
    echo       ERROR: Python no encontrado.
    echo       Descargalo desde https://www.python.org/downloads/
    echo       y marca "Add Python to PATH" durante la instalacion.
    pause
    exit /b 1
)
echo       Python OK

REM Verificar que SmartSDR CAT está corriendo
echo [2/4] Verificando SmartSDR CAT...
netstat -ano | findstr ":60001" | findstr "LISTENING" >nul 2>&1
if errorlevel 1 (
    echo       AVISO: SmartSDR CAT no detectado en puerto 60001.
    echo       Asegurate de que SmartSDR y SmartSDR CAT esten abiertos.
    echo.
    pause
)

REM Arrancar el proxy CAT en ventana separada
echo [2/4] Iniciando CAT Proxy...
if not exist "%PROXY%" (
    echo       ERROR: cat_proxy.py no encontrado en:
    echo       %PROXY%
    pause
    exit /b 1
)
start "CAT Proxy" python "%PROXY%" --radio %RADIO_IP% --listen %CAT_PROXY_PORT%
timeout /t 2 /nobreak >nul
echo       CAT Proxy iniciado en puerto %CAT_PROXY_PORT%

REM Arrancar smartsdr-iqtransfer en ventana separada
echo [3/4] Iniciando smartsdr-iqtransfer...
if not exist "%IQTRANSFER%" (
    echo       ERROR: No se encontro smartsdr-iqtransfer.exe en:
    echo       %IQTRANSFER%
    echo       Edita este .bat y corrige la ruta IQTRANSFER.
    pause
    exit /b 1
)

start "smartsdr-iqtransfer" "%IQTRANSFER%" ^
    --RADIO=%RADIO_IP% ^
    --MYUDP=5999 ^
    --RATE=%RATE% ^
    --CH=%DAX_CH% ^
    --FWD=127.0.0.1:%FWD_PORT%

REM Esperar a que el stream IQ se establezca
echo       Esperando conexion IQ con el radio...
timeout /t 4 /nobreak >nul

REM Arrancar SkyRoof
echo [4/4] Iniciando SkyRoof...
if not exist "%SKYROOF%" (
    echo       ERROR: No se encontro SkyRoof.exe en:
    echo       %SKYROOF%
    echo       Edita este .bat y corrige la ruta SKYROOF.
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
echo   IQ Transfer  : UDP %FWD_PORT% a SkyRoof
echo   Radio IP     : %RADIO_IP%
echo.
echo   IMPORTANTE: En SkyRoof Settings:
echo     RX CAT Port = %CAT_PROXY_PORT%
echo     TX CAT Port = %CAT_PROXY_PORT%
echo.
echo  Cierra esta ventana cuando termines de usar SkyRoof.
echo  Para parar: cierra SkyRoof y luego cierra las ventanas
echo  de CAT Proxy y smartsdr-iqtransfer.
echo.
pause
