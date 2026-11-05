// contador_esp32.ino
// Contador de cajas para Rucaray: ESP32-WROOM-32 (30 pines).
// Lee los dos 4N26 (colector a GPIO con pull-up) y el botón RST, y sirve el conteo por web.
// Reemplaza al Arduino Nano y al CrowPanel. Placa en el IDE: "ESP32 Dev Module".

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <esp_random.h>
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
const uint32_t GUARDADO_MS = 10000;    // el conteo se guarda como máximo una vez cada 10 s

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

volatile uint32_t conteoS1 = 0;  // cajas contadas por el sensor 1; lo escribe solo la tarea de conteo
volatile uint32_t conteoS2 = 0;  // cajas contadas por el sensor 2; lo escribe solo la tarea de conteo
WebServer servidor(80);
TaskHandle_t hConteo = nullptr;   // para medir la pila libre de la tarea de conteo
volatile bool guardarYa = false;  // la tarea de conteo la sube al reiniciar; loop() la baja al guardar
volatile bool pedirReset = false;  // la sube la web al pedir un reinicio; solo la tarea de conteo la baja y reinicia
Preferences prefs;                // libreta (NVS) donde se anota el conteo
uint32_t guardadoS1 = 0;          // últimos valores anotados en la libreta
uint32_t guardadoS2 = 0;
uint32_t ultimoGuardado = 0;  // instante del último intento de guardado

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
    if (flancoDeBajada(s1, ahora, DEBOUNCE_MS)) conteoS1 = conteoS1 + 1;
    if (flancoDeBajada(s2, ahora, DEBOUNCE_MS)) conteoS2 = conteoS2 + 1;
    if (flancoDeBajada(rst, ahora, DEBOUNCE_RST_MS) || pedirReset) {
      pedirReset = false;
      conteoS1 = 0;
      conteoS2 = 0;
      guardarYa = true;  // pide a loop() anotar el 0 en la libreta al instante
    }
    vTaskDelay(1);
  }
}

// ---------- Persistencia (NVS) ----------
// El conteo se anota en la "libreta" (NVS, memoria que sobrevive al apagado) como una sola línea:
// S1 en los 32 bits altos y S2 en los 32 bajos. O se escribe completa o no se escribe.
void guardarConteo(uint32_t c1, uint32_t c2) {
  uint64_t registro = ((uint64_t)c1 << 32) | c2;
  prefs.begin("contador", false);
  size_t escritos = prefs.putULong64("c", registro);
  prefs.end();
  ultimoGuardado = millis();  // aun si falla, se espera antes de reintentar
  if (escritos == 0) {
    Serial.println("[nvs] ERROR: no se pudo guardar el conteo");
    return;
  }
  guardadoS1 = c1;
  guardadoS2 = c2;
  Serial.printf("[nvs] guardado s1=%lu s2=%lu\n", (unsigned long)c1, (unsigned long)c2);
}

