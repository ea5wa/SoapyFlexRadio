# SoapyFlexRadio — Wiki

**Plugin SoapySDR para usar el FlexRadio FLEX-6600 con SkyRoof (VE3NEA)**

Versión: 2.0 | Autor: EA5WA con Claude (Anthropic) | Licencia: MIT

---

## Índice

1. [¿Qué es SoapyFlexRadio?](#1-qué-es-soapyflexradio)
2. [Arquitectura del sistema](#2-arquitectura-del-sistema)
3. [Requisitos](#3-requisitos)
4. [Instalación](#4-instalación)
5. [Configuración](#5-configuración)
6. [Uso diario](#6-uso-diario)
7. [Parámetros del driver](#7-parámetros-del-driver)
8. [Protocolo técnico](#8-protocolo-técnico)
9. [Compilación desde fuentes](#9-compilación-desde-fuentes)
10. [Resolución de problemas](#10-resolución-de-problemas)
11. [Preguntas frecuentes](#11-preguntas-frecuentes)
12. [Créditos y licencias](#12-créditos-y-licencias)

---

## 1. ¿Qué es SoapyFlexRadio?

SoapyFlexRadio es un **plugin (módulo) para la librería SoapySDR** que permite conectar el transceptor **FlexRadio FLEX-6600** con el software de seguimiento de satélites **SkyRoof** de VE3NEA.

### ¿Por qué es necesario?

SkyRoof utiliza SoapySDR como capa de abstracción para comunicarse con receptores SDR. El FlexRadio no es un SDR convencional — usa su propio protocolo propietario **SmartSDR** sobre red TCP/IP — y no dispone de un plugin SoapySDR oficial. Este proyecto cubre esa ausencia.

### ¿Qué permite hacer?

- Ver el **waterfall de espectro** del FlexRadio directamente en SkyRoof
- **Corrección Doppler automática**: SkyRoof calcula y aplica la corrección de frecuencia en tiempo real durante los pasos de satélite
- **Control de frecuencia completo**: el panadaptador y el slice de SmartSDR se sincronizan con la frecuencia del satélite seleccionado en SkyRoof
- Escuchar el **audio del satélite** demodulado por SmartSDR

---

## 2. Arquitectura del sistema

```
┌─────────────────────────────────────────────────────────────────┐
│                         Windows PC                              │
│                                                                 │
│  ┌─────────────┐    SoapySDR API    ┌──────────────────────┐   │
│  │   SkyRoof   │ ◄────────────────► │  SoapyFlexRadio.dll  │   │
│  │  (VE3NEA)   │                    │     (este plugin)    │   │
│  └─────────────┘                    └──────┬──────┬────────┘   │
│                                            │      │            │
│                              UDP:5901      │      │ TCP:60001  │
│                           (IQ Float32 LE)  │      │ (CAT KW)   │
│                                            │      │            │
│  ┌─────────────────────┐         ┌─────────┘      │            │
│  │ smartsdr-iqtransfer │         │       ┌─────────┘            │
│  │   (HB9FXQ/Go)       │         │       │                      │
│  └──────────┬──────────┘         │  ┌────▼──────────────────┐  │
│             │                    │  │   SmartSDR CAT v4.x   │  │
│             │ SmartSDR API       │  └───────────────────────┘  │
│             │ TCP:4992           │                              │
│             │ UDP VITA-49        │  TCP:4992 (SmartSDR API)    │
└─────────────┼────────────────────┼─────────────────────────────┘
              │                    │
              ▼                    ▼
     ┌────────────────────────────────┐
     │       FlexRadio FLEX-6600      │
     │     SmartSDR firmware v3/v4    │
     └────────────────────────────────┘
```

### Flujo de datos

| Canal | Protocolo | Dirección | Propósito |
|-------|-----------|-----------|-----------|
| UDP 5901 | Float32 LE raw | Radio → PC | Muestras IQ (waterfall/audio) |
| TCP 60001 | Kenwood CAT | PC → Radio | Control de frecuencia (Doppler) |
| TCP 4992 | SmartSDR API | PC → Radio | Mover centro del panadaptador |

### Componentes

**smartsdr-iqtransfer** (HB9FXQ)
Proceso Go que habla SmartSDR con el radio y reenvía las muestras IQ en bruto por UDP local. Actúa como puente entre el protocolo propietario SmartSDR y el plugin C++.

**SoapyFlexRadio.dll**
El plugin propiamente dicho. Recibe el IQ de smartsdr-iqtransfer, implementa la API SoapySDR para SkyRoof, y envía comandos de frecuencia al radio via CAT y SmartSDR API.

**SmartSDR CAT**
Servidor CAT oficial de FlexRadio que emula el protocolo Kenwood TS-2000 via TCP. Recibe los comandos `FA` del plugin y los traduce a comandos SmartSDR internos.

---

## 3. Requisitos

### Software

| Componente | Versión | Descarga |
|------------|---------|----------|
| Windows | 10/11 x64 | — |
| SkyRoof | 1.x | ve3nea.com |
| SmartSDR | v3.x o v4.x | flexradio.com |
| SmartSDR CAT | v4.x | flexradio.com |
| smartsdr-iqtransfer | Nov2020 | github.com/hb9fxq/flexlib-go/releases |

### Hardware

- FlexRadio **FLEX-6600** (también compatible con otros modelos FLEX con SmartSDR)
- Para satélites VHF/UHF: transverter externo conectado al FLEX-6600 (configurado como `xvtr` en SmartSDR)
- Conexión de red entre el PC y el FlexRadio (LAN local)

---

## 4. Instalación

### Instalación automática

1. Descarga el paquete de distribución
2. Clic derecho en `install.bat` → **Ejecutar como administrador**
3. El instalador detecta SkyRoof automáticamente y copia `SoapyFlexRadio.dll` en la carpeta correcta

### Instalación manual

Copia `SoapyFlexRadio.dll` en la carpeta de módulos de SoapySDR de SkyRoof:

```
C:\RADIO\SkyRoof\lib\SoapySDR\modules0.8\SoapyFlexRadio.dll
```

> La ruta exacta depende de dónde esté instalado SkyRoof en tu sistema. Busca la carpeta `lib\SoapySDR\modules0.8` dentro del directorio de SkyRoof.

### Verificación

Abre PowerShell y ejecuta:

```powershell
SoapySDRUtil.exe --find="driver=FlexRadio"
```

Debe mostrar:
```
Found device 0
  driver   = FlexRadio
  label    = FlexRadio FLEX-6600 (via smartsdr-iqtransfer) UDP:5901
  udp_port = 5901
```

---

## 5. Configuración

### SmartSDR CAT

Abre SmartSDR CAT y crea (o verifica) una entrada con estos parámetros:

| Campo | Valor |
|-------|-------|
| Port Type | TCP |
| TCP Port | **60001** |
| VFO A Slice | **A** (o el slice con el transverter UHF activo) |
| Protocol | CAT |

> **Importante:** el Slice debe coincidir con el que tiene asignado el canal DAX-IQ y el transverter UHF.

### SkyRoof

En **Tools → Settings**:

**CAT Control → RX CAT:**
- Host: `127.0.0.1`
- TCP Port: `60001`
- Enabled: `True`

**CAT Control → TX CAT:**
- Host: `127.0.0.1`
- TCP Port: `60001`
- Enabled: `True`

**SoapyRemote:**
- Enabled: `False`

### smartsdr-iqtransfer

Edita `start_skyroof_flex6600.bat` y ajusta estas variables:

```batch
set RADIO_IP=192.168.0.208    ← IP de tu FlexRadio en la red local
set DAX_CH=1                  ← Canal DAX-IQ asignado al panadaptador (1-8)
set RATE=192000               ← Sample rate en Hz
set FWD_PORT=5901             ← Puerto UDP local (no cambiar)
```

> El `RATE` debe coincidir con lo que SkyRoof solicita. SkyRoof 1.30 pide 192000 Hz para satélites UHF.

---

## 6. Uso diario

### Orden de arranque

El orden es importante. Siempre arranca en este orden:

```
1. SmartSDR          → conecta al FLEX-6600
2. SmartSDR CAT      → activa el servidor CAT TCP
3. start_skyroof_flex6600.bat → lanza iqtransfer y SkyRoof
```

### Primer uso con SkyRoof

1. En SkyRoof → **Tools → SDR Devices**
2. Selecciona **FlexRadio FLEX-6600 (via smartsdr-iqtransfer) UDP:5901**
3. Pulsa **OK**
4. Selecciona un satélite en paso desde la lista
5. El waterfall mostrará el espectro y la frecuencia se ajustará automáticamente con Doppler

### Ajuste del waterfall

Haz clic en el icono de ajustes del waterfall (botón con sliders) para acceder a:
- **Brillo** (Brightness)
- **Contraste** (Contrast)  
- **Velocidad de scroll** (Scrolling Speed) — recomendado: 16 pixels/s para satélites LEO

---

## 7. Parámetros del driver

El driver acepta los siguientes parámetros de configuración:

| Parámetro | Defecto | Descripción |
|-----------|---------|-------------|
| `host` | `192.168.0.208` | IP del FlexRadio en la red local |
| `udp_port` | `5901` | Puerto UDP local donde llegan las muestras IQ de smartsdr-iqtransfer |
| `rate` | `192000` | Sample rate en Hz (informativo para SkyRoof) |
| `freq` | `14200000` | Frecuencia inicial en Hz |

### Tasas de muestreo soportadas

| Rate (Hz) | Ancho de banda | Uso recomendado |
|-----------|----------------|-----------------|
| 24000 | 24 kHz | Satélites HF, CW/SSB estrecho |
| 48000 | 48 kHz | Satélites VHF/UHF banda estrecha |
| 96000 | 96 kHz | Uso general |
| **192000** | **192 kHz** | **Satélites UHF (recomendado con SkyRoof 1.30)** |

> SkyRoof 1.30 solicita automáticamente 192000 Hz para satélites UHF. El valor de `RATE` en smartsdr-iqtransfer **debe coincidir** con lo que SkyRoof solicita.

---

## 8. Protocolo técnico

### Recepción IQ (UDP)

`smartsdr-iqtransfer` recibe los paquetes VITA-49 del radio (que contienen las muestras IQ en big-endian) y los reenvía al plugin como **Float32 little-endian interleaved** (I₀, Q₀, I₁, Q₁, ...) por UDP, sin cabecera. El plugin los recibe, los almacena en un buffer circular y los entrega a SkyRoof vía `readStream()`.

### Control de frecuencia (CAT TCP)

Cuando SkyRoof llama a `setFrequency()`, el plugin envía el comando Kenwood al SmartSDR CAT:

```
FA<11 dígitos>;
Ejemplo: FA00437250000;  → 437.250000 MHz
```

La conexión TCP con SmartSDR CAT es **persistente** — se establece una sola vez al inicio y se reutiliza para todos los cambios de frecuencia. Si se pierde la conexión, el plugin reconecta automáticamente.

### Control del panadaptador (SmartSDR API TCP)

Simultáneamente, el plugin envía el comando SmartSDR nativo por TCP puerto 4992 para mover el centro del panadaptador:

```
Cnn|display pan set 0x40000000 center=437.250000
```

Donde `nn` es el número de secuencia del comando y `0x40000000` es el ID del panadaptador principal. Esto garantiza que el IQ recibido esté siempre centrado en la frecuencia del satélite.

### Buffer circular IQ

El plugin usa un buffer circular de **256k muestras CF32** (~4 MB). Cuando el buffer se llena, las muestras más antiguas se descartan automáticamente para mantener la latencia baja. Los pares I/Q siempre se mantienen alineados para evitar inversión de espectro.

---

## 9. Compilación desde fuentes

### Requisitos de compilación

- Visual Studio 2022 o superior (Build Tools suficiente)
- CMake 3.10 o superior
- SkyRoof instalado (proporciona SoapySDR.dll y cabeceras)

### Pasos

```powershell
# Clonar o descomprimir las fuentes
cd SoapyFlexRadio2

# Crear directorio de build y configurar
mkdir build && cd build
cmake .. -G "Visual Studio 18 2026" -A x64 `
         -DCMAKE_PREFIX_PATH="C:/RADIO/SkyRoof"

# Compilar
cmake --build . --config Release

# Instalar
copy "Release\SoapyFlexRadio.dll" `
     "C:\RADIO\SkyRoof\lib\SoapySDR\modules0.8\" /Y
```

> Sustituye `"C:/RADIO/SkyRoof"` por la ruta real de tu instalación de SkyRoof. CMake necesita encontrar `SoapySDR.dll` y sus cabeceras en esa ruta.

### Estructura de fuentes

```
SoapyFlexRadio2/
├── CMakeLists.txt              ← Sistema de compilación
├── src/
│   ├── SoapyFlexRadio.hpp      ← Declaración de la clase
│   ├── SoapyFlexRadio.cpp      ← Implementación completa
│   ├── Registration.cpp        ← Registro en SoapySDR + discovery
│   └── SoapyFlexRadio.def      ← Exportación de símbolos DLL
├── install.bat                 ← Instalador para usuarios
├── start_skyroof_flex6600.bat  ← Lanzador
└── README_DISTRIBUTION.md     ← Guía de distribución
```

---

## 10. Resolución de problemas

### El FlexRadio no aparece en SDR Devices de SkyRoof

1. Verifica que `SoapyFlexRadio.dll` está en `<SkyRoof>\lib\SoapySDR\modules0.8\`
2. Reinicia SkyRoof completamente
3. Comprueba el log de debug: `type "C:\Users\Public\SoapyFlexRadio_debug.txt"`
4. Si el log no se crea, el dll no se está cargando — verifica que fue compilado contra la SoapySDR de SkyRoof

### El waterfall está negro / no hay señal

1. Verifica que `smartsdr-iqtransfer` está corriendo y muestra `RADIO_MSG Forwarding data to 127.0.0.1:5901`
2. Comprueba que el canal DAX-IQ está asignado al panadaptador en SmartSDR
3. Verifica con `netstat -ano | findstr ":5901"` que hay una línea `UDP 0.0.0.0:5901` (el plugin escuchando)

### Audio entrecortado

Causa más probable: desajuste entre el `--RATE` de smartsdr-iqtransfer y lo que SkyRoof solicita.

1. Consulta el log: `type "C:\Users\Public\SoapyFlexRadio_debug.txt"`
2. Busca la línea `setSampleRate: SkyRoof pidio=XXXXXX`
3. Ajusta `set RATE=XXXXXX` en `start_skyroof_flex6600.bat` para que coincida

### La frecuencia no sigue el Doppler

1. Verifica que SmartSDR CAT está corriendo
2. Comprueba con `netstat -ano | findstr ":60001"` que hay una línea `ESTABLISHED`
3. En SkyRoof → Settings → CAT Control → RX CAT → Enabled debe ser `True`
4. El Host debe ser `127.0.0.1` (no `localhost`)
5. Consulta el log y busca líneas `setFrequency: enviado CAT FA...`

### El waterfall está centrado incorrectamente

El plugin mueve el panadaptador via SmartSDR API TCP 4992. Si no funciona:
1. Verifica en el log líneas `connectSDR: conectado a SmartSDR`
2. Comprueba que el parámetro `host` tiene la IP correcta del radio

### El puerto 5901 aparece como "Busy" en SmartSDR DAX

Esto es normal y correcto. `smartsdr-iqtransfer` ya tiene el canal DAX-IQ ocupado. No es necesario activarlo manualmente en SmartSDR DAX.

---

## 11. Preguntas frecuentes

**¿Es compatible con otros modelos FlexRadio (6300, 6400, 6700)?**
Probablemente sí, ya que todos usan el protocolo SmartSDR. Sin embargo, solo ha sido probado con el FLEX-6600.

**¿Funciona sin transverter, solo con HF?**
Sí. Configura SmartSDR con un slice HF y ajusta `smartsdr-iqtransfer` al canal DAX-IQ correspondiente. En SkyRoof selecciona satélites HF o úsalo para monitoreo HF general.

**¿Puedo usar varios canales DAX-IQ simultáneamente?**
No directamente con este plugin — cada instancia del plugin usa un canal. Podrías lanzar varias instancias con diferentes `udp_port` y `DAX_CH`, pero SkyRoof solo usa un dispositivo SDR a la vez.

**¿Es compatible con AetherSDR?**
AetherSDR y este plugin pueden coexistir con el mismo radio siempre que usen slices y canales DAX-IQ diferentes.

**¿Por qué se necesita smartsdr-iqtransfer? ¿No podría el plugin hablar directamente con el radio?**
La primera versión del plugin intentaba hablar directamente con SmartSDR, pero el protocolo VITA-49 tiene complejidades (discovery, handshake, gestión de streams) que smartsdr-iqtransfer ya resuelve correctamente y con código probado en producción. Usar smartsdr-iqtransfer como puente es más fiable.

**¿Dónde está el archivo de log de debug?**
En `C:\Users\Public\SoapyFlexRadio_debug.txt`. Se crea automáticamente cuando SkyRoof carga el plugin.

---

## 12. Créditos y licencias

| Componente | Autor | Licencia |
|------------|-------|----------|
| SoapyFlexRadio plugin | EA5WA + Claude (Anthropic) | MIT |
| smartsdr-iqtransfer / flexlib-go | HB9FXQ (Frank Werner-Krippendorf) | MIT |
| SkyRoof | VE3NEA (Alex Shovkoplyas) | Propietario |
| SmartSDR / SmartSDR CAT | FlexRadio Systems | Propietario |
| SoapySDR | PothosWare / pothosware | LGPL-2.1 |

### Licencia MIT (SoapyFlexRadio)

```
Copyright (c) 2026 EA5WA

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
```

---

*Wiki generada el 29 de mayo de 2026 — SoapyFlexRadio v2.0*
