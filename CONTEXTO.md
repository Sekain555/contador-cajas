# CONTEXTO: Contador de cajas (Rucaray)

> Documento vivo. Se actualiza al cerrar cada card. Última actualización: 2026-11-05 (Fase 1).

## Qué es y para quién
Contador de cajas en la línea de embalaje de Rucaray. Muestra, en una página web servida por el propio ESP32, cuántas cajas han pasado. Los inspectores del SAG lo usan para contrastar las cajas embaladas con las cajas que deben revisar según la PreSIF (Sistema de Instructivos Rucaray).

Solo visualiza. No guarda historial por hora, no calcula cajas por minuto y no emite alertas (decisiones del desarrollador: sin NTP, sin alerta de haz tapado).

## Hardware actual
- **Placa:** ESP32-WROOM-32, 30 pines. En el Arduino IDE: «ESP32 Dev Module».
- **Sensores:** 2 × Datalogic S60-PA-5-B01-PP. Retrorreflectivo polarizado con reflector, PNP, 10 a 30 V, salida NO (dark-on): con la caja cortando el haz, la salida sube a ~24 V.
- **Aislamiento:** 2 × 4N26. LED del lado de 24 V con R de 2,2 kΩ; fototransistor del lado del ESP32 con el pull-up interno. Pendiente: cambiar R1 y R2 a 1/2 W (hoy disipan 79 a 94 % de 1/4 W).
- **Alimentación:** fuente de 24 V para sensores y fuente de 5 V al pin VIN del ESP32. El 0 V de 24 V y el GND de 5 V son redes independientes (es el aislamiento).
- **Reset:** botón RST del joystick entre GPIO 27 y GND (verificado: deja el conteo en 0 y anota el 0 de inmediato). También se puede reiniciar desde la página, con sesión.
- **Montaje:** mini protoboard. Pendiente: pasar los empalmes de señal a soldadura o borneras (ya hubo falsos conteos por cables sueltos).

### Pines
| Función | GPIO | Motivo |
|---|---|---|
| Sensor 1 (colector del 4N26 nº 1) | 25 | Sin función de arranque ni de flash, con pull-up interno |
| Sensor 2 (colector del 4N26 nº 2) | 26 | Igual |
| Botón RST | 27 | Igual |

Se evitan los GPIO 0, 2, 5, 12 y 15 (arranque), 6 a 11 (flash) y 34 a 39 (solo entrada, sin pull-up). Ningún GPIO puede recibir más de 3,3 V.

### Redes esperadas (para verificar con multímetro, todo apagado)
| Red | Puntos unidos |
|---|---|
| +24 V | + de la fuente de 24 V, cable marrón de cada S60 |
| 0 V (24 V) | − de la fuente de 24 V, cable azul de cada S60, cátodo (pin 2) de cada 4N26 |
| Salida del sensor | cable negro (NO) de cada S60 → R → ánodo (pin 1) del 4N26 |
| GND (5 V) | − de la fuente de 5 V, GND del ESP32, emisor (pin 4) de cada 4N26, un terminal del RST |
| +5 V | + de la fuente de 5 V, solo al pin VIN |
| Señal | colector (pin 5) de cada 4N26 → GPIO 25 y GPIO 26 |
| Reset | otro terminal del RST → GPIO 27 |

Los pines 3 y 6 de los 4N26 quedan sin conexión. Entre las redes de 24 V y 5 V no debe haber continuidad.

### Reglas de seguridad
- VIN y USB nunca conectados a la vez.
- Conectar o mover cables solo con todo apagado.
- Para programar: USB al PC con VIN desconectado; si el IDE queda en «Connecting…», mantener BOOT.

## Firmware

### v1.0.0 (línea base)
- Tarea de conteo en el núcleo 1, una muestra por milisegundo, prioridad 2. Cada cambio de nivel debe durar `CONFIRMA_MS` = 5 ms. Bloqueo tras contar `DEBOUNCE_MS` = 30 ms; botón RST 250 ms.
- Páginas: `/` (conteo grande) y `/api/count` (`{"count":N}`). Biblioteca `WebServer` del core, sin librerías de terceros.
- Red: modo estación; si no conecta en 15 s al arrancar, crea una red propia de respaldo. Nombre mDNS `contador-cajas`.
- Credenciales en `secrets.h`, fuera del repositorio; la plantilla es `secrets.example.h`.
- Estado: pasó las pruebas de estabilidad (reposo de 5 minutos con la fuente de 5 V sola).

### v1.0.1 (Fase 0)
- Imprime la versión al arrancar: `Contador de cajas v1.0.1`.
- Línea `[mem]` cada 10 s en el monitor serie: `libre`, `minimo`, `bloque_max`, `pila_conteo` y `pila_loop`.
- La página y el JSON se envían con `send_P` y un buffer fijo (`snprintf`), sin `String`: la página ya no se copia al heap en cada solicitud.
- Sin cambios de comportamiento: `/`, `/api/count`, el conteo y el reset funcionan igual que en v1.0.0.

