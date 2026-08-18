# Código Completo - Nodo Autoinver (Heltec LoRa32 V2)

## Librerías Arduino IDE requeridas
- `DHT sensor library` by Adafruit (1.4.6)
- `Adafruit Unified Sensor` (1.1.14)
- `LoRa` by Sandeep Mistry (0.8.0)
- `Adafruit SSD1306` (2.5.9)
- `Adafruit PWM Servo Driver Library` (PCA9685)
- `ESP32Time` by F.Bisinger

## Código completo
```cpp
/*
 * ============================================
 * NODO AUTOINVER - CONTROL COMPLETO
 * Heltec LoRa32 V2 + DHT22 + Hum Suelo + Sharp IR
 * PCA9685 (2 servos) + Rele Bomba
 * 
 * Decisiones locales autonomas + Reporte LoRa
 * ============================================
 */

#include &lt;SPI.h&gt;
#include &lt;LoRa.h&gt;
#include &lt;DHT.h&gt;
#include &lt;Wire.h&gt;
#include &lt;Adafruit_SSD1306.h&gt;
#include &lt;ESP32Time.h&gt;
#include &lt;Adafruit_PWMServoDriver.h&gt;

// ---------- PINES HELTEC V2 ----------
#define DHT_PIN     25
#define DHT_TYPE    DHT22
#define PIN_SOIL    33
#define PIN_SHARP   32
#define PIN_RELE    12

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
#define TX_INTERVAL     5000    // 5 segundos para operacion normal
#define HORA_INI_H      14
#define HORA_INI_M      3
#define HORA_INI_S      0
#define HORA_INI_D      10
#define HORA_INI_MO     8
#define HORA_INI_Y      2026

// Umbrales control
#define TEMP_UMBRAL_1   28.0    // Abrir compuerta 1
#define TEMP_UMBRAL_2   32.0    // Abrir compuerta 2 tambien
#define TEMP_HISTERESIS 1.5     // Histeresis para cerrar
#define SOIL_UMBRAL     40.0    // % humedad suelo. Menor = regar
#define SOIL_HISTERESIS 10.0    // Dejar de regar cuando sube X%
#define BOMBA_DURACION  10000   // ms que dura el riego
#define SHARP_MIN_ADC   500     // Calibrar: ADC cuando tanque lleno
#define SHARP_MAX_ADC   3500    // Calibrar: ADC cuando tanque vacio

// ---------- OBJETOS ----------
DHT dht(DHT_PIN, DHT_TYPE);
Adafruit_SSD1306 display(128, 64, &Wire, OLED_RST);
ESP32Time rtc(-14400);  // GMT-4 Chile
Adafruit_PWMServoDriver pca = Adafruit_PWMServoDriver(0x40);

// ---------- VARIABLES ----------
float temp = 0, hum = 0;
int soilADC = 0, sharpADC = 0;
float soilPct = 0, waterPct = 0;
bool pumpState = false;
int servo1Pos = 0, servo2Pos = 0;
unsigned long pumpStartTime = 0;
bool pumpAutoMode = false;

unsigned long lastTx = 0;
int packetCnt = 0;
int lastGW_RSSI = 0;

// Sync hora
enum SyncState { SYNC_NONE, SYNC_WAIT, SYNC_OK, SYNC_FAIL };
SyncState syncState = SYNC_NONE;
unsigned long lastSyncReq = 0;
int syncAttempts = 0;

// Control manual override
bool manualOverride = false;
unsigned long manualTimeout = 0;
String lastCmd = "NINGUNO";

// ---------- SETUP ----------
void setup() {
  Serial.begin(115200);
  delay(1000);

  // OLED
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW); delay(50);
  digitalWrite(OLED_RST, HIGH); delay(50);
  Wire.begin(OLED_SDA, OLED_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);

  // RTC fallback
  rtc.setTime(HORA_INI_S, HORA_INI_M, HORA_INI_H, HORA_INI_D, HORA_INI_MO, HORA_INI_Y);

  // Sensores
  dht.begin();
  pinMode(PIN_SOIL, INPUT);
  pinMode(PIN_SHARP, INPUT);
  pinMode(PIN_RELE, OUTPUT);
  digitalWrite(PIN_RELE, LOW);

  // PCA9685
  pca.begin();
  pca.setPWMFreq(50); // 50Hz para servos
  setServo(0, 0);   // Cerrar compuerta A
  setServo(1, 0);   // Cerrar compuerta B

  // LoRa
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  if (!LoRa.begin(FREQUENCY)) {
    mostrarError("LoRa FAIL");
    while(1) delay(1000);
  }
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setTxPower(17);
  LoRa.enableCrc();

  mostrarInicio();
  delay(2000);
}

// ---------- LOOP ----------
void loop() {
  unsigned long now = millis();

  // 1. Leer sensores cada 2s
  static unsigned long lastSensor = 0;
  if (now - lastSensor &gt;= 2000) {
    lastSensor = now;
    leerSensores();
    logicaControl();  // Decisiones autonomas
  }

  // 2. Gestionar bomba (apagar si paso el tiempo)
  gestionarBomba(now);

  // 3. Enviar telemetria cada 5s
  if (now - lastTx &gt;= TX_INTERVAL) {
    lastTx = now;
    enviarTelemetria();
  }

  // 4. Sync hora (solo al inicio, no bloqueante)
  if (syncState == SYNC_NONE || syncState == SYNC_WAIT) {
    gestionarSyncHora(now);
  }

  // 5. Escuchar comandos del gateway
  escucharComandos();

  // 6. Actualizar pantalla cada 500ms
  static unsigned long lastDisp = 0;
  if (now - lastDisp &gt;= 500) {
    lastDisp = now;
    actualizarPantalla();
  }

  delay(10);
}

// ---------- SENSORES ----------
void leerSensores() {
  temp = dht.readTemperature();
  hum = dht.readHumidity();
  soilADC = analogRead(PIN_SOIL);
  sharpADC = analogRead(PIN_SHARP);

  // Calcular porcentajes (ajustar segun calibracion)
  soilPct = map(constrain(soilADC, 0, 4095), 0, 4095, 0, 100);
  // Sharp: mayor ADC = mas cerca = mas agua (depende de montaje)
  waterPct = map(constrain(sharpADC, SHARP_MIN_ADC, SHARP_MAX_ADC), SHARP_MAX_ADC, SHARP_MIN_ADC, 0, 100);
  waterPct = constrain(waterPct, 0, 100);

  if (isnan(temp)) temp = -999;
  if (isnan(hum)) hum = -999;
}

// ---------- LOGICA CONTROL AUTONOMO ----------
void logicaControl() {
  if (manualOverride && millis() &lt; manualTimeout) return;  // Respetar override

  // --- Control Temperatura (Compuertas) ---
  if (temp &gt; TEMP_UMBRAL_2) {
    servo1Pos = 90;  // Abrir A
    servo2Pos = 90;  // Abrir B
  } else if (temp &gt; TEMP_UMBRAL_1) {
    servo1Pos = 90;  // Abrir A
    servo2Pos = 0;   // Cerrar B
  } else if (temp &lt; (TEMP_UMBRAL_1 - TEMP_HISTERESIS)) {
    servo1Pos = 0;   // Cerrar A
    servo2Pos = 0;   // Cerrar B
  }
  // Aplicar
  setServo(0, servo1Pos);
  setServo(1, servo2Pos);

  // --- Control Riego ---
  if (!pumpState && soilPct &lt; SOIL_UMBRAL && waterPct &gt; 20.0) {
    // Tierra seca Y hay agua en el tanque
    activarBomba(true, true);  // true = auto
  } else if (pumpState && pumpAutoMode && soilPct &gt; (SOIL_UMBRAL + SOIL_HISTERESIS)) {
    // Ya humedecio suficiente
    activarBomba(false, true);
  }
}

void activarBomba(bool on, bool esAuto) {
  pumpState = on;
  pumpAutoMode = esAuto;
  digitalWrite(PIN_RELE, on ? HIGH : LOW);
  if (on) pumpStartTime = millis();
}

void gestionarBomba(unsigned long now) {
  if (pumpState && (now - pumpStartTime &gt;= BOMBA_DURACION)) {
    // Timeout seguridad bomba (max 10s por ciclo)
    if (pumpAutoMode) activarBomba(false, true);
  }
}

// ---------- SERVOS PCA9685 ----------
void setServo(uint8_t canal, uint16_t angulo) {
  // Convertir 0-180 a pulsos PCA9685 (~150 a ~600 para 50Hz)
  uint16_t pulso = map(constrain(angulo, 0, 180), 0, 180, 150, 600);
  pca.setPWM(canal, 0, pulso);
}

// ---------- COMUNICACION LORA ----------
void enviarTelemetria() {
  packetCnt++;
  String ts = obtenerTimestamp();
  String payload = "ID:AUTOINVER|";
  payload += "TS:" + ts + "|";
  payload += "TEMP:" + String(temp, 1) + "|";
  payload += "HUM:" + String(hum, 1) + "|";
  payload += "SOIL:" + String(soilADC) + "|";
  payload += "WATER:" + String(sharpADC) + "|";
  payload += "WLVL:" + String(waterPct, 1) + "|";
  payload += "S1:" + String(servo1Pos) + "|";
  payload += "S2:" + String(servo2Pos) + "|";
  payload += "PUMP:" + String(pumpState ? 1 : 0) + "|";
  payload += "RSSI:" + String(lastGW_RSSI) + "|";
  payload += "CNT:" + String(packetCnt);

  LoRa.beginPacket();
  LoRa.print(payload);
  LoRa.endPacket();

  Serial.println("[TX] " + payload);
}

void escucharComandos() {
  int pkt = LoRa.parsePacket();
  if (!pkt) return;

  String rx = "";
  while (LoRa.available()) rx += (char)LoRa.read();

  if (rx.indexOf("CMD:AUTOINVER") &lt; 0) return;  // No es para mi

  Serial.println("[RX-CMD] " + rx);
  procesarComando(rx);
}

void procesarComando(String cmd) {
  // CMD:AUTOINVER|ACT:BOMBA|VAL:1|TS:...
  int actIdx = cmd.indexOf("ACT:") + 4;
  int actEnd = cmd.indexOf("|", actIdx);
  String act = cmd.substring(actIdx, actEnd);

  int valIdx = cmd.indexOf("VAL:") + 4;
  int valEnd = cmd.indexOf("|", valIdx);
  if (valEnd &lt; 0) valEnd = cmd.length();
  String valStr = cmd.substring(valIdx, valEnd);

  manualOverride = true;
  manualTimeout = millis() + 600000;  // 10 minutos de override

  if (act == "BOMBA") {
    activarBomba(valStr == "1", false);
    lastCmd = "BOMBA:" + valStr;
  } else if (act == "S1") {
    servo1Pos = constrain(valStr.toInt(), 0, 180);
    setServo(0, servo1Pos);
    lastCmd = "S1:" + valStr;
  } else if (act == "S2") {
    servo2Pos = constrain(valStr.toInt(), 0, 180);
    setServo(1, servo2Pos);
    lastCmd = "S2:" + valStr;
  } else if (act == "MODE") {
    if (valStr == "AUTO") manualOverride = false;
    lastCmd = "MODE:" + valStr;
  }
}

// ---------- SYNC HORA ----------
void gestionarSyncHora(unsigned long now) {
  if (syncState == SYNC_NONE) {
    if (syncAttempts &lt; 5) {
      solicitarHora();
      syncState = SYNC_WAIT;
      lastSyncReq = now;
      syncAttempts++;
    } else {
      syncState = SYNC_FAIL;
    }
  } else if (syncState == SYNC_WAIT) {
    if (now - lastSyncReq &gt; 10000) {
      syncState = SYNC_NONE;  // Timeout, reintentar
      return;
    }
    // Revisar si llego respuesta
    int pkt = LoRa.parsePacket();
    if (pkt) {
      String rx = "";
      while (LoRa.available()) rx += (char)LoRa.read();
      if (rx.indexOf("SYNC:HORA") &gt;= 0) {
        aplicarHora(rx);
      }
    }
  }
}

void solicitarHora() {
  LoRa.beginPacket();
  LoRa.print("REQ:HORA|ID:AUTOINVER");
  LoRa.endPacket();
}

void aplicarHora(String payload) {
  int p = payload.indexOf("SYNC:HORA|") + 10;
  if (p &lt; 10) return;
  String h = payload.substring(p);
  if (h.length() &lt; 19) return;
  int y = h.substring(0,4).toInt();
  int mo = h.substring(5,7).toInt();
  int d = h.substring(8,10).toInt();
  int hr = h.substring(11,13).toInt();
  int mn = h.substring(14,16).toInt();
  int sc = h.substring(17,19).toInt();
  if (y &lt; 2024 || y &gt; 2030) return;
  rtc.setTime(sc, mn, hr, d, mo, y);
  syncState = SYNC_OK;
}

// ---------- PANTALLA ----------
void actualizarPantalla() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Fila 1: Titulo + hora
  display.setCursor(0,0);
  display.print("AUTOINVER");
  display.setCursor(70,0);
  display.print(rtc.getHour(true)); display.print(":");
  if(rtc.getMinute()&lt;10) display.print("0");
  display.print(rtc.getMinute());

  // Fila 2: Fecha + sync
  display.setCursor(0,9);
  display.print(rtc.getDay()); display.print("/");
  display.print(rtc.getMonth()+1); display.print("/");
  display.print(rtc.getYear());
  display.setCursor(100,9);
  if(syncState==SYNC_OK) display.print("[S]");
  else if(syncState==SYNC_WAIT) display.print("[?]");
  else display.print("[L]");

  // Fila 3: separador
  display.setCursor(0,18);
  display.print("----------------");

  // Fila 4: Temp/Hum
  display.setCursor(0,27);
  display.print("T:"); display.print(temp,1); display.print("C ");
  display.print("H:"); display.print(hum,1); display.print("%");

  // Fila 5: Suelo/Agua
  display.setCursor(0,36);
  display.print("SL:"); display.print(soilPct,0); display.print("% ");
  display.print("WT:"); display.print(waterPct,0); display.print("%");

  // Fila 6: Servos + Bomba
  display.setCursor(0,45);
  display.print("C1:"); display.print(servo1Pos);
  display.print(" C2:"); display.print(servo2Pos);
  display.print(" B:"); display.print(pumpState?"ON":"OFF");

  // Fila 7: RSSI + Modo
  display.setCursor(0,54);
  display.print("GW:"); display.print(lastGW_RSSI); display.print("dBm ");
  display.print(manualOverride?"MAN":"AUTO");

  display.display();
}

void mostrarInicio() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,10); display.print("AUTOINVER");
  display.setCursor(0,25); display.print("Iniciando...");
  display.setCursor(0,40); display.print("DHT+Soil+Sharp");
  display.setCursor(0,55); display.print("PCA9685+LoRa");
  display.display();
}

void mostrarError(String e) {
  display.clearDisplay();
  display.setCursor(0,20); display.print("ERROR:");
  display.setCursor(0,35); display.print(e);
  display.display();
}

String obtenerTimestamp() {
  String t = String(rtc.getYear()) + "-";
  if(rtc.getMonth()+1&lt;10) t+="0"; t+=String(rtc.getMonth()+1)+"-";
  if(rtc.getDay()&lt;10) t+="0"; t+=String(rtc.getDay())+"_";
  if(rtc.getHour(true)&lt;10) t+="0"; t+=String(rtc.getHour(true))+":";
  if(rtc.getMinute()&lt;10) t+="0"; t+=String(rtc.getMinute())+":";
  if(rtc.getSecond()&lt;10) t+="0"; t+=String(rtc.getSecond());
  return t;
}