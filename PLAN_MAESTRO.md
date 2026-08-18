# 📋 PLAN MAESTRO DE IMPLEMENTACIÓN - PROYECTO AUTOINVER

## 📌 Visión del Proyecto
Sistema de automatización agrícola móvil para invernadero, con control autónomo inteligente, telemetría LoRa de largo alcance, dashboard web en tiempo real con diseño profesional, y operación offline resiliente.

---

## 🏗️ Arquitectura Mejorada del Sistema

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         SISTEMA AUTOINVER v2.0                               │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌──────────────────┐      LoRa 915MHz      ┌──────────────────┐           │
│  │  NODO AUTOINVER  │ ◄───────────────────► │  GATEWAY FIJO    │           │
│  │  (Invernadero)   │    Half-Duplex SF7   │  (Estación Base) │           │
│  │                  │    BW125, CR 4/5     │                  │           │
│  │ • DHT22          │                      │ • WiFi STA       │           │
│  │ • Hum. Suelo     │    Telemetría 5s     │ • NTP Chile      │           │
│  │ • Sharp IR Nivel │    Comandos async    │ • Web Server     │           │
│  │ • PCA9685 + 2x   │                      │ • Buffer SPIFFS  │           │
│  │   Servos         │                      │ • Supabase API   │           │
│  │ • Relé Bomba     │                      │ • LoRa GW        │           │
│  │ • RTC Local      │                      │ • RTC NTP-sync   │           │
│  │ • FSM Autónoma   │                      │ • Command Queue  │           │
│  │ • Watchdog       │                      │ • Health Monitor │           │
│  └──────────────────┘                      └────────┬─────────┘           │
│                                                     │                      │
│                                                     │ HTTPS REST API       │
│                                                     ▼                      │
│                                          ┌──────────────────┐             │
│                                          │   SUPABASE       │             │
│                                          │   PostgreSQL     │             │
│                                          │   • autoinver_data│            │
│                                          │   • autoinver_events│          │
│                                          │   • commands_queue │           │
│                                          │   • device_status  │           │
│                                          └────────┬─────────┘             │
│                                                   │                        │
│                                                   │ REST API / Realtime    │
│                                                   ▼                        │
│                                          ┌──────────────────┐             │
│                                          │  DASHBOARD WEB   │             │
│                                          │  (GitHub Pages)  │             │
│                                          │                  │             │
│                                          │ • React/Vite     │             │
│                                          │ • Tailwind CSS   │             │
│                                          │ • Chart.js       │             │
│                                          │ • Realtime WS    │             │
│                                          │ • PWA Offline    │             │
│                                          │ • Dark/Light UI  │             │
│                                          └──────────────────┘             │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 📊 Fases de Implementación

### 🔷 FASE 0: Documentación y Arquitectura (COMPLETADO)
✅ Lectura completa de documentación existente
✅ Comprensión del MVP funcional actual
✅ Identificación de mejoras críticas

### 🔷 FASE 1: Firmware Nodo Autoinver (Heltec LoRa32 V2)
**Objetivo**: Código robusto, con máquina de estados, watchdog, calibración persistente

| Sub-tarea | Descripción | Prioridad |
|-----------|-------------|-----------|
| 1.1 | Mejorar estructura con FSM (Finite State Machine) para modos AUTO/MANUAL/SLEEP | Alta |
| 1.2 | Implementar EEPROM/Preferences para guardar calibración servos y umbrales | Alta |
| 1.3 | Añadir watchdog timer y recovery ante fallos | Alta |
| 1.4 | Implementar protocolo ACK con retransmisión para comandos | Media |
| 1.5 | Añadir smoothing/filtering en lecturas analógicas (media móvil) | Media |
| 1.6 | Implementar modo SLEEP de bajo consumo entre ciclos | Baja |
| 1.7 | OLED con íconos/gráficos mejorados (barras de nivel) | Media |

**Entregable**: `Autoinver_Nodo/Autoinver_Nodo.ino` + `config.h`

### 🔷 FASE 2: Firmware Gateway Estación (Heltec LoRa32 V2)
**Objetivo**: Bridge robusto, buffer offline, servidor web profesional, health monitoring