### Fase 1 (en `dev`, sin versión nueva asignada)
- **Contadores por sensor:** S1 y S2 son contadores independientes y el total es S1 + S2. Solo la tarea de conteo los escribe.
- **Persistencia (NVS):** un solo registro de 64 bits, con S1 en los 32 bits altos y S2 en los bajos. Se guarda solo si cambió y como máximo una vez cada 10 s (`GUARDADO_MS`), y de inmediato al reiniciar el conteo. Se restaura al arrancar. Un corte de energía pierde, como máximo, los conteos de los últimos 10 s. Mientras la flash escribe, las demás tareas se detienen un instante (lo documenta Espressif); en las pruebas el total subió de a 1 sin saltarse cajas.
- **Acceso con PIN:** la página pide un PIN (`ACCESS_PIN`, en `secrets.h`; la plantilla lleva un valor de ejemplo en `secrets.example.h`) antes de mostrar el conteo. Con el PIN correcto el servidor entrega una sesión: cookie `sid` con un token aleatorio de 128 bits, válida 12 h, hasta 4 a la vez (la más antigua se reemplaza). Cinco intentos fallidos bloquean el acceso 60 s. Las sesiones viven en RAM y se pierden al reiniciar el ESP32.
- **Rutas:** `/` (pública: formulario de PIN y conteo), `GET /api/count` (con sesión; devuelve `{"count":N,"s1":a,"s2":b}`), `POST /api/login` (PIN en el cuerpo; entrega la cookie) y `POST /api/reset` (con sesión). Sin sesión válida, `/api/count` y `/api/reset` responden 401.
- **Reinicio desde la página:** botón «Reiniciar conteo» dentro de la vista con sesión, con confirmación en dos pasos (muestra el total actual y se cancela sola a los 8 s) y sin pedir el PIN otra vez. La web no toca los contadores: levanta `pedirReset`, y la tarea de conteo hace el reinicio y pide guardar el 0 al instante. El botón físico RST hace lo mismo.
- **Monitor serie:** `{"count":N,"s1":a,"s2":b}` en cada cambio, más las líneas `[nvs]`, `[acceso]` y `[mem]`.
- **Versión:** `VERSION` sigue en `1.0.1`. El cambio de versión y los tags se hacen en el release final, al terminar todas las fases.
- **Límites conocidos:** no hay HTTPS, así que el PIN y la cookie viajan sin cifrar dentro de la red interna. El bloqueo es global: quien falle 5 veces bloquea a todos por 60 s. El diseño de la página (título y formulario) se ve sin PIN; lo protegido son los datos.
- **Verificado en banco** con las pruebas de humo de cada tanda: conteo por sensor, persistencia, PIN, bloqueo, sesiones y reinicio (página y botón físico).

### Memoria (medida en la Fase 0, v1.0.1)
- **Compilación:** flash 962.064 de 1.310.720 bytes (73 %), quedan 348.656 B; variables globales 49.488 de 327.680 bytes (15 %). El tope de flash es una de las dos ranuras de aplicación de la tabla de particiones por defecto (0x140000 cada una), que es lo que permite OTA.
- **Antes del cambio a `send_P`:** flash 962.084 B y globales 49.488 B.
- **Base de v1.0.0:** la compilación del v1.0.0 sin cambios no se midió. Los 876.504 B (66 %) y 45.672 B (13 %) que figuraban como línea base correspondían a otro sketch y no valen. La referencia de comparación para las fases siguientes es v1.0.1.
- **En ejecución** (WiFi conectado, 20 recargas de la página desde un celular), en bytes. A = reposo 30 s tras presionar EN, sin celular. B = 30 s después de las 20 recargas y de cerrar la pestaña. Las pilas son los bytes libres en el punto más justo.

| Valor | Con `send` (A → B) | Con `send_P` (A → B) |
|---|---|---|
| `libre` | 218.240 → 214.356 | 214.272 → 214.484 |
| `minimo` | 190.464 → 185.348 | 200.684 → 192.964 |
| `bloque_max` | 110.580 → 110.580 | 110.580 → 110.580 |
| `pila_conteo` | 3.552 → 3.552 | 3.552 → 3.552 |
| `pila_loop` | 5.852 → 5.596 | 5.532 → 5.532 |

- **Lectura:** con `send_P` el pico transitorio al recargar bajó de ~29 KB a ~21,5 KB. No hay señal de fuga ni de fragmentación. La tarea de conteo usa ~0,5 KB de su pila de 4.096 B, y `loop()` ~2,7 KB de su pila (8 KB por defecto).
- **Observación sin decidir:** el `libre` en reposo difirió ~4 KB entre dos mediciones (218.240 y 214.272), con causa sin determinar.

