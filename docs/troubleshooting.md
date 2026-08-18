# 🔍 Guía de Troubleshooting - Autoinver v2.0

## PROBLEMAS DE HARDWARE

### ❌ OLED no enciende / muestra basura
**Causas posibles:**
- Pines RST/SDA/SCL incorrectos
- OLED dañada

**Solución:**
```cpp
// Verificar en el código:
#define OLED_SDA    4
#define OLED_SCL    15
#define OLED_RST    16  // DEBE ser reseteado en setup()

// En setup():
pinMode(OLED_RST, OUTPUT);
digitalWrite(OLED_RST, LOW); delay(50);
digitalWrite(OLED_RST, HIGH); delay(50);
Wire.begin(OLED_SDA, OLED_SCL);
```

---

### ❌ LoRa no inicia ("LoRa FAIL")
**Causas posibles:**
- Frecuencia incorrecta (debe ser 915E6 para América)
- Pines SPI incorrectos
- Módulo SX1276 dañado

**Solución:**
```cpp
// Verificar pines (fijos en Heltec V2):
#define LORA_SCK    5
#define LORA_MISO   19
#define LORA_MOSI   27
#define LORA_CS     18
#define LORA_RST    14
#define LORA_IRQ    26

// Verificar que no hay conflictos con estos pines
```

---

### ❌ Servos no responden / vibran
**Causas posibles:**
- PCA9685 no tiene alimentación V+ (5V para servos)
- Dirección I2C incorrecta (default 0x40)
- Frecuencia PWM incorrecta (debe ser 50Hz)

**Solución:**
```cpp
// Verificar conexiones:
// PCA9685 VCC -> 3.3V (lógica I2C)
// PCA9685 V+  -> 5V (alimentación servos)
// PCA9685 GND -> GND común

// Verificar dirección I2C:
// Ejecutar I2C scanner para confirmar 0x40
```

---

### ❌ Bomba no enciende
**Causas posibles:**
- Relé con lógica invertida (LOW = ON en algunos relés)
- Insuficiente corriente desde batería 3S
- Fusible quemado

**Solución:**
```cpp
// Probar relé directamente:
digitalWrite(PIN_RELE, HIGH);   // Probar
// Si no funciona, intentar:
digitalWrite(PIN_RELE, LOW);    // Algunos relés son active-LOW
```

---

## PROBLEMAS DE COMUNICACIÓN

### ❌ Nodo no recibe hora del gateway
**Síntoma:** OLED muestra `[L]` o `[?]` permanentemente

**Solución:**
1. Verificar que el gateway está encendido y con LoRa iniciado
2. Verificar que ambos usan la misma frecuencia/SF/BW
3. Reducir distancia entre dispositivos para prueba
4. Verificar en Monitor Serial que se ven los `REQ:HORA` y `SYNC:HORA`

---

### ❌ Gateway no recibe datos del nodo
**Síntoma:** Dashboard muestra "SIN RX" o datos antiguos

**Solución:**
1. Verificar que el nodo está transmitiendo (LED parpadea en TX)
2. Verificar RSSI en gateway: si es < -100 dBm, acercar dispositivos
3. Verificar que no hay interferencia en 915 MHz
4. Probar cambiar a SF9 para mayor alcance

---

### ❌ WiFi no conecta en gateway
**Síntoma:** "WiFi:--" en OLED, IP no asignada

**Solución:**
```cpp
// Verificar credenciales:
const char* ssid = "geogo_IoT";      // Exacto, case-sensitive
const char* password = "2iot2025";   // Exacto

// Verificar que el WiFi está en 2.4GHz (ESP32 no soporta 5GHz)
// Verificar fuerza de señal: -70 dBm o mejor
```

---

## PROBLEMAS DE CLOUD

### ❌ Datos no llegan a Supabase
**Síntoma:** Buffer crece indefinidamente, dashboard vacío

**Solución:**
1. Verificar credenciales en gateway:
   ```cpp
   const char* supabaseUrl = "https://TU-PROJECT.supabase.co";
   const char* supabaseKey = "TU-ANON-KEY";
   ```

2. Verificar que la tabla existe y RLS permite inserts:
   ```sql
   -- En SQL Editor de Supabase:
   select * from autoinver_data limit 1;
   ```

3. Verificar que el gateway tiene hora NTP correcta (HTTPS requiere certificados válidos)

4. Verificar en Monitor Serial del gateway:
   ```
   [SUPA] Error HTTP 401  -> Key incorrecta
   [SUPA] Error HTTP 404  -> Tabla no existe
   [SUPA] Error HTTP 400  -> JSON mal formado
   ```

---

### ❌ Dashboard no carga
**Síntoma:** Página en blanco o errores en consola

**Solución:**
1. Verificar que `SUPABASE_URL` y `SUPABASE_KEY` están configurados en `index.html`
2. Verificar en consola del navegador (F12) errores de CORS
3. En Supabase, ir a Settings > API > Configurar CORS:
   ```
   Allowed origins: https://TU-USUARIO.github.io, http://localhost:5173
   ```

---

## PROBLEMAS DE RENDIMIENTO

### ❌ Gateway se reinicia solo
**Causas:** Watchdog, stack overflow, memory leak

**Solución:**
```cpp
// Verificar heap libre:
Serial.print("Free heap: ");
Serial.println(ESP.getFreeHeap());

// Si es < 20,000 bytes, reducir:
// - Tamaño de buffer SPIFFS
// - Frecuencia de envío a Supabase
// - Desactivar logs de debug
```

---

### ❌ Nodo consume mucha batería
**Causas:** Sin sleep, servos siempre activos, transmisión muy frecuente

**Solución:**
```cpp
// Aumentar intervalos:
#define TX_INTERVAL_MS      30000   // De 5s a 30s
#define SENSOR_INTERVAL_MS  10000   // De 2s a 10s

// Implementar sleep profundo entre ciclos (avanzado)
// Apagar OLED entre actualizaciones
```

---

## ERRORES COMUNES Y CÓDIGOS

| Código/Error | Significado | Solución |
|--------------|-------------|----------|
| LoRa FAIL | Inicialización SX1276 fallida | Revisar pines SPI, verificar frecuencia |
| SPIFFS fail | Flash corrupta o mal formateada | Ejecutar `SPIFFS.format()` una vez |
| HTTP 401 | No autorizado | Revisar SUPABASE_KEY |
| HTTP 404 | Recurso no encontrado | Revisar nombre de tabla |
| HTTP 409 | Conflicto (duplicado) | Ignorar o usar `Prefer: resolution=ignore-duplicates` |
| Watchdog | Tarea bloqueada > 5s | Evitar `delay()` largos, usar máquinas de estado |

---

## CONTACTO Y SOPORTE

Si el problema persiste después de seguir esta guía:

1. **Capturar logs**: Conectar ambos dispositivos por USB y copiar todo del Monitor Serial
2. **Verificar voltajes**: Con multímetro, medir en todos los puntos de alimentación
3. **Aislar el problema**: Probar cada componente individualmente

---

*Documento generado para el Proyecto Autoinver v2.0*
