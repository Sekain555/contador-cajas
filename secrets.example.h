// secrets.example.h: plantilla SIN datos reales. Este archivo sí puede ir al repositorio.
// Copia este archivo como secrets.h, completa los datos y agrega secrets.h al .gitignore.

#pragma once

#define WIFI_SSID "nombre-de-la-red-del-packing"
#define WIFI_PASS "clave-de-la-red"

// Red propia que crea el ESP32 si no logra conectarse (la clave debe tener al menos 8 caracteres).
#define AP_SSID "contador-cajas"
#define AP_PASS "cambia-esta-clave"