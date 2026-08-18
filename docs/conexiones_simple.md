3# 🔌 PIN A PIN - Conexiones del Nodo Autoinver

## Lo único que necesitas: ¿qué cable va de dónde a dónde?
 nn
---

# 🔋 ALIMENTACIÓN

```
Batería 3S (11.1V)  ──►  Buck-Boost 5V/3A  ──►  reparte 5V a todo
```

| De | A | Para qué |
|----|---|----------|
| Batería 3S (+) | Buck-Boost IN+ | Entrada del conversor |
| Batería 3S (-) | Buck-Boost IN- | Tierra del conversor |
| Buck-Boost OUT+ (5V) | Heltec VIN pin | Alimentar el ESP32 |
| Buck-Boost OUT+ (5V) | PCA9685 pin V+ | Fuerza para los servos |
| Buck-Boost OUT+ (5V) | Relé pin VCC | Alimentar el módulo relé |
| Buck-Boost OUT+ (5V) | Sharp IR pin Rojo (VCC) | Alimentar sensor IR |
| Buck-Boost OUT- (GND) | **Todos los GND** | Tierra común de todo |
| Batería 3S (+) directo | Relé pin COM | 11.1V para la bomba |

> **NOTA**: El Heltec también saca 3.3V por un pin. Ese 3.3V se usa para los sensores.

---

# 🌡️ SENSOR DHT22 (Temperatura + Humedad)

```
┌─────────────┐
│   DHT22     │
│  ┌─┬─┬─┐   │
│  │+│D│G│   │  (+)=VCC  (D)=DATA  (G)=GND
│  └─┴─┴─┘   │
└─────────────┘
```

| Pin DHT22 | Conectar a | Color de cable |
|-----------|-----------|----------------|
| **VCC** (+) | Heltec pin **3.3V** | 🟠 Naranja |
| **DATA** (D) | Heltec pin **GPIO25** | 🟡 Amarillo |
| **GND** (G) | Heltec pin **GND** | ⚫ Negro |

---

# 🌱 SENSOR HUMEDAD DE SUELO (Capacitivo)

```
┌──────────────────┐
│  Humedad Suelo   │
│    ┌─┬─┬─┐      │
│    │V│S│G│      │  (V)=VCC  (S)=SIGNAL  (G)=GND
│    └─┴─┴─┘      │
└──────────────────┘
```

| Pin del sensor | Conectar a            | Color de cable |
| -------------- | --------------------- | -------------- |
| **VCC** (V)    | Heltec pin **3.3V**   | 🟠 Naranja     |
| **SIGNAL** (S) | Heltec pin **GPIO33** | 🟢 Verde       |
| **GND** (G)    | Heltec pin **GND**    | ⚫ Negro        |

---

# 💧 SENSOR NIVEL DE AGUA (Sharp 2Y0A21)

```
┌──────────────────┐
│   Sharp IR       │
│  ┌─┬─┬─┐        │
│  │R│B│Y│        │  (R)=VCC  (B)=GND  (Y)=Vo(salida)
│  └─┴─┴─┘        │
└──────────────────┘
```

| Pin Sharp | Conectar a | Color de cable |
|-----------|-----------|----------------|
| **Rojo** (VCC) | Buck-Boost **5V** | 🔴 Rojo |
| **Negro** (GND) | Heltec pin **GND** | ⚫ Negro |
| **Amarillo** (Vo) | Heltec pin **GPIO32** | 🟢 Verde |

> **IMPORTANTE**: El Sharp **necesita 5V** para funcionar. Su salida amarilla nunca pasa de 3.1V, así que **es segura** para el ESP32.

---

# ⚙️ DRIVER DE SERVOS (PCA9685)

```
┌─────────────────────────────────┐
│           PCA9685               │
│  ┌───┬───┬───┬───┬───┬───┐    │
│  │VCC│GND│SDA│SCL│V+ │GND│    │
│  └───┴───┴───┴───┴───┴───┘    │
│                                 │
│  PWM0 ──► Servo A              │
│  PWM1 ──► Servo B              │
└─────────────────────────────────┘
```

## Alimentación y control del PCA9685

