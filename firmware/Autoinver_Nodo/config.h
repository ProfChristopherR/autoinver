/*
 * ╔═══════════════════════════════════════════════════════════════════════════╗
 * ║              AUTOINVER NODO v2.0 - ARCHIVO DE CONFIGURACIÓN              ║
 * ╚═══════════════════════════════════════════════════════════════════════════╝
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ═══════════════════════════════════════════════════════════════════════════════
// CONSTANTES DE COMPILACIÓN
// ═══════════════════════════════════════════════════════════════════════════════

// Descomentar para activar logs detallados de sensores
// #define DEBUG_SENSORS

// ═══════════════════════════════════════════════════════════════════════════════
// PINES - Heltec LoRa32 V2
// ═══════════════════════════════════════════════════════════════════════════════

// --- Sensores ---
#define PIN_DHT 25     // DHT22 - Temperatura/Humedad
#define DHT_TYPE DHT22 //
#define PIN_SOIL 33    // Humedad suelo capacitivo (ADC)
#define PIN_SHARP 32   // Sharp 2Y0A21 - Nivel agua (ADC)

// --- Actuadores ---
#define PIN_RELE 12       // Relé bomba 12V
#define CHANNEL_SERVO_A 0 // PCA9685 Canal 0 - Compuerta A
#define CHANNEL_SERVO_B 1 // PCA9685 Canal 1 - Compuerta B

// --- OLED Interno ---
#define PIN_OLED_SDA 4
#define PIN_OLED_SCL 15
#define PIN_OLED_RST 16
#define OLED_ADDR 0x3C

// --- Dimensiones OLED ---
#define SCREEN_W 128
#define SCREEN_H 64

// --- LoRa Interno (NO MODIFICAR) ---
#define PIN_LORA_SCK 5
#define PIN_LORA_MISO 19
#define PIN_LORA_MOSI 27
#define PIN_LORA_CS 18
#define PIN_LORA_RST 14
#define PIN_LORA_IRQ 26

// --- I2C PCA9685 ---
// El PCA9685 comparte el bus I2C del OLED (GPIO4/GPIO15).
// GPIO21 en Heltec LoRa32 V2 controla Vext y NO debe usarse como SDA.
#define PIN_SDA 4
#define PIN_SCL 15
#define PCA9685_ADDR 0x40

// ═══════════════════════════════════════════════════════════════════════════════
// CONFIGURACIÓN RADIO LoRa
// ═══════════════════════════════════════════════════════════════════════════════

#define LORA_FREQ 915E6 // Frecuencia 915 MHz (ISM Américas)
#define LORA_SF 7       // Spreading Factor 7
#define LORA_BW 125E3   // Bandwidth 125 kHz
#define LORA_CR 5       // Coding Rate 4/5
#define LORA_TXPOWER 17 // dBm

// ═══════════════════════════════════════════════════════════════════════════════
// INTERVALOS DE TIEMPO (ms)
// ═══════════════════════════════════════════════════════════════════════════════

#define SENSOR_INTERVAL_MS 2000 // Lectura sensores cada 2s
#define TX_INTERVAL_MS 5000     // Transmisión LoRa cada 5s
#define DISPLAY_INTERVAL_MS 500 // Refresh OLED cada 500ms
#define SYNC_TIMEOUT_MS 10000   // Timeout sync hora 10s

// ═══════════════════════════════════════════════════════════════════════════════
// PARÁMETROS DE CONTROL
// ═══════════════════════════════════════════════════════════════════════════════

#define TEMP_HYSTERESIS 1.5f      // °C de histeresis para compuertas
#define SOIL_HYSTERESIS 10.0f     // % de histeresis para riego
#define WATER_MIN_PCT 10.0f       // % mínimo de agua para permitir riego (corte de seguridad)
#define MANUAL_OVERRIDE_MS 600000 // 10 minutos de override manual
#define MAX_SYNC_ATTEMPTS 5       // Intentos máximos de sync hora

// ═══════════════════════════════════════════════════════════════════════════════
// SERVOS
// ═══════════════════════════════════════════════════════════════════════════════

#define SERVO_PULSE_MIN 150 // ~0.5ms @ 50Hz
#define SERVO_PULSE_MAX 600 // ~2.5ms @ 50Hz

// ═══════════════════════════════════════════════════════════════════════════════
// ZONA HORARIA
// ═══════════════════════════════════════════════════════════════════════════════

#define GMT_OFFSET_SEC (-4 * 3600) // GMT-4 (Chile continental)

// ═══════════════════════════════════════════════════════════════════════════════
// ESTRUCTURA DE CONFIGURACIÓN CALIBRABLE (persiste en NVS)
// ═══════════════════════════════════════════════════════════════════════════════

struct ConfigData {
  int sharpMinADC = 500;     // ADC con tanque lleno
  int sharpMaxADC = 3500;    // ADC con tanque vacío
  float tempThresh1 = 28.0f; // °C - Abrir compuerta A
  float tempThresh2 = 32.0f; // °C - Abrir compuerta B también
  float soilThresh = 40.0f;  // %  - Umbral riego
  int pumpDuration = 10000;  // ms - Duración máxima riego
};

extern ConfigData g_config;

#endif // CONFIG_H