### Registro de compilaciones
Valores de la compilación del IDE (flash de 1.310.720 B por ranura, globales de 327.680 B). «Δ» es el cambio respecto de la fila anterior.

| # | Hito | Cambios que incluye | Flash (B) | % ranura | Δ flash | Globales (B) | Δ globales |
|---|---|---|---|---|---|---|---|
| 1 | Fase 0, tanda 1 | v1.0.0 más `VERSION` y la línea `[mem]`, con `send` | 962.084 | 73 % | n/a | 49.488 | n/a |
| 2 | Fase 0, tanda 2 | Más `send_P` y el JSON con `snprintf`, sin `String` | 962.064 | 73 % | −20 | 49.488 | 0 |
| 3 | Fase 1, tanda 1 | Más contadores S1 y S2, JSON con `s1` y `s2`, y página con S1 y S2 | 962.432 | 73 % | +368 | 49.488 | 0 |
| 4 | Fase 1, tanda 2 | Más persistencia en NVS: guardado cada 10 s, restauración al arrancar y reset anotado | 963.664 | 73 % | +1.232 | 49.512 | +24 |
| 5 | Fase 1, tanda 3 | Más acceso con PIN: sesiones con cookie, bloqueo tras 5 intentos y formulario de PIN | 966.860 | 73 % | +3.196 | 49.712 | +200 |
| 6 | Fase 1, tanda 4 | Más reinicio desde la página: `POST /api/reset` con sesión, `pedirReset` y botón con confirmación en dos pasos | 967.860 | 73 % | +1.000 | 49.712 | 0 |

- **Estado actual:** flash 967.860 B (73 %), quedan 342.860 B en la ranura; globales 49.712 B (15 %).
- **Fase 1 contra la referencia v1.0.1 (fila 2):** +5.796 B de flash y +224 B de RAM.
- **En ejecución, Fase 1:** una lectura tras las pruebas de la tanda 4: `libre=215.296`, `minimo=202.672`, `bloque_max=110.580`, `pila_conteo=3.552`, `pila_loop=5.660`. Es una sola lectura, tomada en otras condiciones que las de la Fase 0, así que no es comparable uno a uno con A y B. No muestra señales de problema: la pila de la tarea de conteo es la misma que en la Fase 0 y `bloque_max` no cambió.

## Decisiones vigentes
- Hardware limitado en memoria, sin memoria externa: código y funciones cuidadosos con la RAM y la flash. Medir después de cada fase.
- Conteo persistente; el reinicio es solo manual (botón físico y página, con sesión).
- Acceso restringido a los inspectores del SAG con un PIN único. Sesiones de 12 h (hasta 4) y bloqueo de 60 s tras 5 intentos fallidos. El reinicio desde la página no vuelve a pedir el PIN, pero sí una confirmación en dos pasos.
- Estrategia de ramas distinta a SI: cada card se integra a `dev` por PR; el merge a `main` y los tags de versión se hacen una sola vez, al terminar todas las fases. `main` queda en `v1.0.0` hasta entonces.
- Conteo por sensor, con el total destacado (Fase 1). El estado de cada sensor en la página llega con la Fase 2.
- Estado de conexión discreto en la página. La información técnica va aparte, en `/diagnostico`.
- Red de respaldo que aparece solo si cae la red principal.
- OTA con contraseña, si no retrasa el proyecto.
- Estilo Rucaray con los colores del logo (el desarrollador los extrae y se asignan funciones juntos).
- Valores por defecto acordados: guardado del conteo cada 10 s como máximo y solo si cambió; conteo al llegar la caja con rearme por haz libre estable (`LIBRE_MS`, por definir con la velocidad de la cinta y el hueco mínimo entre cajas).

## Fases
| Fase | Contenido | Versión |
|---|---|---|
| 0 | Línea base de memoria, `send_P` | PATCH (v1.0.1) |
| 1 | Persistencia, conteo por sensor, acceso con PIN y reset desde la web | MINOR |
| 2 | Modelo de conteo y estado de sensores | MINOR |
| 3 | Red de respaldo automática, `/diagnostico`, OTA | MINOR |
| 4 | Estilo Rucaray | MINOR |

## Pendientes
- R1 y R2 a 1/2 W.
- Soldar o usar borneras en los empalmes de señal.
- MAC autorizada en la red de Rucaray y reserva de IP (TI). El ESP32 solo trabaja en 2,4 GHz y, con el código actual, solo con WPA2 de clave compartida.
- Logo (SVG si existe) y lista de colores.
- Velocidad de la cinta y hueco mínimo entre cajas, para fijar `LIBRE_MS`.
- PIN definitivo (6 dígitos o más) y cómo se comunica a los inspectores.
- Decidir el número de `VERSION` del release que reúna las fases (hoy `1.0.1`; el contenido de la Fase 1 es un cambio MINOR).

## Versión instalada en la línea
(Completar al cargar cada versión: versión, fecha, quién la cargó, cómo.)