// contador_esp32.ino
// Contador de cajas para Rucaray: ESP32-WROOM-32 (30 pines).
// Lee los dos 4N26 (colector a GPIO con pull-up) y el botón RST, y sirve el conteo por web.
// Reemplaza al Arduino Nano y al CrowPanel. Placa en el IDE: "ESP32 Dev Module".

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "secrets.h"  // WIFI_SSID, WIFI_PASS, AP_SSID, AP_PASS. Este archivo NO va al repositorio.

const char VERSION[] = "1.0.1";

// ---------- Pines ----------
// Sin función de arranque ni de flash, con pull-up interno. Evitar GPIO 0, 2, 5, 12, 15 (arranque),
// 6 a 11 (flash) y 34 a 39 (solo entrada, sin pull-up).
const uint8_t PIN_S1 = 25;   // colector del 4N26 número 1
const uint8_t PIN_S2 = 26;   // colector del 4N26 número 2
const uint8_t PIN_RST = 27;  // botón RST del joystick (entre el pin y GND)

// ---------- Ajustes de conteo ----------
const uint32_t CONFIRMA_MS = 5;        // todo cambio de nivel debe durar esto para ser válido
const uint32_t DEBOUNCE_MS = 30;       // bloqueo tras contar, igual que infrarrojo_data.ino
const uint32_t DEBOUNCE_RST_MS = 250;  // bloqueo del botón de reset

// ---------- Estado de cada entrada ----------
struct Canal {
  uint8_t pin;
  bool bajo;        // nivel filtrado: true = LOW confirmado (haz cortado o botón presionado)
  bool pendiente;   // hay una lectura distinta al nivel filtrado esperando confirmación
  uint32_t desde;   // instante en que empezó esa lectura distinta
  uint32_t ultimo;  // instante de la última bajada aceptada
};

Canal s1 = { PIN_S1, false, false, 0, 0 };
Canal s2 = { PIN_S2, false, false, 0, 0 };
Canal rst = { PIN_RST, false, false, 0, 0 };

volatile uint32_t conteo = 0;  // lo escribe solo la tarea de conteo
WebServer servidor(80);
TaskHandle_t hConteo = nullptr;  // para medir la pila libre de la tarea de conteo

// Devuelve true cuando se confirma una bajada válida (LOW estable CONFIRMA_MS y fuera del bloqueo).
bool flancoDeBajada(Canal &c, uint32_t ahora, uint32_t bloqueo) {
  bool lecturaBaja = (digitalRead(c.pin) == LOW);
  if (lecturaBaja == c.bajo) {
    c.pendiente = false;
    return false;
  }
  if (!c.pendiente) {
    c.pendiente = true;
    c.desde = ahora;
    return false;
  }
  if (ahora - c.desde < CONFIRMA_MS) return false;
  c.pendiente = false;
  c.bajo = lecturaBaja;
  if (c.bajo && (ahora - c.ultimo > bloqueo)) {
    c.ultimo = ahora;
    return true;
  }
  return false;
}

// Tarea de conteo: una muestra por milisegundo, en el núcleo 1 y con más prioridad que loop().
// El WiFi corre en el núcleo 0, así que la red no retrasa el conteo.
void tareaConteo(void *) {
  for (;;) {
    uint32_t ahora = millis();
    if (flancoDeBajada(s1, ahora, DEBOUNCE_MS)) conteo = conteo + 1;
    if (flancoDeBajada(s2, ahora, DEBOUNCE_MS)) conteo = conteo + 1;
    if (flancoDeBajada(rst, ahora, DEBOUNCE_RST_MS)) conteo = 0;
    vTaskDelay(1);
  }
}

