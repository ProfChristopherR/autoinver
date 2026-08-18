# 📡 DIAGRAMA DE CONEXIONES - AUTOINVER NODO
## Guía paso a paso para ensamblar el hardware

---

## 🗺️ MAPA GENERAL DE CONEXIONES

```
                    ┌──────────────────────────────────────────────────────────────┐
                    │           BATTERY PACK 3S Li-ion (11.1V nominal)             │
                    │              🔋 🔋 🔋                                         │
                    │            (12.6V carga completa)                            │
                    └─────────────┬────────────────────────────────────────────────┘
                                  │
                                  │ 11.1V directo (para bomba 12V)
                                  ▼
                    ┌─────────────────────┐        ┌─────────────────────┐
                    │   BUCK-BOOST 5V/3A  │◄───────│    BOMBA 12V        │
                    │    Entrada: 7-24V   │        │    (Riego)          │
                    │    Salida:  5V/3A   │        │                     │
                    └──────┬──────────────┘        └─────────────────────┘
                           │
                           │ 5V VIN
                           ▼
    ┌────────────────────────────────────────────────────────────────────────────┐
    │                        HELTEC LoRa32 V2 (NODO)                              │
    │  ┌───────────────────────────────────────────────────────────────────────┐   │
    │  │ 5V VIN ────┐                                                        │   │
    │  │ GND ───────┼── GND común                                            │   │
    │  │ 3.3V OUT ──┼──► DHT22 VCC                                           │   │
    │  │      │     └──► Hum Suelo VCC                                       │   │
    │  │      │                                                          │   │
    │  │ GPIO25 ◄─────── DHT22 DATA  (Temp/Hum)                           │   │
    │  │ GPIO33 ◄─────── Hum Suelo SIG (ADC capacitivo)                   │   │
    │  │ GPIO32 ◄─────── Sharp IR Vo  (ADC nivel agua)                    │   │
    │  │ GPIO12 ───────► Relé IN    (Bomba ON/OFF)                        │   │
    │  │                                                                  │   │
    │  │ GPIO4  ────┬──► OLED SDA  (interno)                              │   │
    │  │            └──► PCA9685 SDA (I2C, bus compartido)                │   │
    │  │ GPIO15 ────┬──► OLED SCL  (interno)                              │   │
    │  │            └──► PCA9685 SCL (I2C, bus compartido)                │   │
    │  │ GPIO16 ───────► OLED RST  (interno)                              │   │
    │  │                                                                  │   │
    │  │ ⚠️ GPIO21 NO usar: controla Vext en Heltec LoRa32 V2             │   │
    │  │                                                                  │   │
    │  │ GPIO5  ── LoRa SCK   (interno, NO TOCAR)                         │   │
    │  │ GPIO19 ── LoRa MISO  (interno, NO TOCAR)                         │   │
    │  │ GPIO27 ── LoRa MOSI  (interno, NO TOCAR)                         │   │
    │  │ GPIO18 ── LoRa CS    (interno, NO TOCAR)                         │   │
    │  │ GPIO14 ── LoRa RST   (interno, NO TOCAR)                         │   │
    │  │ GPIO26 ── LoRa IRQ   (interno, NO TOCAR)                         │   │
    │  │                                                                  │   │
    │  │ ◄── Antena LoRa 915MHz (conector U.FL / SMA)                     │   │
    │  │ ◄── Antena WiFi 2.4GHz (en board, no conectar nada)              │   │
    │  └───────────────────────────────────────────────────────────────────────┘   │
    └────────────────────────────────────────────────────────────────────────────┘
                                  │
                                  │ 5V VIN
                                  ▼
                    ┌─────────────────────────────────────┐
                    │         PCA9685 (I2C 0x40)           │
                    │  VCC  ───► 3.3V (lógica I2C)         │
                    │  V+   ───► 5V (alimentación servos)  │
                    │  GND  ───► GND común                  │
                    │  SDA  ───► GPIO4 (compartido con OLED)  │
                    │  SCL  ───► GPIO15 (compartido con OLED) │
                    │                                      │
                    │  PWM0 ───► Servo A (Compuerta A)     │
                    │  PWM1 ───► Servo B (Compuerta B)     │
                    └─────────────────────────────────────┘
```