| Pin PCA9685 | Conectar a | ¿Por qué? |
|-------------|-----------|-----------|
| **VCC** | Heltec pin **3.3V** | Lógica I2C (SIEMPRE 3.3V) |
| **GND** | Heltec pin **GND** | Tierra |
| **SDA** | Heltec pin **GPIO21** | Datos I2C |
| **SCL** | Heltec pin **GPIO22** | Reloj I2C |
| **V+** | Buck-Boost **5V** | Fuerza para motores servos |
| **GND** (terminal V+) | Heltec pin **GND** | Tierra de servos |

> ⚠️ **CRÍTICO**: PCA9685 tiene **DOS** entradas de corriente:
> - **VCC = 3.3V** → para la lógica I2C (nunca 5V o quemas el ESP32)
> - **V+ = 5V** → para mover los servos (puede ser 5V-6V)

## Servos al PCA9685

| Servo | Conectar a | Posición |
|-------|-----------|----------|
| **Servo A** (3 cables) | PCA9685 **PWM0** + V+ + GND | Compuerta A |
| **Servo B** (3 cables) | PCA9685 **PWM1** + V+ + GND | Compuerta B |

Cada servo tiene 3 cables:
- **Rojo** → V+ del PCA9685 (5V)
- **Marrón/Negro** → GND del PCA9685
- **Naranja/Blanco** → PWM0 o PWM1

---

# 🔌 RELÉ + BOMBA

```
┌──────────────────────────────┐
│  Módulo Relé 5V              │
│                              │
│  ┌───┬───┬───┐              │
│  │VCC│GND│ IN│   (lógica)   │
│  └───┴───┴───┘              │
│                              │
│  ┌───┬───┬───┐              │
│  │COM│ NO│ NC│   (potencia) │
│  └───┴───┴───┘              │
└──────────────────────────────┘
```

## Parte de lógica (control)

| Pin Relé | Conectar a | Función |
|----------|-----------|---------|
| **VCC** | Buck-Boost **5V** | Alimentar el módulo relé |
| **GND** | Heltec pin **GND** | Tierra |
| **IN** | Heltec pin **GPIO12** | Señal ON/OFF desde ESP32 |

## Parte de potencia (bomba)

| Pin Relé | Conectar a | Función |
|----------|-----------|---------|
| **COM** | Batería 3S **(+)** | Entrada 11.1V |
| **NO** (Normally Open) | Bomba 12V **(+)** | Salida a bomba cuando relé activa |
| Bomba 12V **(-)** | Batería 3S **(-)** | Retorno directo |

> **NO** conectar nada a NC (Normally Closed)
> 
> La bomba se prende cuando GPIO12 = HIGH (el relé cierra COM→NO)

---

# 📡 PINES DEL HELTEC - Resumen visual

```
┌─────────────────────────────────────────────┐
│              HELTEC LoRa32 V2               │
│                                             │
│   3.3V ─────●                               │
│             ├──► DHT22 VCC                  │
│             ├──► Hum Suelo VCC              │
│             └──► PCA9685 VCC (3.3V!)        │
│                                             │
│   GND  ─────●                               │
│             ├──► TODOS los GND              │
│             └──► (une todos los negros)     │
│                                             │
│   VIN  ─────●                               │
│             └──► Buck-Boost 5V              │
│                                             │
│   GPIO25 ───●────► DHT22 DATA               │
│                                             │
│   GPIO33 ───●────► Hum Suelo SIGNAL         │
│                                             │
│   GPIO32 ───●────► Sharp IR Amarillo        │
│                                             │
│   GPIO12 ───●────► Relé IN                  │
│                                             │
│   GPIO21 ───●────► PCA9685 SDA              │
│                                             │
│   GPIO22 ───●────► PCA9685 SCL              │
│                                             │
│   ┌─────────────┐                           │
│   │ OLED interno│  ← Usa GPIO4/15/16        │
│   │   (ya está) │    NO conectar nada ahí   │
│   └─────────────┘                           │
│                                             │
│   ┌─────────────┐                           │
│   │  LoRa interno│  ← Usa GPIO5/18/19/26/27 │
│   │   (ya está)  │    NO conectar nada ahí  │
│   └─────────────┘                           │
│                                             │
│   ◄── Antena LoRa (¡conectar!)              │
│                                             │
└─────────────────────────────────────────────┘
```

---

# 🎯 GATEWAY (Estación Base)

El gateway es solo un Heltec LoRa32 V2. **No necesitas cablear nada extra.**

