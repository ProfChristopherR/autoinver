
---


# Código Completo - Gateway Estación (Heltec LoRa32 V2)

## Librerías adicionales requeridas
- `NTPClient` by Fabrice Weinberg
- `ArduinoJson` by Benoit Blanchon (para buffer SPIFFS)

## Código completo
cpp
/*
 * ============================================
 * GATEWAY ESTACION - RECEPTOR + WIFI + WEB
 * Heltec LoRa32 V2 + OLED + WiFi + NTP
 * 
 * Funciones:
 * - Recibir telemetria LoRa del nodo
 * - Mostrar datos en OLED (interfaz fija abreviada)
 * - Sincronizar hora NTP via WiFi "geogo_IoT"
 * - Responder solicitudes de hora del nodo
 * - Enviar datos a Supabase cada 30s
 * - Buffer local SPIFFS si falla internet
 * - Servidor web local fallback (dashboard simple)
 * - Recibir comandos web y reenviar por LoRa al nodo
 * ============================================
 */

#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Time.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

// ---------- PINES HELTEC V2 ----------
#define LORA_SCK    5
#define LORA_MISO   19
#define LORA_MOSI   27
#define LORA_CS     18
#define LORA_RST    14
#define LORA_IRQ    26
#define OLED_SDA    4
#define OLED_SCL    15
#define OLED_RST    16
#define OLED_ADDR   0x3C

// ---------- CONFIGURACION ----------
#define FREQUENCY       915E6
const char* ssid = "geogo_IoT";
const char* password = "2iot2025";
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -14400;

// Supabase config (AGENTE: completar con credenciales reales)
const char* supabaseUrl = "https://TU-PROJECT.supabase.co";
const char* supabaseKey = "TU-ANON-KEY";
const char* supabaseTable = "autoinver_data";

// ---------- VARIABLES ----------
Adafruit_SSD1306 display(128, 64, &Wire, OLED_RST);
ESP32Time rtc(0);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer, gmtOffset_sec, 60000);
WebServer server(80);

// Datos nodo
float lastTemp=0, lastHum=0, lastSoil=0, lastWater=0;
int lastS1=0, lastS2=0, lastRSSI=0, packetCnt=0;
bool lastPump=false;
String lastNodeID="--";
String lastTS="--";
unsigned long lastPacketTime=0;
bool hayDatos=false;

// Estado sistema
bool wifiOK=false, ntpOK=false;

// Buffer SPIFFS
#define BUFFER_FILE "/buffer.json"
unsigned long lastSupabaseSend=0;

// ---------- SETUP ----------
void setup() {
  Serial.begin(115200);
  delay(1000);

  // SPIFFS
  if(!SPIFFS.begin(true)) Serial.println("[WARN] SPIFFS fail");

  // OLED
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW); delay(50);
  digitalWrite(OLED_RST, HIGH); delay(50);
  Wire.begin(OLED_SDA, OLED_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);

  mostrarInicio();
  delay(1500);

  // WiFi
  WiFi.begin(ssid, password);
  int intentos=0;
  while(WiFi.status()!=WL_CONNECTED && intentos<30){
    delay(500); intentos++;
    mostrarWiFi(intentos);
  }
  wifiOK = (WiFi.status()==WL_CONNECTED);
  if(wifiOK){
    timeClient.begin();
    if(timeClient.update()){
      rtc.setTime(timeClient.getEpochTime());
      ntpOK=true;
    }
  }

  // LoRa
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  if(!LoRa.begin(FREQUENCY)){
    mostrarError("LoRa FAIL"); while(1) delay(1000);
  }
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setTxPower(17);
  LoRa.enableCrc();

  // Web server local
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/data", HTTP_GET, handleApiData);
  server.on("/api/cmd", HTTP_POST, handleApiCmd);
  server.begin();

  mostrarEstadoOK();
  delay(2000);
}

// ---------- LOOP ----------
void loop() {
  unsigned long now = millis();

  // NTP update cada 5 min
  if(wifiOK && now%300000<1000){
    if(timeClient.update()) rtc.setTime(timeClient.getEpochTime());
  }

  // Recibir LoRa
  int pkt = LoRa.parsePacket();
  if(pkt) procesarPaquete();

  // Enviar a Supabase cada 30s
  if(wifiOK && now-lastSupabaseSend>=30000){
    lastSupabaseSend=now;
    enviarSupabase();
  }

  // Web server
  server.handleClient();

  // Actualizar OLED cada 1s
  static unsigned long lastDisp=0;
  if(now-lastDisp>=1000){
    lastDisp=now;
    actualizarInterfaz();
  }

  delay(10);
}

