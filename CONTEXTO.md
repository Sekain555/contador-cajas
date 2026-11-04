# CONTEXTO: Contador de cajas (Rucaray)

> Documento vivo. Se actualiza al cerrar cada card. Borrador inicial: 2026-11-04.

## Qué es y para quién
Contador de cajas en la línea de embalaje de Rucaray. Muestra, en una página web servida por el propio ESP32, cuántas cajas han pasado. Los inspectores del SAG lo usan para contrastar las cajas embaladas con las cajas que deben revisar según la PreSIF (Sistema de Instructivos Rucaray).

Solo visualiza. No guarda historial por hora, no calcula cajas por minuto y no emite alertas (decisiones del desarrollador: sin NTP, sin alerta de haz tapado).

## Hardware actual
- **Placa:** ESP32-WROOM-32, 30 pines. En el Arduino IDE: «ESP32 Dev Module».
- **Sensores:** 2 × Datalogic S60-PA-5-B01-PP. Retrorreflectivo polarizado con reflector, PNP, 10 a 30 V, salida NO (dark-on): con la caja cortando el haz, la salida sube a ~24 V.
- **Aislamiento:** 2 × 4N26. LED del lado de 24 V con R de 2,2 kΩ; fototransistor del lado del ESP32 con el pull-up interno. Pendiente: cambiar R1 y R2 a 1/2 W (hoy disipan 79 a 94 % de 1/4 W).
- **Alimentación:** fuente de 24 V para sensores y fuente de 5 V al pin VIN del ESP32. El 0 V de 24 V y el GND de 5 V son redes independientes (es el aislamiento).
- **Reset:** botón RST del joystick entre GPIO 27 y GND.
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

## Firmware: v1.0.0 (línea base)
- Tarea de conteo en el núcleo 1, una muestra por milisegundo, prioridad 2. Cada cambio de nivel debe durar `CONFIRMA_MS` = 5 ms. Bloqueo tras contar `DEBOUNCE_MS` = 30 ms; botón RST 250 ms.
- Páginas: `/` (conteo grande) y `/api/count` (`{"count":N}`). Biblioteca `WebServer` del core, sin librerías de terceros.
- Red: modo estación; si no conecta en 15 s al arrancar, crea una red propia de respaldo. Nombre mDNS `contador-cajas`.
- Credenciales en `secrets.h`, fuera del repositorio; la plantilla es `secrets.example.h`.
- Estado: pasó las pruebas de estabilidad (reposo de 5 minutos con la fuente de 5 V sola).

### Línea base de memoria (v1.0.0, compilación del IDE)
- Flash: 876.504 de 1.310.720 bytes (66 %). El tope es una de las dos ranuras de aplicación de la tabla de particiones por defecto (0x140000 cada una), que es lo que permite OTA.
- Variables globales: 45.672 de 327.680 bytes (13 %).
- Heap y pila en ejecución: se miden en la Fase 0 (línea `[mem]` del monitor serie).

## Decisiones vigentes
- Hardware limitado en memoria, sin memoria externa: código y funciones cuidadosos con la RAM y la flash. Medir después de cada fase.
- Conteo persistente; el reinicio es solo manual (botón físico y, más adelante, web con PIN).
- Conteo por sensor, con el total destacado. Estado de cada sensor en la página.
- Estado de conexión discreto en la página. La información técnica va aparte, en `/diagnostico`.
- Red de respaldo que aparece solo si cae la red principal.
- OTA con contraseña, si no retrasa el proyecto.
- Estilo Rucaray con los colores del logo (el desarrollador los extrae y se asignan funciones juntos).
- Valores por defecto acordados: guardado del conteo cada 10 s como máximo y solo si cambió; conteo al llegar la caja con rearme por haz libre estable (`LIBRE_MS`, por definir con la velocidad de la cinta y el hueco mínimo entre cajas).

## Fases
| Fase | Contenido | Versión |
|---|---|---|
| 0 | Línea base de memoria, `send_P` | PATCH (v1.0.1) |
| 1 | Persistencia, conteo por sensor, reset web con PIN | MINOR |
| 2 | Modelo de conteo y estado de sensores | MINOR |
| 3 | Red de respaldo automática, `/diagnostico`, OTA | MINOR |
| 4 | Estilo Rucaray | MINOR |

## Pendientes
- R1 y R2 a 1/2 W.
- Soldar o usar borneras en los empalmes de señal.
- MAC autorizada en la red de Rucaray y reserva de IP (TI). El ESP32 solo trabaja en 2,4 GHz y, con el código actual, solo con WPA2 de clave compartida.
- Logo (SVG si existe) y lista de colores.
- Velocidad de la cinta y hueco mínimo entre cajas, para fijar `LIBRE_MS`.

## Versión instalada en la línea
(Completar al cargar cada versión: versión, fecha, quién la cargó, cómo.)