| Componente | ¿Dónde está? |
|------------|-------------|
| WiFi | Dentro del Heltec (ya viene) |
| LoRa | Dentro del Heltec (ya viene) |
| OLED | Dentro del Heltec (ya viene) |
| Alimentación | Cable USB o pin VIN a 5V |

Solo necesitas:
- Conectar antena LoRa
- Conectar cable USB para alimentación + subir código

---

# ✅ CHECKLIST ANTES DE ENCENDER

| # | Revisar | ¿Cómo? |
|---|---------|--------|
| 1 | Todos los GND unidos | Multímetro en "continuidad", pitido entre todos los GND |
| 2 | PCA9685 VCC = 3.3V | Multímetro en pin VCC del PCA9685, debe marcar 3.3V |
| 3 | PCA9685 V+ = 5V | Multímetro en terminal V+, debe marcar 5V |
| 4 | Buck-Boost salida = 5V | Antes de conectar nada, medir salida |
| 5 | DHT22 en 3.3V | Si lo pones en 5V se puede quemar |
| 6 | Sharp en 5V | Si lo pones en 3.3V no funciona bien |
| 7 | Antena LoRa conectada | En AMBOS dispositivos antes de energizar |
| 8 | No hay cortos | Multímetro entre pines adyacentes del Heltec |

---

# ❌ ERRORES QUE DESTRUYEN COSAS

| Si haces esto... | ...pasa esto |
|------------------|-------------|
| PCA9685 VCC a 5V | **Quemas el I2C del ESP32** (Heltec muerto) |
| Encender sin antena LoRa | **Quemas el chip de radio** (SX1276 muerto) |
| DHT22 a 5V | Puede durar poco o funcionar mal |
| Sharp a 3.3V | Lee valores erráticos o cero |
| GND no común | Todo funciona mal o se reinicia |
| Bomba sin relé | Corona directa de 11.1V al GPIO12 = **ESP32 muerto** |

---

# 📸 Foto mental de tu circuito terminado

```
        ┌──────────────────┐
        │   BATERÍA 3S     │
        │    11.1V         │
        └────┬──────┬──────┘
             │      │
     ┌───────┘      └───────┐
     │                      │
     ▼                      ▼
┌──────────┐           ┌──────────┐
│Buck-Boost│           │  RELÉ    │
│  5V/3A   │           │  (COM)   │
└────┬─────┘           └────┬─────┘
     │                      │
     ├──────┬──────┬──┐    │ NO
     │      │      │  │    │
     ▼      ▼      ▼  │    ▼
┌────────┐┌────────┐│  │ ┌────────┐
│ Heltec ││PCA9685 ││  │ │  BOMBA │
│  VIN   ││V+   VCC││  │ │   12V  │
│  5V    ││5V   3.3V││  │ │        │
└────┬───┘└──┬──┬──┘│  │ └────────┘
     │       │  │   │  │
     │       │  │   │  │
GPIO25├──────┘  │   │  │
GPIO33├─────────┘   │  │
GPIO32├─────────────┘  │
GPIO12├────────────────┘
GPIO21├──► SDA
GPIO22├──► SCL
```

---

# 📝 RESUMEN ULTRA-CORTO

| Pin Heltec | ¿A qué va? | Pin del otro lado |
|------------|-----------|-------------------|
| **3.3V** | DHT22, Hum Suelo, PCA9685(VCC) | VCC de cada uno |
| **GND** | TODOS | GND de cada uno |
| **GPIO25** | DHT22 | DATA |
| **GPIO33** | Hum Suelo | SIGNAL |
| **GPIO32** | Sharp IR | Amarillo (Vo) |
| **GPIO12** | Relé | IN |
| **GPIO21** | PCA9685 | SDA |
| **GPIO22** | PCA9685 | SCL |
| **VIN** | Buck-Boost | 5V OUT |

| Pin Buck-Boost 5V | ¿A qué va? |
|-------------------|-----------|
| **5V OUT** | Heltec VIN, PCA9685 V+, Relé VCC, Sharp VCC |
| **GND** | Todos los GND |

| Pin Batería 3S directo | ¿A qué va? |
|------------------------|-----------|
| **+ 11.1V** | Buck-Boost IN+, Relé COM |
| **- GND** | Todos los GND |

---

*Documento simplificado para el Proyecto Autoinver v2.0*
