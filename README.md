# 🌿 AUTOINVER v2.0 - Sistema de Invernadero Automatizado IoT

Sistema completo de automatización agrícola con control autónomo inteligente, telemetría LoRa de largo alcance, dashboard web profesional en tiempo real y operación offline resiliente.

---

## 📋 Tabla de Contenidos

- [Arquitectura](#-arquitectura)
- [Hardware](#-hardware)
- [Firmware](#-firmware)
- [Backend](#-backend)
- [Dashboard Web](#-dashboard-web)
- [Instalación Rápida](#-instalación-rápida)
- [Calibración](#-calibración)
- [Troubleshooting](#-troubleshooting)

---

## 🏗️ Arquitectura

```
┌──────────────────┐      LoRa 915MHz      ┌──────────────────┐
│  NODO AUTOINVER  │ ◄───────────────────► │  GATEWAY FIJO    │
│  (Invernadero)   │    Half-Duplex SF7   │  (Estación Base) │
│                  │    BW125, CR 4/5     │                  │
│ • DHT22          │                      │ • WiFi STA       │
│ • Hum. Suelo     │    Telemetría 5s     │ • NTP Chile      │
│ • Sharp IR Nivel │    Comandos async    │ • Web Server     │
│ • PCA9685 + 2x   │                      │ • Buffer SPIFFS  │
│   Servos         │                      │ • Supabase API   │
│ • Relé Bomba     │                      │ • LoRa GW        │
│ • RTC Local      │                      │ • RTC NTP-sync   │
│ • FSM Autónoma   │                      │ • Command Queue  │
│ • Watchdog       │                      │ • Health Monitor │
└──────────────────┘                      └────────┬─────────┘
                                                   │ HTTPS
                                          ┌────────┴─────────┐
                                          │    SUPABASE      │
                                          │   PostgreSQL     │
                                          └────────┬─────────┘
                                                   │ REST/WS
                                          ┌────────┴─────────┐
                                          │  DASHBOARD WEB   │
                                          │  (GitHub Pages)  │
                                          └──────────────────┘
```

---

## 🔌 Hardware

### Nodo Autoinver (Invernadero Móvil)
| Componente | Pin/Conexión | Detalle |
|------------|-------------|---------|
| **MCU** | Heltec LoRa32 V2 | ESP32 + SX1276 + OLED |
| DHT22 | GPIO25 | Temp/Hum ambiente |
| Hum. Suelo | GPIO33 | Capacitivo, ADC |
| Sharp 2Y0A21 | GPIO32 | Nivel agua, ADC |
| PCA9685 SDA | GPIO21 | I2C servos |
| PCA9685 SCL | GPIO22 | I2C servos |
| Relé Bomba | GPIO12 | Active HIGH |
| Servo A | PCA9685 Ch0 | Compuerta 0-90° |
| Servo B | PCA9685 Ch1 | Compuerta 0-90° |

### Gateway Estación (Base Fija)
| Componente | Detalle |
|------------|---------|
| **MCU** | Heltec LoRa32 V2 |
| WiFi | STA mode, conecta a router |
| OLED | Estado del sistema |
| SPIFFS | Buffer offline |

### Alimentación
- **Batería 3S Li-ion**: 11.1V nominal
- **Buck-Boost 5V/3A**: Alimenta Heltec, PCA9685 V+, Relé, Sharp
- **Heltec 3.3V OUT**: Alimenta DHT22, Hum. Suelo

---

## 💻 Firmware

### Librerías Arduino IDE Requeridas

**Nodo:**
- `DHT sensor library` by Adafruit (1.4.6)
- `Adafruit Unified Sensor` (1.1.14)
- `LoRa` by Sandeep Mistry (0.8.0)
- `Adafruit SSD1306` (2.5.9)
- `Adafruit PWM Servo Driver Library` (PCA9685)
- `ESP32Time` by F.Bisinger

**Gateway:**
- Todas las del nodo, más:
- `NTPClient` by Fabrice Weinberg
- `ArduinoJson` by Benoit Blanchon

### Instalación

1. **Nodo:**
   ```
   Abrir firmware/Autoinver_Nodo/Autoinver_Nodo.ino
   Seleccionar board: "Heltec WiFi LoRa 32(V2)"
   Compilar y subir
   ```

2. **Gateway:**
   ```
   Abrir firmware/Autoinver_Gateway/Autoinver_Gateway.ino
   Editar: WIFI_SSID, WIFI_PASS, SUPABASE_URL, SUPABASE_KEY
   Seleccionar board: "Heltec WiFi LoRa 32(V2)"
   Compilar y subir
   ```

---

## 🗄️ Backend (Supabase)

### Setup

1. Crear proyecto en [supabase.com](https://supabase.com)
2. Ir a SQL Editor
3. Copiar y ejecutar el contenido de `backend/schema.sql`
4. Copiar URL y Anon Key del proyecto
5. Pegar en el código del gateway

### Tablas creadas:
- `autoinver_data` - Telemetría histórica
- `autoinver_events` - Eventos de cambio de estado
- `commands_queue` - Cola de comandos pendientes
- `device_config` - Configuración por dispositivo

---

## 🌐 Dashboard Web

### Opción A: HTML Vanilla (Rápido)
1. Abrir `dashboard/index.html`
2. Editar `SUPABASE_URL` y `SUPABASE_KEY` al inicio del `<script>`
3. Subir a GitHub Pages, Netlify, o Vercel

### Opción B: React + Vite (Avanzado)
```bash
cd dashboard
npm install
npm run dev
# Editar src/lib/supabase.ts con tus credenciales
npm run deploy  # Para GitHub Pages
```

### Características:
- ✅ Gráficos históricos interactivos (Chart.js)
- ✅ Actualización en tiempo real (Supabase Realtime)
- ✅ Control manual remoto (bomba, servos, modo)
- ✅ Timeline de eventos
- ✅ Diseño responsive con modo oscuro
- ✅ Fallback a gateway local

---

## ⚡ Instalación Rápida

### Paso 1: Hardware
1. Conectar sensores y actuadores según diagrama
2. Verificar alimentación 5V estable
3. Cargar batería 3S completamente

### Paso 2: Firmware Nodo
1. Instalar librerías en Arduino IDE
2. Cargar `Autoinver_Nodo.ino`
3. Verificar OLED muestra datos

### Paso 3: Firmware Gateway
1. Editar credenciales WiFi y Supabase
2. Cargar `Autoinver_Gateway.ino`
3. Verificar conexión WiFi y NTP

### Paso 4: Supabase
1. Ejecutar `backend/schema.sql`
2. Verificar tablas creadas
3. Habilitar Realtime en tablas necesarias

### Paso 5: Dashboard
1. Configurar credenciales Supabase
2. Desplegar en hosting estático
3. Probar control remoto

---

## 🔧 Calibración

Ver [docs/calibracion.md](docs/calibracion.md) para:
- Calibración sensor Sharp IR (nivel de agua)
- Calibración sensor humedad suelo
- Calibración servos PCA9685
- Ajuste de umbrales por tipo de cultivo
- Verificación del enlace LoRa

---

## 🔍 Troubleshooting

Ver [docs/troubleshooting.md](docs/troubleshooting.md) para:
- Problemas de hardware (OLED, LoRa, servos, bomba)
- Problemas de comunicación (WiFi, LoRa, cloud)
- Problemas de rendimiento (consumo, reinicios)
- Códigos de error comunes

---

## 📁 Estructura del Proyecto

```
Autoinver/
├── firmware/
│   ├── Autoinver_Nodo/         # Código del nodo invernadero
│   │   ├── Autoinver_Nodo.ino
│   │   └── config.h
│   └── Autoinver_Gateway/      # Código del gateway estación
│       └── Autoinver_Gateway.ino
├── backend/
│   └── schema.sql              # Schema completo Supabase
├── dashboard/
│   ├── index.html              # Dashboard vanilla (listo para usar)
│   ├── package.json            # Config React (opcional)
│   ├── vite.config.ts
│   └── tailwind.config.js
├── docs/
│   ├── calibracion.md          # Guía de calibración
│   └── troubleshooting.md      # Guía de problemas
├── PLAN_MAESTRO.md             # Planificación del proyecto
└── README.md                   # Este documento
```

---

## 🛡️ Especificaciones Técnicas

| Parámetro | Valor |
|-----------|-------|
| Frecuencia LoRa | 915 MHz |
| Spreading Factor | 7 (ajustable 7-12) |
| Bandwidth | 125 kHz |
| TX Power | 17 dBm |
| Intervalo TX | 5 segundos |
| Alcance LoRa | 500m - 2km LOS |
| Consumo nodo | ~150mA promedio |
| Duración batería 3S | ~24-48h (depende uso) |
| Buffer offline | >500 registros |

---

## 📄 Licencia

MIT License - Proyecto educativo de automatización agrícola.

---

**Autoinver v2.0** - Cultivando el futuro, un bit a la vez. 🌱
