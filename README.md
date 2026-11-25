# Smart Bin IoT — Cesto de Residuos Inteligente (ESP32)

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
