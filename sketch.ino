/*
 * ============================================================================
 * PROYECTO: Smart Bin IoT — Cesto de Residuos Inteligente con Deteccion de Fallas
 * MATERIA:  PP: Integracion Tecnologica (S0D53) — Parcial 2
 * ALUMNO:   Ricardo Gabriel Diaz (Legajo 29135452) — Comision ACM5AV
 * DOCENTE:  Sebastian Suarez Cores
 * ============================================================================
 * HARDWARE:
 *  - Plataforma: ESP32 DevKit v1 (Wi-Fi Wokwi-GUEST)
 *  - Sensor 1:   HC-SR04 Externo (apertura sin contacto, <= 20 cm)
 *  - Sensor 2:   HC-SR04 Interno (medicion de % de llenado del cesto)
 *  - Actuador 1: Servomotor SG90 (tapa: 0 deg cerrada / 90 deg abierta)
 *  - Actuador 2: Pantalla LCD 16x2 I2C (estado, % llenado y codigos de error)
 *  - Actuador 3: Semaforo de 3 LEDs (Verde, Amarillo, Rojo) con R=220 ohm
 *  - Actuador 4: Buzzer (avisos sonoros y alarmas de falla)
 *  - Fallas:     2 Slide Switches (inyeccion de falla de sensor y falla de tapa)
 * ============================================================================
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>

// ===================== CONFIGURACION WI-FI Y TELEGRAM =====================
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";
const int   WIFI_CANAL = 6;

// Reemplazar con credenciales reales antes de la demo (NO subir token publico)
const char* TELEGRAM_TOKEN   = "PEGAR_TOKEN_AQUI";
const char* TELEGRAM_CHAT_ID = "PEGAR_CHAT_ID_AQUI";
bool telegramHabilitado = false; // Se activa automaticamente si se carga un token valido

// ===================== DEFINICION DE PINES (ESP32) =====================
// Sensor 1: HC-SR04 Externo (mano del usuario)
const int PIN_TRIG_MANO  = 13;
const int PIN_ECHO_MANO  = 14;

// Sensor 2: HC-SR04 Interno (nivel de llenado)
const int PIN_TRIG_NIVEL = 26;
const int PIN_ECHO_NIVEL = 27;

// Actuadores
const int PIN_SERVO      = 18;
const int PIN_BUZZER     = 19;
const int PIN_LED_VERDE  = 25;
const int PIN_LED_AMAR   = 33;
const int PIN_LED_ROJO   = 32;

// Interruptores de simulacion de fallas (activos en LOW por INPUT_PULLUP)
const int PIN_SW_FALLA_SENSOR = 4;  // Simula sensor de llenado desconectado/invalido
const int PIN_SW_FALLA_TAPA   = 5;  // Simula servo de tapa trabado/sin respuesta

// ===================== PARAMETROS DEL CESTO =====================
const float DISTANCIA_APERTURA_CM = 20.0; // Abre la tapa si la mano esta a <= 20 cm
const float ALTURA_CESTO_VACIO_CM = 40.0; // Distancia sensor->fondo (0% lleno)
const float ALTURA_CESTO_LLENO_CM = 5.0;  // Distancia sensor->tope (100% lleno)
const int   UMBRAL_MEDIO_PCT      = 60;   // Amarillo desde 60%
const int   UMBRAL_LLENO_PCT      = 85;   // Rojo + Alerta Telegram desde 85%
const int   UMBRAL_BLOQUEO_PCT    = 95;   // Bloqueo anti-desborde desde 95%
const int   ANGULO_CERRADO        = 0;
const int   ANGULO_ABIERTO        = 90;
const unsigned long TIEMPO_TAPA_ABIERTA_MS = 4000;

// ===================== OBJETOS Y VARIABLES DE ESTADO =====================
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo servoTapa;

bool tapaAbierta = false;
unsigned long momentoApertura = 0;

// Banderas de deteccion de flanco (anti-spam para Telegram, segun guia de catedra)
bool alertaLlenoEnviada       = false;
bool alertaFallaSensorEnviada = false;
bool alertaFallaTapaEnviada   = false;
bool fallaTapaActiva          = false;

// ===================== FUNCIONES AUXILIARES =====================

// Convierte espacios a %20 para envio seguro por HTTP POST a Telegram
String codificarURL(const String& texto) {
  String out = "";
  for (size_t i = 0; i < texto.length(); i++) {
    char c = texto.charAt(i);
    if (c == ' ') out += "%20";
    else if (c == '\n') out += "%0A";
    else out += c;
  }
  return out;
}

// Envia notificacion por Telegram usando WiFiClientSecure + HTTPClient (guia catedra)
void enviarTelegram(const String& mensaje) {
  Serial.println("[TELEGRAM] Evento: " + mensaje);

  if (!telegramHabilitado) {
    Serial.println("[TELEGRAM] Modo demo local (cargar TOKEN y CHAT_ID para envio real).");
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[TELEGRAM] Sin conexion Wi-Fi. Operando en modo local autonomo.");
    return;
  }

  WiFiClientSecure cliente;
  cliente.setInsecure(); // Solo demostracion educativa (en prod validar certificado TLS)
  HTTPClient http;
  String url = String("https://api.telegram.org/bot") + TELEGRAM_TOKEN + "/sendMessage";

  http.begin(cliente, url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String datos = String("chat_id=") + TELEGRAM_CHAT_ID + "&text=" + codificarURL(mensaje);

  int codigoHTTP = http.POST(datos);
  Serial.printf("[TELEGRAM] Respuesta HTTP: %d\n", codigoHTTP);
  http.end();
}

// Lee distancia en cm de un sensor ultrasonico HC-SR04 con timeout de seguridad
float medirDistanciaCm(int pinTrig, int pinEcho) {
  digitalWrite(pinTrig, LOW);
  delayMicroseconds(2);
  digitalWrite(pinTrig, HIGH);
  delayMicroseconds(10);
  digitalWrite(pinTrig, LOW);

  // Timeout de 30 ms (~500 cm max) para evitar bloqueo del loop si el sensor falla
  unsigned long duracion = pulseIn(pinEcho, HIGH, 30000);
  if (duracion == 0) {
    return -1.0; // Codigo de error: sin eco / sensor desconectado
  }
  return (duracion * 0.0343) / 2.0;
}

// Emite pitidos cortos con el buzzer
void emitirPitidos(int cantidad, int duracionMs) {
  for (int i = 0; i < cantidad; i++) {
    tone(PIN_BUZZER, 1200, duracionMs);
    delay(duracionMs + 80);
  }
}

// Control del semaforo de 3 LEDs
void actualizarSemaforo(bool verde, bool amarillo, bool rojo) {
  digitalWrite(PIN_LED_VERDE, verde ? HIGH : LOW);
  digitalWrite(PIN_LED_AMAR,  amarillo ? HIGH : LOW);
  digitalWrite(PIN_LED_ROJO,  rojo ? HIGH : LOW);
}

// Muestra dos lineas limpias en el LCD 16x2
void mostrarLCD(const String& linea1, const String& linea2) {
  lcd.setCursor(0, 0);
  lcd.print(linea1);
  for (int i = linea1.length(); i < 16; i++) lcd.print(' ');

  lcd.setCursor(0, 1);
  lcd.print(linea2);
  for (int i = linea2.length(); i < 16; i++) lcd.print(' ');
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== INICIANDO SMART BIN IoT (ESP32) ===");

  // Configurar pines de sensores ultrasonicos
  pinMode(PIN_TRIG_MANO,  OUTPUT);
  pinMode(PIN_ECHO_MANO,  INPUT);
  pinMode(PIN_TRIG_NIVEL, OUTPUT);
  pinMode(PIN_ECHO_NIVEL, INPUT);

  // Configurar pines de actuadores
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_AMAR,  OUTPUT);
  pinMode(PIN_LED_ROJO,  OUTPUT);
  pinMode(PIN_BUZZER,    OUTPUT);

  // Configurar switches de simulacion de fallas
  pinMode(PIN_SW_FALLA_SENSOR, INPUT_PULLUP);
  pinMode(PIN_SW_FALLA_TAPA,   INPUT_PULLUP);

  // Inicializar LCD I2C (SDA=21, SCL=22 por defecto en ESP32)
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  mostrarLCD("Smart Bin IoT", "Iniciando...");

  // Inicializar Servo en posicion cerrada
  servoTapa.attach(PIN_SERVO, 500, 2400);
  servoTapa.write(ANGULO_CERRADO);

  // Verificar si se cargaron credenciales reales de Telegram
  if (String(TELEGRAM_TOKEN) != "PEGAR_TOKEN_AQUI" && String(TELEGRAM_CHAT_ID) != "PEGAR_CHAT_ID_AQUI") {
    telegramHabilitado = true;
  }

  // Conectar a Wi-Fi virtual Wokwi-GUEST (canal 6) con timeout de 15 segundos
  mostrarLCD("Conectando WiFi", "Wokwi-GUEST...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS, WIFI_CANAL);
  Serial.print("Conectando a Wokwi-GUEST");
  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 60) {
    delay(250);
    Serial.print(".");
    intentos++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("\nWi-Fi conectado! IP: ");
    Serial.println(WiFi.localIP());
    mostrarLCD("WiFi Conectado", WiFi.localIP().toString());
    enviarTelegram("Smart Bin IoT iniciado y conectado a la red.");
  } else {
    Serial.println("\nSin Wi-Fi. Iniciando en modo autonomo local.");
    mostrarLCD("Modo Local", "Sin red WiFi");
  }
  delay(1200);
}

// ===================== LOOP PRINCIPAL =====================
void loop() {
  // 1. Leer estado de interruptores de inyeccion de fallas (LOW = falla activa)
  bool simularFallaSensor = (digitalRead(PIN_SW_FALLA_SENSOR) == LOW);
  bool simularFallaTapa   = (digitalRead(PIN_SW_FALLA_TAPA)   == LOW);

  // 2. Medir distancia interna (llenado) y externa (mano)
  float distNivel = simularFallaSensor ? -1.0 : medirDistanciaCm(PIN_TRIG_NIVEL, PIN_ECHO_NIVEL);
  float distMano  = medirDistanciaCm(PIN_TRIG_MANO, PIN_ECHO_MANO);

  // ---------------- ESCENARIO DE FALLA 1: SENSOR DE LLENADO ----------------
  if (distNivel < 0 || distNivel > 400.0) {
    // Modo seguro: cerrar tapa, prender LED rojo, avisar en LCD y Telegram (1 sola vez)
    servoTapa.write(ANGULO_CERRADO);
    tapaAbierta = false;
    actualizarSemaforo(false, false, true);
    mostrarLCD("ERR-S01: SENSOR", "MODO SEGURO");
    Serial.println("[FALLA] ERR-S01: Sensor interno sin respuesta o lectura invalida.");

    if (!alertaFallaSensorEnviada) {
      emitirPitidos(3, 120);
      enviarTelegram("ALERTA ERR-S01: Falla en sensor de llenado. Cesto en Modo Seguro.");
      alertaFallaSensorEnviada = true;
    }
    delay(500);
    return;
  } else if (alertaFallaSensorEnviada) {
    // Recuperacion automatica del sensor
    alertaFallaSensorEnviada = false;
    enviarTelegram("INFO: Sensor de llenado restablecido correctamente.");
  }

  // 3. Calcular porcentaje de llenado (0% = 40 cm, 100% = 5 cm)
  float porcentajeFloat = ((ALTURA_CESTO_VACIO_CM - distNivel) / (ALTURA_CESTO_VACIO_CM - ALTURA_CESTO_LLENO_CM)) * 100.0;
  int porcentaje = constrain((int)round(porcentajeFloat), 0, 100);

  // ---------------- ESCENARIO DE FALLA 2: ACTUADOR (TAPA ATASCADA) ----------------
  if (!simularFallaTapa && fallaTapaActiva) {
    fallaTapaActiva = false;
    alertaFallaTapaEnviada = false;
    enviarTelegram("INFO: Actuador de tapa restablecido.");
  }

  if (fallaTapaActiva) {
    actualizarSemaforo(false, false, true);
    mostrarLCD("ERR-A01: TAPA", "REVISAR SERVO");
    delay(500);
    return;
  }

  // 4. Evaluar semaforo y alerta de recoleccion por Telegram (deteccion de flanco)
  if (porcentaje >= UMBRAL_LLENO_PCT) {
    actualizarSemaforo(false, false, true);
    if (!alertaLlenoEnviada) {
      emitirPitidos(2, 150);
      enviarTelegram("ALERTA RECOLECCION: Contenedor al " + String(porcentaje) + "% de capacidad.");
      alertaLlenoEnviada = true;
    }
  } else if (porcentaje >= UMBRAL_MEDIO_PCT) {
    actualizarSemaforo(false, true, false);
    if (alertaLlenoEnviada && porcentaje < 50) alertaLlenoEnviada = false;
  } else {
    actualizarSemaforo(true, false, false);
    if (alertaLlenoEnviada) {
      alertaLlenoEnviada = false;
      enviarTelegram("INFO: Contenedor vaciado (" + String(porcentaje) + "%). Disponible.");
    }
  }

  // 5. Logica de apertura automatica de tapa sin contacto
  if (!tapaAbierta && distMano > 0 && distMano <= DISTANCIA_APERTURA_CM) {
    if (porcentaje >= UMBRAL_BLOQUEO_PCT) {
      // Proteccion anti-desborde: no abre si esta al >= 95%
      mostrarLCD("LLENO: " + String(porcentaje) + "%", "BLOQUEO DESBORDE");
      emitirPitidos(2, 80);
      Serial.println("[SEGURIDAD] Apertura bloqueada: cesto lleno.");
    } else if (simularFallaTapa) {
      // Inyeccion de falla de actuador: intenta abrir pero el servo esta trabado
      fallaTapaActiva = true;
      actualizarSemaforo(false, false, true);
      mostrarLCD("ERR-A01: TAPA", "REVISAR SERVO");
      Serial.println("[FALLA] ERR-A01: El actuador de tapa no respondio a la orden de apertura.");
      if (!alertaFallaTapaEnviada) {
        emitirPitidos(3, 180);
        enviarTelegram("ALERTA ERR-A01: Tapa atascada o falla de servomotor.");
        alertaFallaTapaEnviada = true;
      }
      return;
    } else {
      // Funcionamiento normal: abrir tapa
      servoTapa.write(ANGULO_ABIERTO);
      tapaAbierta = true;
      momentoApertura = millis();
      tone(PIN_BUZZER, 1000, 100);
      Serial.println("[OK] Mano detectada -> Tapa ABIERTA.");
    }
  }

  // 6. Cierre automatico tras TIEMPO_TAPA_ABIERTA_MS (solo si la mano ya se retiro)
  if (tapaAbierta && (millis() - momentoApertura >= TIEMPO_TAPA_ABIERTA_MS)) {
    if (distMano < 0 || distMano > DISTANCIA_APERTURA_CM) {
      servoTapa.write(ANGULO_CERRADO);
      tapaAbierta = false;
      Serial.println("[OK] Tiempo cumplido -> Tapa CERRADA.");
    }
  }

  // 7. Actualizar display LCD y Monitor Serie
  String estadoTapa = tapaAbierta ? "Tapa: ABIERTA" : (porcentaje >= UMBRAL_BLOQUEO_PCT ? "LLENO - BLOQUEO" : "Tapa: CERRADA");
  mostrarLCD("Llenado: " + String(porcentaje) + "%", estadoTapa);

  Serial.print("Nivel: ");
  Serial.print(distNivel, 1);
  Serial.print(" cm (");
  Serial.print(porcentaje);
  Serial.print("%) | Mano: ");
  Serial.print(distMano, 1);
  Serial.print(" cm | ");
  Serial.println(estadoTapa);

  delay(300);
}