void restaurarConteo() {
  prefs.begin("contador", false);  // lectura y escritura: si aún no existe, se crea vacío sin dar error
  uint64_t registro = prefs.getULong64("c", 0);
  prefs.end();
  conteoS1 = (uint32_t)(registro >> 32);
  conteoS2 = (uint32_t)(registro & 0xFFFFFFFFULL);
  guardadoS1 = conteoS1;
  guardadoS2 = conteoS2;
  Serial.printf("[nvs] restaurado s1=%lu s2=%lu\n", (unsigned long)conteoS1, (unsigned long)conteoS2);
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
#d{display:flex;gap:2rem;justify-content:center;margin-top:.5rem;font-size:1.3rem;opacity:.85}#d b{font-weight:600}
#l{margin-top:1rem}#l input{font-size:1.5rem;width:9rem;text-align:center;padding:.4rem}#l button{font-size:1.2rem;padding:.4rem 1rem;margin-left:.5rem}
#r{margin-top:1.5rem}#r button{font-size:1rem;padding:.4rem .9rem;margin:.3rem .25rem 0}#q{font-size:1.1rem}
</style></head><body><main>
<h1>Cajas contadas</h1><div id="v" hidden><div id="n">-</div><div id="d"><span>S1 <b id="a">-</b></span><span>S2 <b id="b">-</b></span></div><div id="r"><button id="rs">Reiniciar conteo</button><div id="rc" hidden><div id="q"></div><button id="si">Sí, reiniciar</button><button id="no">Cancelar</button></div></div></div><div id="l" hidden><input id="pin" type="password" inputmode="numeric" autocomplete="off" placeholder="PIN"><button id="ir">Ingresar</button></div><div id="e" class="mal">Conectando</div>
</main><script>
const g=id=>document.getElementById(id),n=g('n'),e=g('e'),a=g('a'),b=g('b'),v=g('v'),l=g('l'),pin=g('pin');
let aviso='Ingresa el PIN',bloq=0;
function mostrar(ok){v.hidden=!ok;l.hidden=ok;}
const rs=g('rs'),rc=g('rc'),q=g('q');
function confirmar(si){rc.hidden=!si;rs.hidden=si;if(si)q.textContent='¿Reiniciar el conteo de '+n.textContent+' cajas a 0?';}
async function leer(){
  try{
    const r=await fetch('/api/count',{cache:'no-store'});
    if(r.status===401){
      mostrar(false);
      const s=Math.ceil((bloq-Date.now())/1000);
      if(s>0)aviso='Demasiados intentos. Espera '+s+' s';else if(bloq){bloq=0;aviso='Ingresa el PIN';}
      e.textContent=aviso;e.className=aviso==='Ingresa el PIN'?'':'mal';return;
    }
    const d=await r.json();
    mostrar(true);
    n.textContent=d.count;a.textContent=d.s1;b.textContent=d.s2;e.textContent='En línea';e.className='ok';
  }catch(x){e.textContent='Sin conexión con el contador';e.className='mal';}
}
async function entrar(){
  try{
    const r=await fetch('/api/login',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'pin='+encodeURIComponent(pin.value)});
    pin.value='';
    if(r.ok){aviso='Ingresa el PIN';leer();return;}
    const d=await r.json();
    if(r.status===429)bloq=Date.now()+d.wait*1000;
    aviso=r.status===429?'Demasiados intentos. Espera '+d.wait+' s':'PIN incorrecto';
    e.textContent=aviso;e.className='mal';
  }catch(x){e.textContent='Sin conexión con el contador';e.className='mal';}
}
g('ir').onclick=entrar;pin.onkeydown=ev=>{if(ev.key==='Enter')entrar();};
rs.onclick=()=>{confirmar(true);setTimeout(()=>confirmar(false),8000);};
g('no').onclick=()=>confirmar(false);
g('si').onclick=async()=>{
  confirmar(false);
  try{await fetch('/api/reset',{method:'POST'});leer();}
  catch(x){e.textContent='Sin conexión con el contador';e.className='mal';}
};
leer();setInterval(leer,1000);
</script></body></html>)rawliteral";

// ---------- Acceso con PIN y sesiones ----------
// Como la pulsera de un evento: el PIN se muestra una vez en la puerta y a cambio se recibe un código
// (cookie) que la página presenta en cada consulta. Las sesiones viven en RAM y se pierden al reiniciar.
const uint8_t MAX_SESIONES = 4;
const uint32_t SESION_MS = 12UL * 3600UL * 1000UL;  // 12 horas
const uint8_t MAX_INTENTOS = 5;
const uint32_t BLOQUEO_MS = 60000;  // bloqueo tras demasiados intentos fallidos
const uint8_t LARGO_TOKEN = 32;

struct Sesion {
  char token[LARGO_TOKEN + 1];
  uint32_t inicio;
  bool activa;
};

Sesion sesiones[MAX_SESIONES];
uint8_t intentosFallidos = 0;
bool bloqueado = false;
uint32_t bloqueoInicio = 0;

void responderJson(int codigo, const char *json) {
  servidor.sendHeader("Cache-Control", "no-store");
  servidor.send_P(codigo, "application/json", json);
}

// Compara dos textos de largo conocido sin cortar en la primera diferencia.
bool iguales(const char *a, const char *b, size_t n) {
  uint8_t dif = 0;
  for (size_t i = 0; i < n; i++) dif |= (uint8_t)(a[i] ^ b[i]);
  return dif == 0;
}

bool pinCorrecto(const String &pin) {
  size_t n = strlen(ACCESS_PIN);
  return pin.length() == n && iguales(pin.c_str(), ACCESS_PIN, n);
}

// true si la petición trae una pulsera (cookie sid) válida y vigente.
bool sesionValida() {
  String galleta = servidor.header("Cookie");
  const char *p = strstr(galleta.c_str(), "sid=");
  if (!p) return false;
  p += 4;
  if (strlen(p) < LARGO_TOKEN) return false;
  uint32_t ahora = millis();
  for (uint8_t i = 0; i < MAX_SESIONES; i++) {
    if (sesiones[i].activa && ahora - sesiones[i].inicio < SESION_MS && iguales(p, sesiones[i].token, LARGO_TOKEN)) return true;
  }
  return false;
}

// Crea una sesión en un lugar libre o vencido; si no hay, reemplaza la más antigua. Devuelve su posición.
uint8_t crearSesion() {
  uint32_t ahora = millis();
  uint8_t elegida = 0;
  uint32_t mayorEdad = 0;
  for (uint8_t i = 0; i < MAX_SESIONES; i++) {
    uint32_t edad = ahora - sesiones[i].inicio;
    if (!sesiones[i].activa || edad >= SESION_MS) {
      elegida = i;
      break;
    }
    if (edad >= mayorEdad) {
      mayorEdad = edad;
      elegida = i;
    }
  }
  for (uint8_t i = 0; i < LARGO_TOKEN; i += 8) snprintf(sesiones[elegida].token + i, 9, "%08lx", (unsigned long)esp_random());
  sesiones[elegida].inicio = ahora;
  sesiones[elegida].activa = true;
  return elegida;
}