---

## 📋 TABLA DETALLADA DE CONEXIONES POR COMPONENTE

### 1️⃣ BATTERY PACK 3S Li-ion → Buck-Boost
| Desde | Hacia | Cable | Nota |
|-------|-------|-------|------|
| Batería 3S (+) | Buck-Boost IN+ | Rojo 16AWG | Principal alimentación |
| Batería 3S (-) | Buck-Boost IN- | Negro 16AWG | GND común |
| Batería 3S (+) | Relé VCC (para bomba) | Rojo 18AWG | 11.1V directo a bomba |

### 2️⃣ Buck-Boost 5V/3A → Distribución 5V
| Desde | Hacia | Cable | Nota |
|-------|-------|-------|------|
| Buck-Boost OUT+ (5V) | Heltec VIN | Rojo 20AWG | Alimenta ESP32 |
| Buck-Boost OUT+ (5V) | PCA9685 V+ | Rojo 22AWG | Alimenta servos |
| Buck-Boost OUT+ (5V) | Relé VCC (módulo relé) | Rojo 22AWG | Lógica del relé |
| Buck-Boost OUT+ (5V) | Sharp 2Y0A21 VCC | Rojo 22AWG | Sensor IR |
| Buck-Boost OUT- (GND) | Bus GND común | Negro 20AWG | **Punto de tierra único** |

### 3️⃣ Heltec LoRa32 V2 → Sensores (3.3V)
| Pin Heltec | Pin Sensor | Función | Cable | Nota |
|------------|-----------|---------|-------|------|
| 3.3V OUT | DHT22 VCC | Alimentación | Naranja | 3.3V máximo |
| 3.3V OUT | Hum Suelo VCC | Alimentación | Naranja | Sensor capacitivo |
| GND | DHT22 GND | Tierra | Negro | Bus GND |
| GND | Hum Suelo GND | Tierra | Negro | Bus GND |
| **GPIO25** | DHT22 DATA | Datos | Amarillo | One-wire protocol |
| **GPIO33** | Hum Suelo SIG | ADC | Verde | Entrada analógica |
| **GPIO32** | Sharp IR Vo | ADC | Verde | Entrada analógica |

### 4️⃣ Heltec LoRa32 V2 → PCA9685 (I2C)
| Pin Heltec | Pin PCA9685 | Función | Cable | Nota |
|------------|-------------|---------|-------|------|
| **GPIO4** | SDA | I2C Data | Azul | Bus compartido con OLED |
| **GPIO15** | SCL | I2C Clock | Morado | Bus compartido con OLED |
| 3.3V OUT | VCC | Lógica I2C | Naranja | **Solo 3.3V para lógica** |
| GND | GND | Tierra | Negro | Bus GND |
| Buck-Boost 5V OUT | V+ | Fuerza servos | Rojo | **5V para motores servos** |

> ⚠️ **CRÍTICO**: PCA9685 necesita **DOS** alimentaciones:
> - **VCC = 3.3V** (lógica I2C, compatible con ESP32)
> - **V+ = 5V** (fuerza para servos, puede ser hasta 6V)

### 5️⃣ PCA9685 → Servos
| Pin PCA9685 | Pin Servo | Función | Cable | Nota |
|-------------|-----------|---------|-------|------|
| PWM0 | Servo A (naranja/blanco) | Señal | Amarillo | Compuerta A |
| PWM1 | Servo B (naranja/blanco) | Señal | Amarillo | Compuerta B |
| V+ (terminal) | Servo A Rojo | VCC 5V | Rojo | Del PCA9685 V+ |
| V+ (terminal) | Servo B Rojo | VCC 5V | Rojo | Del PCA9685 V+ |
| GND (terminal) | Servo A Marrón/Negro | GND | Negro | Bus GND |
| GND (terminal) | Servo B Marrón/Negro | GND | Negro | Bus GND |

