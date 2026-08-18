
---


# Frontend Dashboard

## Opción A: GitHub Pages (remoto)
- Repositorio GitHub con GitHub Pages habilitado
- HTML/JS vanilla (sin framework para simplicidad)
- Consume Supabase REST API con fetch() + apikey anon

### Funcionalidades
- Cards con Temp, Hum, Suelo, Nivel Agua, Bomba, Compuertas
- Gráfico histórico (Chart.js) leyendo últimos 100 registros de Supabase
- Botones de control manual que hacen POST a una Edge Function o a un endpoint del gateway (si expone puerto, requiere ngrok)
- Indicador de conexión (último paquete hace cuántos segundos)

### Estructura archivos
/docs (o /) 
index.html 
app.js 
style.css

## Opción B: Web Local (fallback en gateway)
- Servida por el ESP32 directamente (ya incluida en código gateway)
- Disponible en http://[IP-del-gateway]/
- Muestra últimos datos recibidos y permite control básico bomba ON/OFF
- No requiere internet

## Nota sobre control remoto
Para actuar desde GitHub Pages hacia el nodo:
1. GitHub Pages → POST a Supabase (tabla `pending_commands`)
2. Gateway lee `pending_commands` cada 30s vía GET
3. Gateway reenvía comando por LoRa al nodo
4. Nodo confirma y gateway marca comando como ejecutado

(AGENTE: implementar polling de comandos pendientes en el gateway)