# METODOLOGÍA: Contador de cajas (Rucaray)

> Borrador armado desde la metodología de SI y STR, adaptado a un proyecto con hardware. Los puntos marcados como *propuesta* no vienen de SI y requieren confirmación. Documento vivo.

## Kanban (Trello)
- Columnas, igual que SI: Backlog / En Progreso / En Revisión-Testing / Hecho-Done / Descartado-En Pausa.
- Una card a la vez.
- Formato de card: Título + Descripción (Prioridad Alta/Media/Baja | Tamaño S/M/L | Categoría) + Alcance (lista) + Dependencias + CA (Criterios de Aceptación).
- Categorías para este proyecto (*propuesta*): FW (firmware), WEB (página servida por el ESP32), HW (circuito y montaje), OPS (despliegue y red).
- Cierre de card con comentario estructurado: resultado, cambios (archivos y líneas), verificación (pruebas de humo), mediciones, versión.

## Git Flow simplificado
- `feature/nombre` → `dev` (PR con Squash & Merge) → `main` (solo releases) → tag SemVer.
- Crear la rama feature **antes** de escribir código (lección del incidente de stash en SI).
- Commits con prefijo `feat:`, `fix:`, `chore:`.
- Las ramas feature no se eliminan tras el merge (igual que SI; confirmar).
- Antes de crear un tag: confirmar que el Squash & Merge se completó y que `git --no-pager log --oneline` muestra el commit real.
- Tras un merge: verificar que no quedaron marcadores de conflicto: `Get-Content archivo | Select-String "<<<<<<<"`.
- `secrets.h` y cualquier credencial nunca van al repositorio. Antes del primer commit: `git status` no debe listar `secrets.h`.

## SemVer (*propuesta* de significado para firmware)
- **PATCH:** fix u optimización sin cambio de comportamiento visible. Se libera apenas está listo.
- **MINOR:** función nueva, o card agrupada completa. El PATCH vuelve a 0.
- **MAJOR:** cambia el uso diario ya instalado, o exige tocar el cableado.
- El firmware imprime su versión al arrancar (`VERSION` en el código). La versión impresa debe coincidir con el tag.

## Cambios en el código
- Cambios puntuales, con línea exacta y el porqué. No reescribir bloques que ya funcionan.
- Verificar el impacto de cada cambio en el código existente antes de proponerlo.
- Evaluar cualquier librería antes de incorporarla; para este proyecto se prefieren las que ya vienen en el core del ESP32.
- Ante dudas técnicas, consultar la documentación oficial antes de asumir.

## Memoria (específico de este proyecto)
- Después de cada card de firmware se registra: flash y globales de la compilación, y la línea `[mem]` (heap libre, mínimo, bloque máximo, pilas).
- Páginas estáticas con `send_P`; respuestas dinámicas con `snprintf` sobre un buffer fijo; sin recursos externos de internet.
- Si algo se acerca al límite (flash cerca de la ranura de 1.310.720 bytes o heap mínimo bajo), se discute antes de seguir.

## Pruebas y QA
- Cierre con pruebas de humo, no con baterías largas. Cada comprobación indica la acción, dónde mirar (monitor serie, página, multímetro) y el resultado esperado.
- *Propuesta:* las cards que cambian el comportamiento se prueban primero en banco (USB, con cables simulando cajas) y después con cajas reales en la línea. El desarrollador hace el QA.

## Despliegue del firmware
- Hoy es manual, por USB: VIN desconectado, placa «ESP32 Dev Module», monitor serie cerrado antes de subir.
- Con OTA (Fase 3) se podrá cargar por red, con contraseña, guardando el conteo antes de reiniciar.
- Registrar en CONTEXTO.md la versión instalada: versión, fecha y quién la cargó.

## Documentos vivos
- `CONTEXTO.md`: estado del hardware, firmware, decisiones y pendientes.
- `METODOLOGIA.md`: este documento.
- La documentación detallada vive en el vault de Obsidian, como archivos Markdown enlazados.