// ---------- PROCESAMIENTO LORA ----------
void procesarPaquete(){
  String rx="";
  while(LoRa.available()) rx += (char)LoRa.read();

  // Solicitud hora del nodo
  if(rx.indexOf("REQ:HORA")>=0){
    enviarHoraNodo();
    return;
  }

  // Telemetria
  lastRSSI = LoRa.packetRssi();
  packetCnt++;
  lastPacketTime = millis();
  hayDatos = true;

  parsearTelemetria(rx);
  guardarBufferLocal(rx);  // Guardar siempre localmente

  Serial.println("[RX] " + rx);
}

void enviarHoraNodo(){
  String s = "SYNC:HORA|";
  s += String(rtc.getYear())+"-";
  if(rtc.getMonth()+1<10) s+="0"; s+=String(rtc.getMonth()+1)+"-";
  if(rtc.getDay()<10) s+="0"; s+=String(rtc.getDay())+"_";
  if(rtc.getHour(true)<10) s+="0"; s+=String(rtc.getHour(true))+":";
  if(rtc.getMinute()<10) s+="0"; s+=String(rtc.getMinute())+":";
  if(rtc.getSecond()<10) s+="0"; s+=String(rtc.getSecond());

  LoRa.beginPacket();
  LoRa.print(s);
  LoRa.endPacket();
  Serial.println("[TX-HORA] " + s);
}

void parsearTelemetria(String p){
  // ID:AUTOINVER|TS:...|TEMP:...|...
  int i1 = p.indexOf("ID:")+3; int i2 = p.indexOf("|",i1);
  if(i1>2 && i2>i1) lastNodeID = p.substring(i1,i2);

  i1 = p.indexOf("TS:")+3; i2 = p.indexOf("|",i1);
  if(i1>2 && i2>i1) lastTS = p.substring(i1,i2);

  i1 = p.indexOf("TEMP:")+5; i2 = p.indexOf("|",i1);
  if(i1>4 && i2>i1) lastTemp = p.substring(i1,i2).toFloat();

  i1 = p.indexOf("HUM:")+4; i2 = p.indexOf("|",i1);
  if(i1>3 && i2>i1) lastHum = p.substring(i1,i2).toFloat();

  i1 = p.indexOf("SOIL:")+5; i2 = p.indexOf("|",i1);
  if(i1>4 && i2>i1) lastSoil = p.substring(i1,i2).toFloat();

  i1 = p.indexOf("WLVL:")+5; i2 = p.indexOf("|",i1);
  if(i1>4 && i2>i1) lastWater = p.substring(i1,i2).toFloat();

  i1 = p.indexOf("S1:")+3; i2 = p.indexOf("|",i1);
  if(i1>2 && i2>i1) lastS1 = p.substring(i1,i2).toInt();

  i1 = p.indexOf("S2:")+3; i2 = p.indexOf("|",i1);
  if(i1>2 && i2>i1) lastS2 = p.substring(i1,i2).toInt();

  i1 = p.indexOf("PUMP:")+5; i2 = p.indexOf("|",i1);
  if(i1>4 && i2>i1) lastPump = (p.substring(i1,i2)=="1");
}

// ---------- BUFFER LOCAL SPIFFS ----------
void guardarBufferLocal(String payload){
  File f = SPIFFS.open(BUFFER_FILE, FILE_APPEND);
  if(!f) return;
  StaticJsonDocument<512> doc;
  doc["ts"] = obtenerTimestamp();
  doc["payload"] = payload;
  doc["rssi"] = lastRSSI;
  String out;
  serializeJson(doc, out);
  f.println(out);
  f.close();
}

void enviarSupabase(){
  // AGENTE: Implementar POST HTTPS a Supabase REST API
  // Leer BUFFER_FILE, enviar linea por linea.
  // Si exitoso, truncar archivo.
  // Por ahora, placeholder:
  Serial.println("[SUPABASE] Enviando datos pendientes...");
}

