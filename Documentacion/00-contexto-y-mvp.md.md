# Contexto del Proyecto Autoinver

## Historial (MVP ya construido)
Se construyó un enlace LoRa punto-a-punto funcional entre:
- **Nodo**: Heltec LoRa32 V2 (invernadero móvil, sin WiFi)
- **Gateway**: Heltec LoRa32 V2 (estación fija, con WiFi)

### Funcionalidades MVP existentes:
1. Transmisión de temperatura/humedad (DHT22) cada 1s vía LoRa 915 MHz
2. Pantalla OLED fija en gateway mostrando: título, fecha/hora, nodo, temp/hum, RSSI, tiempo último paquete
3. Sincronización de hora: nodo solicita `REQ:HORA|ID:AUTOINVER`, gateway responde `SYNC:HORA|YYYY-MM-DD_HH:MM:SS`
4. El nodo NO se conecta a WiFi. Todo pasa por LoRa.
5. El gateway se conecta a WiFi y sincroniza NTP.

## Arquitectura objetivo
[Nodo Autoinver] --LoRa 915MHz--> [Gateway Fijo] --WiFi--> [Supabase + GitHub Pages] | | Sensores + Buffer SPIFFS Actuadores Web local fallback

## Principios de diseño
- **Nodo**: Autónomo. Toma decisiones localmente (compuertas, bomba). Reporta estado.
- **Gateway**: Receptor, bridge a internet, buffer offline, servidor web local.
- **Comunicación**: LoRa es half-duplex. El nodo envía datos periódicamente. El gateway puede enviar comandos cuando el nodo no está transmitiendo.