| Sub-tarea | Descripción | Prioridad |
|-----------|-------------|-----------|
| 2.1 | Implementar HTTPS POST completo a Supabase REST API | Alta |
| 2.2 | Mejorar buffer SPIFFS con rotación y compresión JSONL | Alta |
| 2.3 | Implementar polling de comandos pendientes desde Supabase | Alta |
| 2.4 | Servidor web con API REST JSON completa (/api/v1/*) | Alta |
| 2.5 | Dashboard web embebido con diseño moderno (HTML/CSS/JS en PROGMEM) | Media |
| 2.6 | Health monitoring: uptime, memoria libre, paquetes perdidos | Media |
| 2.7 | Reconexión WiFi automática con backoff exponencial | Alta |
| 2.8 | NTP sync con múltiples servidores de fallback | Media |

**Entregable**: `Autoinver_Gateway/Autoinver_Gateway.ino` + `webui.h` + `supabase.h`

### 🔷 FASE 3: Backend Supabase
**Objetivo**: Base de datos relacional con RLS, triggers, funciones edge

| Sub-tarea | Descripción | Prioridad |
|-----------|-------------|-----------|
| 3.1 | DDL completo: tablas, índices, claves foráneas | Alta |
| 3.2 | RLS policies seguras pero funcionales | Alta |
| 3.3 | Triggers para auto-generar eventos en cambios de estado | Media |
| 3.4 | Función RPC para insertar batch (buffer gateway) | Media |
| 3.5 | Vista materializada para dashboard (últimos datos por device) | Media |
| 3.6 | Configurar realtime en tabla commands_queue | Alta |

**Entregable**: `supabase/schema.sql` + `supabase/seed.sql` + `supabase/edge-functions/`

### 🔷 FASE 4: Frontend Dashboard Web
**Objetivo**: Interfaz profesional, responsiva, en tiempo real, PWA

| Sub-tarea | Descripción | Prioridad |
|-----------|-------------|-----------|
| 4.1 | Setup React + Vite + TypeScript | Alta |
| 4.2 | Diseño UI/UX con Tailwind CSS + componentes shadcn/ui | Alta |
| 4.3 | Dashboard principal: cards en vivo + gauge charts | Alta |
| 4.4 | Gráficos históricos con Chart.js / Recharts | Alta |
| 4.5 | Mapa de estado del sistema (topología visual) | Media |
| 4.6 | Panel de control manual (bomba, servos, modo) | Alta |
| 4.7 | Tabla de eventos históricos con filtros | Media |
| 4.8 | Conexión Supabase Realtime para updates en vivo | Alta |
| 4.9 | Modo oscuro/claro + responsive mobile | Media |
| 4.10 | PWA: service worker, offline cache, manifest | Baja |

**Entregable**: `dashboard/` (repo completo React)

### 🔷 FASE 5: Integración y Validación
**Objetivo**: Pruebas end-to-end, calibración, documentación de operación

| Sub-tarea | Descripción |
|-----------|-------------|
| 5.1 | Flujo de datos completo: Nodo → LoRa → Gateway → Supabase → Dashboard |
| 5.2 | Prueba de buffer offline: desconectar WiFi, reconectar, verificar sync |
| 5.3 | Prueba de comandos: Dashboard → Supabase → Gateway → LoRa → Nodo |
| 5.4 | Calibración física: servos, Sharp IR, humedad suelo |
| 5.5 | Pruebas de estrés: pérdida de paquetes, interferencia, alcance |
| 5.6 | Documentación de operación y troubleshooting |

---

## 🎨 Guía de Diseño UI/UX (Dashboard)

### Paleta de Colores
```
Primary:    #10B981 (Emerald 500)  - Verde agricultura/tecnología
Secondary:  #3B82F6 (Blue 500)     - Agua/tecnología
Accent:     #F59E0B (Amber 500)    - Alertas/energía
Danger:     #EF4444 (Red 500)      - Errores/crítico
Dark:       #0F172A (Slate 900)    - Fondo oscuro
Light:      #F8FAFC (Slate 50)     - Fondo claro
```

### Tipografía
- **Display**: Inter (Google Fonts)
- **Monospace**: JetBrains Mono (datos técnicos)

### Layout
- Sidebar colapsable con navegación
- Header con estado de conexión, hora, notificaciones
- Grid de cards responsive (1 col móvil, 2 col tablet, 3-4 col desktop)
- Gráficos con lazy loading

---

## 🔧 Mejoras Técnicas Clave vs MVP Original

| Aspecto | MVP Original | Versión Mejorada |
|---------|-------------|------------------|
| Protocolo ACK | No tiene | ACK con timeout y retransmisión |
| Buffer offline | JSON simple | JSONL con rotación y batch insert |
| Web local | HTML embebido simple | SPA profesional embebida |
| Dashboard | GitHub Pages estático | React + Realtime + PWA |
| Control remoto | Polling 30s | Supabase Realtime (sub-second) |
| Calibración | Hardcoded | EEPROM/Preferences persistente |
| Estado bomba | ON/OFF simple | FSM con modos AUTO/MANUAL/SCHEDULE |
| Gráficos | Chart.js simple | Recharts con zoom, brush, export |
| Seguridad | RLS básica | RLS + API key rotation |
| Monitoreo | Ninguno | Health checks, métricas, logs |

---

## 📁 Estructura de Archivos del Proyecto

```
Autoinver/
├── 📁 firmware/
│   ├── 📁 Autoinver_Nodo/
│   │   ├── Autoinver_Nodo.ino      # Código principal
│   │   ├── config.h                # Constantes y pines
│   │   ├── sensors.h/.cpp          # Módulo sensores
│   │   ├── actuators.h/.cpp        # Módulo actuadores
│   │   ├── lora_comms.h/.cpp       # Protocolo LoRa
│   │   └── display_ui.h/.cpp       # Interfaz OLED
│   └── 📁 Autoinver_Gateway/
│       ├── Autoinver_Gateway.ino   # Código principal
│       ├── config.h                # WiFi, Supabase creds
│       ├── web_server.h/.cpp       # Servidor web + API
│       ├── supabase_client.h/.cpp  # Cliente HTTPS
│       ├── lora_bridge.h/.cpp      # Bridge LoRa ↔ WiFi
│       └── spiffs_buffer.h/.cpp    # Gestión buffer offline
│
├── 📁 backend/
│   ├── schema.sql                  # DDL completo
│   ├── seed.sql                    # Datos de prueba
│   ├── policies.sql                # RLS policies
│   └── edge-functions/
│       └── process-batch.ts        # Batch insert
│
├── 📁 dashboard/
│   ├── src/
│   │   ├── components/             # Componentes React
│   │   ├── pages/                  # Páginas
│   │   ├── hooks/                  # Custom hooks (Supabase)
│   │   ├── lib/                    # Utilidades
│   │   └── types/                  # Tipos TypeScript
│   ├── public/
│   ├── index.html
│   ├── package.json
│   ├── vite.config.ts
│   └── tailwind.config.js
│
└── 📁 docs/
    ├── plan-maestro.md             # Este documento
    ├── calibracion.md              # Guía de calibración
    ├── troubleshooting.md          # Guía de problemas
    └── diagramas/                  # Diagramas de arquitectura
```

---

## ⏱️ Estimación de Tiempo

| Fase | Estimación | Dependencias |
|------|-----------|--------------|
| Fase 1: Nodo | 3-4 horas | Ninguna |
| Fase 2: Gateway | 4-5 horas | Ninguna |
| Fase 3: Backend | 1-2 horas | Ninguna |
| Fase 4: Dashboard | 6-8 horas | Fase 3 |
| Fase 5: Integración | 2-3 horas | Fases 1-4 |
| **TOTAL** | **16-22 horas** | |

---

## 🎯 Criterios de Éxito

1. ✅ Nodo opera autónomamente 24/7 sin intervención
2. ✅ Gateway mantiene buffer offline >48h sin pérdida de datos
3. ✅ Dashboard carga en <2s, actualiza en tiempo real
4. ✅ Comando manual: <3s desde click hasta actuador
5. ✅ Alcance LoRa: >500m línea de vista
6. ✅ Consumo nodo: <200mA promedio (con sleep parcial)

---

*Documento generado por el Agente Implementador - Proyecto Autoinver v2.0*