// ---------- WEB SERVER LOCAL ----------
void handleRoot(){
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'><title>Autoinver Local</title>";
  html += "<style>body{font-family:Arial;background:#1a1a1a;color:#fff;padding:20px;}";
  html += ".card{background:#333;border-radius:10px;padding:15px;margin:10px 0;}";
  html += ".value{font-size:2em;color:#4CAF50;}</style></head><body>";
  html += "<h1>🌿 Autoinver - Dashboard Local</h1>";
  html += "<div class='card'><h3>Temperatura</h3><div class='value'>"+String(lastTemp,1)+"°C</div></div>";
  html += "<div class='card'><h3>Humedad</h3><div class='value'>"+String(lastHum,1)+"%</div></div>";
  html += "<div class='card'><h3>Suelo</h3><div class='value'>"+String(lastSoil,1)+"%</div></div>";
  html += "<div class='card'><h3>Nivel Agua</h3><div class='value'>"+String(lastWater,1)+"%</div></div>";
  html += "<div class='card'><h3>Bomba</h3><div class='value'>"+String(lastPump?"ON":"OFF")+"</div></div>";
  html += "<div class='card'><h3>Compuertas</h3><div class='value'>A:"+String(lastS1)+" B:"+String(lastS2)+"</div></div>";
  html += "<div class='card'><h3>Ultimo paquete</h3><div class='value'>"+String((millis()-lastPacketTime)/1000)+"s</div></div>";
  html += "<hr><h3>Control Manual</h3>";
  html += "<form action='/api/cmd' method='POST'>";
  html += "<input type='hidden' name='act' value='BOMBA'>";
  html += "<button name='val' value='1'>Bomba ON</button> ";
  html += "<button name='val' value='0'>Bomba OFF</button></form>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleApiData(){
  StaticJsonDocument<512> doc;
  doc["temp"] = lastTemp;
  doc["hum"] = lastHum;
  doc["soil"] = lastSoil;
  doc["water"] = lastWater;
  doc["pump"] = lastPump;
  doc["s1"] = lastS1;
  doc["s2"] = lastS2;
  doc["rssi"] = lastRSSI;
  doc["last_seen"] = (millis()-lastPacketTime)/1000;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleApiCmd(){
  if(!server.hasArg("act") || !server.hasArg("val")){
    server.send(400, "text/plain", "Bad Request"); return;
  }
  String act = server.arg("act");
  String val = server.arg("val");

  // Reenviar por LoRa al nodo
  String cmd = "CMD:AUTOINVER|ACT:"+act+"|VAL:"+val+"|TS:"+obtenerTimestamp();
  LoRa.beginPacket();
  LoRa.print(cmd);
  LoRa.endPacket();

  server.send(200, "text/plain", "Comando enviado: " + cmd);
}

// ---------- PANTALLA OLED FIJA ----------
void actualizarInterfaz(){
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // F1: Titulo
  display.setCursor(0,0);
  display.print("GW-Autoinver-Geogo");

  // F2: Fecha/Hora
  display.setCursor(0,9);
  display.print(rtc.getDay()); display.print("/");
  display.print(rtc.getMonth()+1); display.print("/");
  display.print(rtc.getYear()); display.print(" ");
  display.print(rtc.getHour(true)); display.print(":");
  if(rtc.getMinute()<10) display.print("0");
  display.print(rtc.getMinute());

  // F3: Linea
  display.setCursor(0,18);
  display.print("----------------");

  // F4: Nodo
  display.setCursor(0,27);
  display.print("Nodo:"); display.print(lastNodeID);

  // F5: Temp/Hum (abreviado)
  display.setCursor(0,36);
  display.print("T:"); display.print(lastTemp,1); display.print("C ");
  display.print("H:"); display.print(lastHum,1); display.print("%");

  // F6: Suelo/Agua/Bomba
  display.setCursor(0,45);
  display.print("SL:"); display.print(lastSoil,0); display.print("% ");
  display.print("B:"); display.print(lastPump?"ON":"OFF");

  // F7: RSSI + Ultimo paquete
  display.setCursor(0,54);
  display.print("R:"); display.print(lastRSSI); display.print("dBm ");
  if(hayDatos){
    unsigned long s = (millis()-lastPacketTime)/1000;
    if(s<60){display.print(s);display.print("s");}
    else{display.print(s/60);display.print("m");}
  }else{
    display.print("SIN RX");
  }

  display.display();
}

void mostrarInicio(){
  display.clearDisplay();
  display.setCursor(0,10); display.print("GW-Autoinver-Geogo");
  display.setCursor(0,25); display.print("Iniciando...");
  display.setCursor(0,40); display.print("WiFi+LoRa+Web");
  display.display();
}

void mostrarWiFi(int i){
  display.clearDisplay();
  display.setCursor(0,0); display.print("Conectando WiFi");
  display.setCursor(0,20); display.print("geogo_IoT");
  display.setCursor(0,40); display.print("Intento "); display.print(i);
  display.display();
}

void mostrarEstadoOK(){
  display.clearDisplay();
  display.setCursor(0,0); display.print("GW OK");
  display.setCursor(0,15); display.print(wifiOK?"WiFi:OK":"WiFi:--");
  display.setCursor(0,28); display.print(ntpOK?"NTP:OK":"NTP:--");
  display.setCursor(0,41); display.print("IP:"); display.print(WiFi.localIP());
  display.setCursor(0,54); display.print("Web:80 LoRa:915M");
  display.display();
}

void mostrarError(String e){
  display.clearDisplay();
  display.setCursor(0,20); display.print("ERROR:"); display.print(e);
  display.display();
}

String obtenerTimestamp(){
  String t=String(rtc.getYear())+"-";
  if(rtc.getMonth()+1<10)t+="0"; t+=String(rtc.getMonth()+1)+"-";
  if(rtc.getDay()<10)t+="0"; t+=String(rtc.getDay())+"_";
  if(rtc.getHour(true)<10)t+="0"; t+=String(rtc.getHour(true))+":";
  if(rtc.getMinute()<10)t+="0"; t+=String(rtc.getMinute())+":";
  if(rtc.getSecond()<10)t+="0"; t+=String(rtc.getSecond());
  return t;
}