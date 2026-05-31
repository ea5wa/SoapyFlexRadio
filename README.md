# SoapyFlexRadio v2 — Plugin SoapySDR para FlexRadio 6600

Versión simplificada que usa **smartsdr-iqtransfer** como puente al radio.

```
SkyRoof (VE3NEA)
     ↕  SoapySDR API
SoapyFlexRadio.dll   ←── UDP 5900 (Float32 LE, IQ raw)
                                    ↑
                     smartsdr-iqtransfer.exe
                                    ↑
                          FlexRadio 6600 (SmartSDR)
```

---

## Requisitos

- Windows 10/11 x64
- [PothosSDR](http://downloads.myriadrf.org/builds/PothosSDR/?C=M;O=D) instalado
- [smartsdr-iqtransfer.exe](https://github.com/hb9fxq/flexlib-go/releases/tag/Nov2020) instalado
- SmartSDR corriendo y conectado al FLEX-6600
- SkyRoof (VE3NEA)

---

## Compilación

```powershell
cd SoapyFlexRadio2
mkdir build && cd build
cmake .. -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH="C:/Program Files/PothosSDR"
cmake --build . --config Release
cmake --install . --config Release
```

---

## Uso paso a paso

### 1. Preparar SmartSDR
- Abre SmartSDR y conecta al FLEX-6600
- Crea un panadaptador en la frecuencia que quieras monitorear
- Asigna el canal DAX-IQ 1 al panadaptador (menú DAX del panadaptador)

### 2. Iniciar smartsdr-iqtransfer
Abre PowerShell y ejecuta:
```powershell
.\smartsdr-iqtransfer.exe --RADIO=192.168.0.208 --MYUDP=5999 --RATE=96000 --CH=1 --FWD=127.0.0.1:5900
```
- `--RADIO` → IP de tu FlexRadio
- `--RATE`  → debe coincidir con el rate configurado en DAX-IQ (24/48/96/192)
- `--FWD`   → siempre `127.0.0.1:5900` (localhost, puerto 5900)

Deja esta ventana abierta. Verás los mensajes DEBUG indicando que el stream está activo.

### 3. Verificar el plugin
```powershell
SoapySDRUtil.exe --find="driver=FlexRadio"
# Debe mostrar: FlexRadio FLEX-6600 (via smartsdr-iqtransfer) UDP:5900

SoapySDRUtil.exe --probe="driver=FlexRadio,udp_port=5900"
```

### 4. Configurar SkyRoof
1. SkyRoof → **Tools → SDR Devices → Add**
2. Parámetros:
   ```
   driver=FlexRadio, udp_port=5900, rate=96000
   ```
3. Sample Rate: **96000** (debe coincidir con `--RATE` de smartsdr-iqtransfer)
4. La frecuencia central la toma del panadaptador de SmartSDR automáticamente

---

## Parámetros del driver

| Parámetro  | Defecto    | Descripción                                      |
|------------|------------|--------------------------------------------------|
| `udp_port` | `5900`     | Puerto UDP local donde llegan los datos IQ       |
| `rate`     | `96000`    | Sample rate en Hz (informativo para SkyRoof)     |
| `freq`     | `14200000` | Frecuencia inicial en Hz (informativo)           |

> **Nota:** La frecuencia y el sample rate reales los controla SmartSDR /
> smartsdr-iqtransfer. Los valores aquí son solo informativos para que
> SkyRoof sepa con qué frecuencia y ancho de banda está trabajando.

---

## Iniciar todo automáticamente (opcional)

Crea un archivo `start_skyroof.bat` en el escritorio:

```batch
@echo off
echo Iniciando smartsdr-iqtransfer...
start "IQ Transfer" "C:\ruta\smartsdr-iqtransfer.exe" --RADIO=192.168.0.208 --MYUDP=5999 --RATE=96000 --CH=1 --FWD=127.0.0.1:5900
timeout /t 3
echo Iniciando SkyRoof...
start "" "C:\Program Files\SkyRoof\SkyRoof.exe"
```

---

## Solución de problemas

| Problema | Solución |
|----------|----------|
| SkyRoof no recibe señal | Verifica que smartsdr-iqtransfer muestra "Forwarding data to 127.0.0.1:5900" |
| Puerto 5900 ocupado | Cambia a otro puerto: `--FWD=127.0.0.1:5901` y `udp_port=5901` en SkyRoof |
| No encuentra el driver | Ejecuta `cmake --install` y reinicia SkyRoof |
| Waterfall congelado | El canal DAX-IQ debe estar asignado y activo en SmartSDR |
