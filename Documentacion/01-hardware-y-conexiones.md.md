# Hardware y Conexiones - Nodo Autoinver

## MCU Principal
- **Heltec LoRa32 V2** (ESP32 + SX1276 + OLED 0.96" SSD1306)
- Alimentación: 5V VIN (desde buck-boost)
- OLED interno: GPIO4 (SDA), GPIO15 (SCL), GPIO16 (RST)

## Alimentación
- **Battery Pack 3S Li-ion**: 11.1V nominal / 12.6V carga completa
- **Buck-Boost 5V/3A**: Alimenta:
  - Heltec VIN (5V)
  - PCA9685 V+ (5V para servos)
  - Relé VCC (5V)
  - Sharp 2Y0A21 VCC (5V) — *Nota: Sharp funciona a 5V, su Vo max ~3.1V, seguro para ADC ESP32*

## Sensores (alimentados 3.3V desde Heltec)
| Sensor | Pin | Tipo | Detalle |
|--------|-----|------|---------|
| DHT22 | GPIO25 | Digital | Temp/Hum ambiente |
| Humedad Suelo | GPIO33 | Analógico (ADC) | Capacitivo. Rango 0-3.3V |
| Sharp 2Y0A21 | GPIO32 | Analógico (ADC) | Nivel agua. Alimentado 5V, Vo a GPIO32 |

## Actuadores
| Actuador | Control | Alimentación | Detalle |
|----------|---------|--------------|---------|
| Servo Compuerta A | PCA9685 Canal 0 | 5V V+ | 0°=cerrada, 90°=abierta |
| Servo Compuerta B | PCA9685 Canal 1 | 5V V+ | 0°=cerrada, 90°=abierta |
| Relé Bomba 12V | GPIO12 | 5V VCC (rele), 11.1V (bomba) | Relé con optoacoplador. Bomba alimentada por batería 3S directo |

## Driver Servos
- **PCA9685** (I2C 0x40)
- SDA → GPIO21
- SCL → GPIO22
- VCC → 3.3V (lógica I2C)
- V+ → 5V (alimentación servos)

## Pines Libres (NO conectados)
GPIO0 (BOOT), GPIO2, GPIO13, GPIO17, GPIO23,
GPIO34 (IN only), GPIO35 (IN only), GPIO36-VP (IN only),
GPIO37, GPIO38, GPIO39-VN (IN only)

## Pines LoRa Internos (NO tocar)
GPIO5 (SCK), GPIO19 (MISO), GPIO27 (MOSI), GPIO18 (CS), GPIO14 (RST), GPIO26 (IRQ)

## Diagrama
Ver imagen: `diagrama_conexiones_autoinver.png`