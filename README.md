# Smart Bin — Cesto de Residuos Inteligente (ESP32)

Prototipo IoT desarrollado para el **Parcial 2** de la materia **PP: Integración Tecnológica (S0D53)** — Analista de Sistemas, Escuela Da Vinci.

* **Estudiante:** Ricardo Gabriel Díaz — Comisión `ACM5AV`
* **Docente:** Sebastián Suárez Cores
* **Simulador Wokwi en vivo:** https://wokwi.com/projects/481600182431364097
* **Informe Técnico (Landing Page):** https://rgabrieldiaz.github.io/smart-bin/

---

## Qué resuelve el sistema

1. **Apertura sin contacto:** Un sensor ultrasónico (`HC-SR04` externo) detecta la mano a `<= 20 cm`, acciona un servomotor `SG90` (`0°` a `90°`) con aviso sonoro y cierra automáticamente tras 4 segundos.
2. **Medición de llenado y semáforo local:** Un segundo `HC-SR04` interno mide la distancia a los residuos (`40 cm` vacío a `5 cm` lleno), calcula el porcentaje en tiempo real y lo muestra en una pantalla `LCD 16x2 I2C` junto a un semáforo de 3 LEDs (Verde `< 60%`, Amarillo `60–84%`, Rojo `>= 85%`).
3. **Conectividad IoT (Telegram):** El `ESP32` se conecta a `Wokwi-GUEST` y envía notificaciones HTTPS POST a un Bot de Telegram usando **detección de flanco** (un único aviso por cambio de estado, sin spam).
4. **Protocolos de seguridad y fallas simuladas:**
   * **Bloqueo anti-desborde (`>= 95%`):** Impide la apertura automática cuando el contenedor está lleno.
   * **Falla de sensor:** Activando el switch izquierdo (`sw_sensor`), el cesto pasa a `MODO SEGURO`, cierra la tapa, enciende LED rojo, emite 3 pitidos y alerta por Telegram.
   * **Falla de actuador:** Activando el switch derecho (`sw_tapa`) al solicitar apertura, detecta que la tapa no respondió, bloquea el servo para no forzarlo y notifica por Telegram.

---

## Estructura del repositorio

* `index.html` — Informe técnico completo en formato Landing Page (imprimible a PDF con `Ctrl + P`), incluyendo Gantt, Diagrama de Bloques, Wire Diagram, Cálculo de Consumo (`P = V × I`), Presupuesto en ARS, Hardware/Software y Plantilla de Escenarios de Falla.
* `sketch.ino` — Firmware completo y comentado para ESP32.
* `diagram.json` — Esquema de conexiones Wokwi respetando protoboard y código de colores (`Rojo = 5V`, `Negro = GND`, resistencias de `220 Ω`).
* `libraries.txt` — Dependencias de compilación (`ESP32Servo`, `LiquidCrystal I2C`).
* `assets/circuito_wokwi.png` — Captura verificada del circuito en funcionamiento.

---

## Configuración de Telegram (seguridad)

Por buenas prácticas de seguridad, las credenciales reales del Bot no se versionan en GitHub. Para probar el envío real en Wokwi, reemplazar en `sketch.ino`:

```cpp
const char* TELEGRAM_TOKEN   = "PEGAR_TOKEN_AQUI";
const char* TELEGRAM_CHAT_ID = "PEGAR_CHAT_ID_AQUI";
```

---

## Relevamiento y Evolución Futura (Feedback de Cátedra)

A partir de la devolución académica y el intercambio técnico con el docente, se relevó un nuevo requerimiento operativo para el backlog de mejoras del sistema:

### Modo Bloqueo de Tapa / Lock Remoto (Protección Mascotas y Niños)
* **Problema que resuelve:** En entornos hogareños u oficinas donde habitan mascotas (o niños pequeños), la detección por proximidad ultrasónica puede activarse de forma involuntaria, o los animales pueden intentar acceder a restos de comida durante la noche o en momentos de ausencia del usuario.
* **Control bidireccional IoT vía Telegram:**
  * Comando `/lock`: Envía una instrucción al ESP32 para forzar el servomotor a 0° (tapa cerrada) y deshabilitar temporalmente la lectura del sensor ultrasónico externo de proximidad.
  * Comando `/unlock`: Restaura la operación normal y habilita la apertura automática por proximidad.
  * Comando `/status`: Reporta en tiempo real el porcentaje de llenado y el estado de traba (`OPERATIVO` / `BLOQUEADO`).
* **Arquitectura de estados prevista (FSM):**
  * Incorporación del estado `ESTADO_BLOQUEADO` en la Máquina de Estados Finitos del firmware.
  * Feedback visual en el display `LCD 16x2` (ej. `L1: Smart Bin [LOCK]`, `L2: Nivel: XX% [TRABADO]`) y señalización local.
* **Alcance:** Requerimiento formalmente relevado en fase de especificación para futuras versiones; el código base actual de Wokwi se conserva estable con los entregables calificados del Parcial 2.

