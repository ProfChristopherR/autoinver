# Protocolo de Comunicación LoRa

## Parámetros Radio
- Frecuencia: 915 MHz
- Spreading Factor: 7
- Bandwidth: 125 kHz
- Coding Rate: 4/5
- TX Power: 17 dBm
- CRC: Habilitado

## Direccionalidad
- Nodo → Gateway: Paquetes de telemetría (cada 5 segundos en operación normal)
- Gateway → Nodo: Respuestas a solicitudes de hora, y comandos manuales

## Formato Paquete Nodo → Gateway (Telemetría)

ID:AUTOINVER|TS:YYYY-MM-DD_HH:MM:SS|TEMP:xx.xx|HUM:xx.xx|SOIL:xxx|WATER:xxx|WLVL:xx.x|S1:xxx|S2:xxx|PUMP:x|RSSI:xx|CNT:xxx


| Campo | Ejemplo | Descripción |
|-------|---------|-------------|
| ID | AUTOINVER | Identificador del nodo |
| TS | 2026-08-10_14:03:00 | Timestamp local del nodo |
| TEMP | 24.50 | Temperatura DHT22 (°C) |
| HUM | 65.00 | Humedad DHT22 (%) |
| SOIL | 342 | Valor ADC humedad suelo (0-4095) |
| WATER | 2890 | Valor ADC Sharp IR (0-4095) |
| WLVL | 85.5 | Nivel agua calculado (%) |
| S1 | 90 | Posición servo 1 (0-180) |
| S2 | 0 | Posición servo 2 (0-180) |
| PUMP | 1 | Estado bomba (0=off, 1=on) |
| RSSI | -82 | RSSI del último paquete recibido del GW (si aplica) |
| CNT | 42 | Contador de paquetes |

## Formato Solicitud Nodo → Gateway (Hora)

REQ:HORA|ID:AUTOINVER


## Formato Respuesta Gateway → Nodo (Hora)
SYNC:HORA|YYYY-MM-DD_HH:MM:SS

## Formato Comando Gateway → Nodo (Control manual)
CMD:AUTOINVER|ACT:xxxx|VAL:xx|TS:YYYY-MM-DD_HH:MM:SS

| Acción (ACT) | Valor (VAL) | Efecto |
|--------------|-------------|--------|
| BOMBA | 0 / 1 | Apagar / Encender bomba (override 10 min) |
| S1 | 0-180 | Posicionar compuerta A |
| S2 | 0-180 | Posicionar compuerta B |
| MODE | AUTO / MANUAL | Cambiar modo control local |

El nodo debe confirmar recepción de comando en el siguiente paquete de telemetría incluyendo `ACK:CMD|ACT:xxxx`.