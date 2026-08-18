# Instrucciones para el Agente Implementador

## Contexto previo
El usuario ya construyó un MVP funcional de enlace LoRa entre dos Heltec LoRa32 V2:
- Nodo emisor con DHT22 enviando cada 1s
- Gateway receptor con OLED fija mostrando datos
- Sincronización de hora vía LoRa (REQ:HORA / SYNC:HORA)
- Interfaz fija en ambos dispositivos

## Tareas a implementar

### Fase 1: Hardware Nodo
1. Conectar PCA9685 a GPIO21 (SDA) y GPIO22 (SCL)
2. Conectar Servo A al canal 0, Servo B al canal 1
3. Conectar sensor humedad suelo a GPIO33 (3.3V)
4. Conectar Sharp 2Y0A21 a GPIO32 (alimentado 5V, Vo a GPIO32)
5. Conectar relé bomba a GPIO12 (alimentado 5V, switch bomba 12V desde batería 3S)
6. Verificar pines libres no se tocan

### Fase 2: Código Nodo
1. Instalar librería `Adafruit PWM Servo Driver Library`
2. Subir código `03-nodo-autoinver-codigo.md`
3. Calibrar SHARP_MIN_ADC y SHARP_MAX_ADC midiendo con tanque lleno/vacio
4. Ajustar umbrales TEMP_UMBRAL_1, TEMP_UMBRAL_2, SOIL_UMBRAL según cultivo
5. Verificar en OLED: muestra AUTOINVER, temp, hum, suelo, agua, compuertas, bomba, RSSI

### Fase 3: Código Gateway
1. Subir código `04-gateway-estacion-codigo.md`
2. Verificar conexión WiFi a "geogo_IoT" / "2iot2025"
3. Verificar NTP sincroniza hora Chile (GMT-4)
4. Verificar OLED muestra: GW-Autoinver-Geogo, fecha/hora, nodo, temp/hum, suelo/bomba, RSSI/tiempo
5. Probar web local: navegar a IP del gateway

### Fase 4: Backend
1. Crear proyecto Supabase
2. Ejecutar SQL de tablas (`05-backend-supabase.md`)
3. Obtener URL y anon key
4. Completar variables `supabaseUrl` y `supabaseKey` en gateway
5. Implementar función `enviarSupabase()` en gateway (POST HTTPS)

### Fase 5: Frontend
1. Crear repo GitHub con GitHub Pages
2. Implementar dashboard consumiendo Supabase REST API
3. Implementar polling de comandos pendientes (o usar Supabase Realtime)
4. Probar control manual de bomba desde web

### Fase 6: Buffer offline
1. Verificar SPIFFS funciona en gateway
2. Confirmar que si falla WiFi, los datos se acumulan en `/buffer.json`
3. Al restaurar WiFi, leer archivo y enviar líneas a Supabase, luego truncar

## Errores comunes a evitar
- Heltec V2 OLED usa GPIO4/15/16 (NO usar para otros dispositivos I2C)
- Sharp 2Y0A21 a 5V: su Vo nunca supera 3.1V, seguro para ADC ESP32
- PCA9685 necesita VCC=3.3V (lógica) Y V+=5V (fuerza servos)
- LoRa es half-duplex: no transmitir y recibir simultáneo
- Servos MG consumen ~500mA al moverse: asegurar buck-boost entrega suficiente corriente

## Entregables esperados
- Código nodo funcional con control autónomo
- Código gateway funcional con WiFi, web local, buffer SPIFFS
- Base de datos Supabase con datos históricos
- Dashboard web accesible públicamente