### 6️⃣ Heltec LoRa32 V2 → Relé Bomba
| Pin Heltec | Pin Relé | Función | Cable | Nota |
|------------|----------|---------|-------|------|
| **GPIO12** | Relé IN (SIGNAL) | Control ON/OFF | Blanco | HIGH = activa |
| Buck-Boost 5V | Relé VCC | Alimentación relé | Rojo | Lógica del módulo |
| GND | Relé GND | Tierra | Negro | Bus GND |
| Relé COM | Bomba 12V (+) | Interruptor | Rojo grueso | Desde batería 3S |
| Relé NO (Normally Open) | Bomba 12V (-) | Interruptor | Negro grueso | A bomba |

> ⚠️ **IMPORTANTE**: El relé actúa como interruptor en la línea positiva o negativa de la bomba. La bomba consume CORRIENTE ALTA - usa cables gruesos (18AWG mínimo) para la línea de la bomba.

### 7️⃣ Sharp 2Y0A21 (Sensor IR distancia/nivel)
| Pin Sharp | Conexión | Función | Cable | Nota |
|-----------|----------|---------|-------|------|
| VCC (rojo) | Buck-Boost 5V | Alimentación | Rojo | **5V requerido** |
| GND (negro) | GND común | Tierra | Negro | Bus GND |
| Vo (amarillo) | **GPIO32** | Salida analógica | Verde | **0-3.1V (seguro para ESP32)** |

> ✅ El Sharp 2Y0A21 alimentado a 5V tiene Vo máximo ~3.1V, por lo que **ES SEGURO** conectar directamente al ADC del ESP32 (que soporta hasta 3.3V).

---

## 🎨 COLORES SUGERIDOS DE CABLES (Código de colores)

Para evitar confusiones al armar, usa este código:

| Color | Uso |
|-------|-----|
| 🔴 Rojo | VCC positivo (+5V, +11.1V, +3.3V) |
| ⚫ Negro | GND / Tierra |
| 🟡 Amarillo | Señales de datos digitales (DHT22, servos, relé) |
| 🟢 Verde | Señales analógicas ADC (humedad suelo, Sharp IR) |
| 🔵 Azul | I2C SDA |
| 🟣 Morado | I2C SCL |
| 🟠 Naranja | Alimentación 3.3V a sensores |

---

## 🔌 CONEXIONES DEL GATEWAY (Heltec LoRa32 V2 - Estación Base)

El gateway es más simple - solo necesita:

| Componente | Pin Heltec | Función | Nota |
|------------|-----------|---------|------|
| WiFi | Interno | Conexión a router | Configurar SSID/pass en código |
| LoRa | GPIO5/14/18/19/26/27 | Radio 915MHz | Pines internos, no tocar |
| OLED | GPIO4/15/16 | Display interno | Pines internos, no tocar |
| Alimentación | 5V VIN | Desde USB o fuente 5V | Puede usar cable USB directo |

---

## ⚡ ESQUEMA DE ALIMENTACIÓN COMPLETO

```
                    ┌─────────────────┐
                    │  BATTERY 3S     │
                    │  11.1V nominal  │
                    │  (12.6V max)    │
                    └────────┬────────┘
                             │
            ┌────────────────┼────────────────┐
            │                │                │
            ▼                ▼                ▼
    ┌──────────────┐  ┌──────────────┐  ┌──────────────┐
    │ Buck-Boost   │  │ Relé COM     │  │ (futuro)    │
    │ 5V / 3A      │  │ (interruptor)│  │             │
    └──────┬───────┘  └──────┬───────┘  └──────────────┘
           │                 │
    ┌──────┼──────┐          │
    │      │      │          │
    ▼      ▼      ▼          ▼
 ┌────┐ ┌────┐ ┌────┐   ┌─────────┐
 │Heltec│ │PCA │ │Sharp│   │  BOMBA  │
 │VIN  │ │V+  │ │VCC  │   │  12V    │
 │5V   │ │5V  │ │5V   │   │         │
 └────┘ └────┘ └────┘   └─────────┘
    │      │      │
    ▼      ▼      ▼
 ┌─────────────────────────┐
 │      BUS GND COMÚN      │
 │  (todos los GND unidos) │
 └─────────────────────────┘
```

