# SoapyFlexRadio v2.0
## Plugin SoapySDR para FlexRadio 6600 + SkyRoof (VE3NEA)

Desarrollado por EA5WA con Claude (Anthropic)

---

## Contenido del paquete

```
SoapyFlexRadio.dll          ← Plugin SoapySDR (copiar en SkyRoof)
install.bat                 ← Instalador automático
start_skyroof_flex6600.bat  ← Lanzador (editar rutas antes de usar)
smartsdr-iqtransfer.exe     ← Puente IQ (de flexlib-go by HB9FXQ)
README.md                   ← Este archivo
```

---

## Requisitos

- Windows 10/11 x64
- **SkyRoof 1.x** (VE3NEA) instalado
- **SmartSDR** corriendo y conectado al FLEX-6600
- **SmartSDR CAT v4.x** corriendo con puerto TCP 60001 activo
- FlexRadio FLEX-6600 con transverter VHF/UHF (para satélites)

---

## Instalación

1. Descarga y descomprime el paquete
2. Clic derecho en `install.bat` → **Ejecutar como administrador**
3. El instalador detecta SkyRoof automáticamente y copia el dll

---

## Configuración en SkyRoof

En **Tools → Settings**:

**CAT Control:**
- RX CAT → Host: `127.0.0.1`, TCP Port: `60001`, Enabled: `True`
- TX CAT → Host: `127.0.0.1`, TCP Port: `60001`, Enabled: `True`

**SoapyRemote:**
- Enabled: `False`

---

## Configuración en SmartSDR CAT

Crear o editar una entrada TCP con:
- Port Type: **TCP**
- TCP Port: **60001**
- VFO A Slice: **A** (o el slice con el transverter UHF)

---

## Uso diario

1. Abre **SmartSDR** y conecta al radio
2. Abre **SmartSDR CAT**
3. Ejecuta `start_skyroof_flex6600.bat`
   - Lanza `smartsdr-iqtransfer` automáticamente
   - Lanza SkyRoof automáticamente
4. En SkyRoof selecciona un satélite en paso
5. El waterfall muestra el espectro IQ del radio
6. La frecuencia se ajusta automáticamente con corrección Doppler

---

## Parámetros de smartsdr-iqtransfer

Edita `start_skyroof_flex6600.bat` y ajusta:

```
set RADIO_IP=192.168.0.208    ← IP de tu FlexRadio
set DAX_CH=1                  ← Canal DAX-IQ (1-8)
set RATE=192000               ← Sample rate (24000/48000/96000/192000)
set FWD_PORT=5901             ← Puerto UDP local (no cambiar)
```

---

## Arquitectura

```
SkyRoof (VE3NEA)
    ↕ SoapySDR API + CAT TCP:60001
SoapyFlexRadio.dll
    ↕ UDP:5901 (IQ Float32 LE)      ↕ TCP:60001 (Kenwood CAT)
smartsdr-iqtransfer.exe         SmartSDR CAT
    ↕ SmartSDR API
FlexRadio FLEX-6600
```

---

## Créditos

- **HB9FXQ** — flexlib-go / smartsdr-iqtransfer
- **VE3NEA** — SkyRoof
- **FlexRadio Systems** — SmartSDR API
- **EA5WA / Claude (Anthropic)** — SoapyFlexRadio plugin

---

## Licencia

MIT License — libre para uso personal y distribución.