void rutaLogin() {
  uint32_t ahora = millis();
  char r[48];
  if (bloqueado) {
    if (ahora - bloqueoInicio < BLOQUEO_MS) {
      snprintf(r, sizeof(r), "{\"ok\":false,\"wait\":%lu}", (unsigned long)((BLOQUEO_MS - (ahora - bloqueoInicio) + 999) / 1000));
      responderJson(429, r);
      return;
    }
    bloqueado = false;
    intentosFallidos = 0;
  }
  if (!pinCorrecto(servidor.arg("pin"))) {
    intentosFallidos++;
    Serial.printf("[acceso] PIN incorrecto (%u/%u)\n", (unsigned)intentosFallidos, (unsigned)MAX_INTENTOS);
    if (intentosFallidos >= MAX_INTENTOS) {
      bloqueado = true;
      bloqueoInicio = ahora;
      Serial.printf("[acceso] bloqueado %lu s por intentos fallidos\n", (unsigned long)(BLOQUEO_MS / 1000));
      responderJson(429, "{\"ok\":false,\"wait\":60}");
    } else {
      responderJson(401, "{\"ok\":false}");
    }
    return;
  }
  intentosFallidos = 0;
  uint8_t i = crearSesion();
  char cookie[96];
  snprintf(cookie, sizeof(cookie), "sid=%s; Path=/; Max-Age=%lu; HttpOnly; SameSite=Strict", sesiones[i].token, (unsigned long)(SESION_MS / 1000));
  servidor.sendHeader("Set-Cookie", cookie);
  Serial.println("[acceso] sesion iniciada");
  responderJson(200, "{\"ok\":true}");
}

void rutaRaiz() {
  servidor.sendHeader("Cache-Control", "no-store");
  servidor.send_P(200, "text/html; charset=utf-8", PAGINA);  // desde la flash, sin copiar la página al heap
}

// Reinicio pedido desde la página: solo con sesión válida. La web no toca los contadores: deja un papelito
// (pedirReset) y la tarea de conteo, que es la única que los escribe, hace el reinicio en su siguiente vuelta.
void rutaReset() {
  if (!sesionValida()) {
    responderJson(401, "{\"auth\":false}");
    return;
  }
  pedirReset = true;
  Serial.println("[acceso] reinicio pedido desde la pagina");
  responderJson(200, "{\"ok\":true}");
}

void rutaConteo() {
  if (!sesionValida()) {
    responderJson(401, "{\"auth\":false}");
    return;
  }
  uint32_t c1 = conteoS1;
  uint32_t c2 = conteoS2;
  char json[64];
  snprintf(json, sizeof(json), "{\"count\":%lu,\"s1\":%lu,\"s2\":%lu}",
           (unsigned long)(c1 + c2), (unsigned long)c1, (unsigned long)c2);
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

  // Recupera el conteo guardado antes de que arranque la tarea que lo escribe.
  restaurarConteo();

  // El conteo arranca antes que el WiFi, así que cuenta mientras la red se conecta.
  xTaskCreatePinnedToCore(tareaConteo, "conteo", 4096, nullptr, 2, &hConteo, 1);

  iniciarRed();
  servidor.on("/", rutaRaiz);
  servidor.on("/api/count", rutaConteo);
  servidor.on("/api/login", HTTP_POST, rutaLogin);
  servidor.on("/api/reset", HTTP_POST, rutaReset);
  static const char *cabeceras[] = { "Cookie" };  // el servidor solo guarda las cabeceras que se le piden
  servidor.collectHeaders(cabeceras, 1);
  servidor.begin();
}

void loop() {
  static uint32_t ultimoImpreso = 0xFFFFFFFF;
  servidor.handleClient();
  vigilarRed();

  uint32_t c1 = conteoS1;
  uint32_t c2 = conteoS2;
  uint32_t copia = c1 + c2;
  if (copia != ultimoImpreso) {  // misma salida que el Nano: una línea por cambio
    Serial.printf("{\"count\":%lu,\"s1\":%lu,\"s2\":%lu}\n", (unsigned long)copia, (unsigned long)c1, (unsigned long)c2);
    ultimoImpreso = copia;
  }

  // Guardado en NVS: al instante si se pidió (reset); si no, solo si cambió y como máximo cada 10 s.
  if (guardarYa) {
    guardarYa = false;  // primero se baja la bandera y después se leen los contadores
    guardarConteo(conteoS1, conteoS2);
  } else if ((c1 != guardadoS1 || c2 != guardadoS2) && millis() - ultimoGuardado >= GUARDADO_MS) {
    guardarConteo(c1, c2);
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