---

## 🧪 CHECKLIST DE VERIFICACIÓN ANTES DE ENCENDER

Antes de conectar la batería o encender, verifica:

- [ ] **NO hay cortocircuitos** entre pines adyacentes
- [ ] **GND es común** en TODOS los componentes (multímetro continuidad)
- [ ] **PCA9685 VCC = 3.3V** (NO 5V, puede dañar el I2C del ESP32)
- [ ] **PCA9685 V+ = 5V** (para fuerza de servos)
- [ ] **DHT22 está a 3.3V** (NO 5V, puede dañarse)
- [ ] **Sharp IR está a 5V** (requerido para funcionar correctamente)
- [ ] **GPIO32 solo recibe Vo del Sharp** (no conectar VCC aquí)
- [ ] **Relé bomba usa cables gruesos** para la línea de la bomba
- [ ] **Antena LoRa conectada** en ambos dispositivos (sin antena puede dañar el SX1276)
- [ ] **Polaridad correcta** en todos los conectores (especialmente batería 3S)

---

## 🚨 ERRORES FATALES COMUNES QUE EVITAR

| ❌ Error | 💀 Consecuencia | ✅ Solución |
|----------|----------------|-------------|
| PCA9685 VCC a 5V | **Quemar I2C del ESP32** | VCC = 3.3V SIEMPRE |
| DHT22 a 5V | Sensor puede dañarse | DHT22 a 3.3V |
| No conectar antena LoRa | **Quemar módulo SX1276** | Conectar antena antes de energizar |
| GPIO21 usado como SDA | **No funciona: GPIO21 controla Vext en Heltec V2** | PCA9685 en GPIO4/15 (bus compartido con OLED) |
| Servos sin V+ en PCA9685 | Servos no responden | V+ del PCA9685 a 5V |
| GND no común | Comportamiento errático | Unir TODOS los GND |
| Bomba sin diodo flyback | Picos de voltaje dañan relé | Usar relé con optoacoplador (ya incluido en módulo) |
| Sharp a 3.3V | Lecturas erráticas o nulas | Sharp a 5V (Vo sigue siendo <3.3V) |

---

## 📸 FOTO DE REFERENCIA - Vista física sugerida