// ---------- Página web ----------
const char PAGINA[] = R"rawliteral(<!DOCTYPE html>
<html lang="es"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Contador de cajas</title>
<style>
:root{color-scheme:light dark}
body{margin:0;min-height:100vh;display:grid;place-items:center;font-family:system-ui,sans-serif;text-align:center}
h1{font-size:1.2rem;font-weight:600;margin:0 0 .5rem}
#n{font-size:clamp(5rem,28vw,12rem);font-weight:700;line-height:1;font-variant-numeric:tabular-nums}
#e{margin-top:1rem;font-size:1rem}.ok{color:#1d8a4c}.mal{color:#c9372e}
</style></head><body><main>
<h1>Cajas contadas</h1><div id="n">-</div><div id="e" class="mal">Conectando</div>
</main><script>
const n=document.getElementById('n'),e=document.getElementById('e');
async function leer(){
  try{
    const r=await fetch('/api/count',{cache:'no-store'});
    const d=await r.json();
    n.textContent=d.count;e.textContent='En línea';e.className='ok';
  }catch(x){e.textContent='Sin conexión con el contador';e.className='mal';}
}
leer();setInterval(leer,1000);
</script></body></html>)rawliteral";

void rutaRaiz() {
  servidor.sendHeader("Cache-Control", "no-store");
  servidor.send_P(200, "text/html; charset=utf-8", PAGINA);  // desde la flash, sin copiar la página al heap
}

void rutaConteo() {
  uint32_t copia = conteo;
  char json[24];
  snprintf(json, sizeof(json), "{\"count\":%lu}", (unsigned long)copia);
  servidor.sendHeader("Cache-Control", "no-store");
  servidor.send_P(200, "application/json", json);  // acepta también un buffer en RAM, sin copiarlo a String
}

// ---------- Red ----------
void iniciarRed() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("contador-cajas");
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Conectando a WiFi");
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(250);
    Serial.print('.');
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nConectado. IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    // Respaldo: el ESP32 crea su propia red para poder ver el conteo igual.
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.printf("\nSin red. Punto de acceso '%s' en %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
  }
  if (MDNS.begin("contador-cajas")) Serial.println("Nombre: http://contador-cajas.local");
}

// Si estaba en modo estación y se cae la red, intenta reconectar cada 30 s.
void vigilarRed() {
  static uint32_t ultimoIntento = 0;
  if (WiFi.getMode() != WIFI_STA) return;
  if (millis() - ultimoIntento < 30000) return;
  ultimoIntento = millis();
  if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
}

// ---------- Arranque y bucle ----------
void setup() {
  Serial.begin(115200);
  Serial.printf("\nContador de cajas v%s\n", VERSION);
  pinMode(PIN_S1, INPUT_PULLUP);
  pinMode(PIN_S2, INPUT_PULLUP);
  pinMode(PIN_RST, INPUT_PULLUP);
  delay(50);  // deja estabilizar el pull-up

  // Parte desde el nivel real: una caja ya frente al sensor al energizar no se cuenta.
  s1.bajo = (digitalRead(PIN_S1) == LOW);
  s2.bajo = (digitalRead(PIN_S2) == LOW);
  rst.bajo = (digitalRead(PIN_RST) == LOW);

  // El conteo arranca antes que el WiFi, así que cuenta mientras la red se conecta.
  xTaskCreatePinnedToCore(tareaConteo, "conteo", 4096, nullptr, 2, &hConteo, 1);

  iniciarRed();
  servidor.on("/", rutaRaiz);
  servidor.on("/api/count", rutaConteo);
  servidor.begin();
}

void loop() {
  static uint32_t ultimoImpreso = 0xFFFFFFFF;
  servidor.handleClient();
  vigilarRed();

  uint32_t copia = conteo;
  if (copia != ultimoImpreso) {  // misma salida que el Nano: una línea por cambio
    Serial.printf("{\"count\":%lu}\n", (unsigned long)copia);
    ultimoImpreso = copia;
  }
  static uint32_t ultimoMem = 0;
  if (millis() - ultimoMem >= 10000) {  // línea de memoria cada 10 s
    ultimoMem = millis();
    Serial.printf("[mem] libre=%lu minimo=%lu bloque_max=%lu pila_conteo=%u pila_loop=%u\n",
                  (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap(),
                  (unsigned long)ESP.getMaxAllocHeap(),
                  (unsigned)uxTaskGetStackHighWaterMark(hConteo),
                  (unsigned)uxTaskGetStackHighWaterMark(NULL));
  }
  delay(2);
}
