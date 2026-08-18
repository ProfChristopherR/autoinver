# 🔧 Guía de Calibración - Autoinver v2.0

## 1. Calibración del Sensor Sharp IR (Nivel de Agua)

El sensor Sharp 2Y0A21 mide distancia por infrarrojo. Para convertir la lectura ADC a porcentaje de agua, necesitas calibrar los valores mínimo y máximo.

### Procedimiento:

1. **Tanque VACÍO**: Con el tanque completamente vacío, lee el valor ADC del pin GPIO32.
   - Abre el Monitor Serial del Arduino IDE
   - Anota el valor (ej: 3500)
   - Este será tu `SHARP_MAX_ADC`

2. **Tanque LLENO**: Llena el tanque al 100%.
   - Lee el valor ADC (ej: 500)
   - Este será tu `SHARP_MIN_ADC`

3. **Actualizar configuración**:
   ```cpp
   // En config.h o via comando remoto
   g_config.sharpMinADC = 500;   // Tu valor lleno
   g_config.sharpMaxADC = 3500;  // Tu valor vacío
   ```

### Tabla de referencia aproximada:
| Distancia (cm) | Voltaje (V) | ADC (aprox) |
|----------------|-------------|-------------|
| 10 (lleno)     | 2.5         | ~3100       |
| 20             | 1.5         | ~1860       |
| 30             | 1.0         | ~1240       |
| 40             | 0.8         | ~990        |
| 80 (vacío)     | 0.4         | ~500        |

> ⚠️ **IMPORTANTE**: El Sharp 2Y0A21 tiene un rango óptimo de 10-80cm. Asegúrate de que la superficie del agua esté dentro de este rango.

---

## 2. Calibración del Sensor de Humedad de Suelo

El sensor capacitivo de humedad de suelo tiene una salida analógica 0-3.3V.

### Procedimiento:

1. **Suelo SECO**: Coloca el sensor en suelo completamente seco.
   - Anota el valor ADC (típicamente ~3500-4095)
   - Este es tu valor máximo

2. **Suelo SATURADO**: Sumerge el sensor en agua (o suelo empapado).
   - Anota el valor ADC (típicamente ~0-500)
   - Este es tu valor mínimo

3. **Verificar rango**:
   ```cpp
   // El map() automático usa 0-4095 -> 0-100%
   // Si tu sensor tiene un rango diferente, ajusta en:
   // adcToSoilPercent() en el código del nodo
   ```

### Tips:
- Los sensores capacitivos son más estables que los resistivos
- La lectura varía con la salinidad del suelo
- Re-calibrar cada 3-6 meses

---

## 3. Calibración de Servos (PCA9685)

Los servos MG90S/MG995 pueden tener variaciones en sus límites de pulso.

### Procedimiento:

1. **Verificar límites mecánicos**:
   ```cpp
   // En setup() temporalmente:
   setServoAngle(0, 0);    delay(1000);
   setServoAngle(0, 45);   delay(1000);
   setServoAngle(0, 90);   delay(1000);
   ```

2. **Ajustar pulsos si es necesario**:
   ```cpp
   // En config.h:
   #define SERVO_PULSE_MIN  130   // Si 0° no cierra del todo
   #define SERVO_PULSE_MAX  620   // Si 90° no abre del todo
   ```

3. **Verificar alimentación**:
   - Los servos consumen ~500mA al moverse
   - Asegúrate de que el buck-boost entrega suficiente corriente
   - Si los servos "tartan", añade capacitores de 470µF en V+

---

## 4. Ajuste de Umbrales de Control

Los umbrales dependen del cultivo que estés cultivando.

### Valores sugeridos por tipo de cultivo:

| Cultivo      | Temp Máx (°C) | Temp Crítica (°C) | Humedad Suelo (%) |
|--------------|---------------|-------------------|-------------------|
| Tomates      | 28            | 32                | 60-80             |
| Lechugas     | 24            | 28                | 70-90             |
| Hierbas      | 26            | 30                | 50-70             |
| Pimientos    | 30            | 35                | 60-80             |

### Para ajustar:
1. Monitorea los datos durante 24-48h
2. Observa cuándo las plantas muestran estrés
3. Ajusta los umbrales gradualmente
4. Los cambios se pueden hacer vía dashboard (futuro) o editando `config.h`

---

## 5. Verificación del Enlace LoRa

### Alcance esperado:
- **Línea de vista**: 500m - 2km (con SF7, BW125)
- **Con obstáculos**: 100m - 300m
- **Interior**: 30m - 50m

### Para optimizar:
1. Usa antenas de 915MHz correctamente sintonizadas
2. Evita paredes metálicas entre nodo y gateway
3. Si hay mucha pérdida de paquetes, sube el SF a 9 (menor velocidad, mayor alcance)

### Métricas de salud del enlace:
| RSSI       | Calidad   | Acción recomendada        |
|------------|-----------|---------------------------|
| > -70 dBm  | Excelente | Ninguna                   |
| -70 a -90  | Buena     | Monitorear                |
| -90 a -100 | Débil     | Considerar repetidor      |
| < -100 dBm | Crítica   | Reubicar o aumentar SF    |

---

## 6. Checklist de Primer Encendido

- [ ] Verificar voltajes: Heltec 5V, PCA9685 VCC 3.3V, PCA9685 V+ 5V
- [ ] Verificar que LoRa inicia sin errores en ambos dispositivos
- [ ] Verificar que el nodo recibe hora del gateway (SYNC:HORA)
- [ ] Verificar que datos aparecen en OLED del gateway
- [ ] Verificar conexión WiFi del gateway
- [ ] Verificar sincronización NTP
- [ ] Verificar que datos llegan a Supabase
- [ ] Verificar que el dashboard muestra datos
- [ ] Probar comando manual de bomba desde dashboard
- [ ] Verificar que buffer SPIFFS funciona (desconectar WiFi, reconectar)

---

*Documento generado para el Proyecto Autoinver v2.0*