```
    ┌─────────────────────────────────────────────────────────────┐
    │  VISTA SUPERIOR - NODO AUTOINVER (aproximación física)      │
    │                                                             │
    │   ┌──────────┐                                              │
    │   │ Heltec   │◄── Antena LoRa (915MHz)                     │
    │   │ LoRa32   │◄── Antena WiFi (integrada, no tocar)        │
    │   │  V2      │                                              │
    │   └────┬─────┘                                              │
    │        │GPIO25────┐                                         │
    │        │GPIO33────┼──┐                                      │
    │        │GPIO32────┼──┼──┐                                   │
    │        │GPIO12────┼──┼──┼──┐                                │
    │        │GPIO21────┼──┼──┼──┼──┐                             │
    │        │GPIO22────┼──┼──┼──┼──┼──┐                          │
    │        └───────────┘  │  │  │  │  │                          │
    │                       │  │  │  │  │                          │
    │   ┌──────────┐   ┌────┘  │  │  │  │                          │
    │   │   DHT22  │   │  ┌────┘  │  │  │                          │
    │   └──────────┘   │  │  ┌────┘  │  │                          │
    │                  │  │  │  ┌────┘  │                          │
    │   ┌──────────┐   │  │  │  │  ┌────┘                          │
    │   │ Hum Suelo│   │  │  │  │  │                               │
    │   └──────────┘   │  │  │  │  │                               │
    │                  │  │  │  │  │                               │
    │   ┌──────────┐   │  │  │  │  │                               │
    │   │Sharp IR  │◄──┘  │  │  │  │                               │
    │   └──────────┘      │  │  │  │                               │
    │                     │  │  │  │                               │
    │   ┌──────────┐      │  │  │  │                               │
    │   │  Relé 5V │◄─────┘  │  │  │                               │
    │   │  (Bomba) │◄────────┘  │  │                               │
    │   └────┬─────┘            │  │                               │
    │        │ 11.1V            │  │                               │
    │        ▼                  │  │                               │
    │   ┌──────────┐            │  │                               │
    │   │  BOMBA   │            │  │                               │
    │   │   12V    │            │  │                               │
    │   └──────────┘            │  │                               │
    │                           │  │                               │
    │   ┌──────────┐            │  │                               │
    │   │ PCA9685  │◄───────────┘  │                               │
    │   │ 0x40     │◄──────────────┘                               │
    │   └────┬─────┘                                               │
    │        │PWM0──► Servo A                                      │
    │        │PWM1──► Servo B                                      │
    │        │V+  ──► 5V servos                                   │
    │        └────────┐                                            │
    │                 │                                            │
    │   ┌─────────────┴────────────────────────┐                   │
    │   │         BUCK-BOOST 5V/3A             │                   │
    │   │  Entrada: 11.1V (batería 3S)         │                   │
    │   │  Salida:  5V  (distribución)         │                   │
    │   └──────────────────────────────────────┘                   │
    │                 ▲                                            │
    │                 │                                            │
    │   ┌─────────────┘                                            │
    │   │  BATTERY 3S Li-ion                                       │
    │   │  11.1V nominal                                           │
    │   └──────────────────────────────────────────────────────────┘
    └─────────────────────────────────────────────────────────────┘
```

---

## 🔧 PASOS DE ENSAMBLAJE RECOMENDADOS

### Paso 1: Preparar la alimentación
1. Conectar la batería 3S al buck-boost
2. Medir con multímetro: salida debe ser **5.0V ±0.2V**
3. No conectar nada más todavía

### Paso 2: Alimentar el Heltec (sin sensores)
1. Conectar VIN del Heltec al buck-boost 5V
2. Conectar GND
3. Verificar que el Heltec enciende (OLED muestra algo)
4. Subir el sketch del nodo

### Paso 3: Conectar sensores uno por uno
1. **DHT22 primero**: VCC→3.3V, GND→GND, DATA→GPIO25
2. Verificar en Serial Monitor que lee temperatura/humedad
3. **Hum Suelo**: VCC→3.3V, GND→GND, SIG→GPIO33
4. Verificar lectura ADC
5. **Sharp IR**: VCC→5V, GND→GND, Vo→GPIO32
6. Verificar lectura ADC

### Paso 4: Conectar PCA9685 y servos
1. PCA9685: VCC→3.3V, V+→5V, GND→GND, SDA→GPIO4, SCL→GPIO15 (bus compartido con OLED)
2. Verificar con I2C scanner que aparece en 0x40 (el firmware lo imprime al arrancar)
3. Conectar servos (sin mecánica todavía)
4. Verificar que se mueven al encender

### Paso 5: Conectar relé y bomba
1. Relé: VCC→5V, GND→GND, IN→GPIO12
2. **NO conectar la bomba todavía**
3. Probar relé con multímetro en modo continuidad (escuchar "click")
4. Si funciona, conectar bomba 12V a través del relé

### Paso 6: Prueba LoRa
1. Encender gateway primero
2. Encender nodo
3. Verificar que el nodo recibe SYNC:HORA
4. Verificar que el gateway recibe telemetría

---

*Documento generado para el Proyecto Autoinver v2.0*
