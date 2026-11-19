# PSX-K2 — Emulador de PlayStation 1 para Switch 2 con kaiHEN (.nro)

Frontend propio sobre **ounce-devkit** (libounce + OunceVK) + núcleo **Beetle PSX HW** (Mednafen, GPLv2) enlazado de forma estática.
Interfaz en español con Dear ImGui, pensada para mando y pantalla táctil.

## Funciones

| Área | Qué incluye |
|---|---|
| Render | **Vulkan 1.4** (OunceVK, por defecto) o Software exacto con salida OpenGL |
| Resolución interna | 1x (240p), 2x, 4x (~1080p), 8x (1920 líneas, por encima de 4K) |
| Salida | Auto (1080p portátil / 2160p en dock), 720p, 1080p, 1440p, 2160p |
| Escaladores de salida | Vulkan: Nearest y Bilineal · OpenGL: Nearest, Bilineal, Sharp Bilinear, Bicúbico Catmull-Rom, CRT scanlines |
| Arreglo poligonal (PGXP) | Modo memoria / memoria + CPU, corrección de perspectiva de texturas, caché de vértices, NCLIP preciso, tolerancia 2D, hack de líneas |
| Texturas | Nearest, SABR, xBR, Bilineal, 3-point, JINC2, con exclusión de sprites y 2D (fondos de RE/FF nítidos) |
| Imagen | 32 bpp, sin dithering, MSAA 2x–8x, supersampling, suavizado de FMV, widescreen hack 16:9/16:10/21:9, recorte de overscan |
| CPU | Overclock de CPU, GTE y rasterizador; modo boost de Switch |
| CD | Síncrono / asíncrono / precarga en RAM; carga rápida 4x–8x; CUE/BIN, CHD, PBP, M3U, ISO, CCD, EXE |
| Guardado | Memory card en ranura 1 (.srm) y 2 (.mcr), compartidas o por juego, autoguardado y escritura atómica; 10 ranuras de estado por juego |
| Multidisco | Cambio de disco desde el menú o con − + X |
| Controles | DualShock analógico o digital, 2 jugadores, vibración, distribución por posición o Nintendo |
| Extras | Configuración global y **por juego**, avance rápido, capturas BMP, contador de FPS, log |

## Estructura en la microSD

```
sdmc:/switch/psx-k2/
├── psx-k2.nro
├── bios/              scph5501.bin · scph5500.bin · scph5502.bin
├── roms/              tus juegos (subcarpetas permitidas)
├── saves/
│   ├── memcards/      <juego>.srm (ranura 1) · .mcr (ranura 2)
│   └── states/        <juego>.state0 … state9
├── screenshots/
├── cheats/
├── config/
│   ├── psx-k2.ini     configuración global
│   └── games/         <juego>.ini (solo lo que cambia ese juego)
└── logs/psx-k2.log
```

## Compilar

```bash
export DEVKITPRO=/opt/devkitpro
./setup.sh       # instala ounce-devkit si falta, compila Beetle PSX HW (Vulkan+GL) y prepara ImGui
make dist        # deja todo listo en dist/ para copiar a la raíz de la microSD
```

También puedes compilar dentro de la imagen Docker de ounce-devkit
(`docker build -t ounce-devkit .` en su repo, luego `docker run --rm -v "$PWD:/build/psx-k2" -w /build/psx-k2 ounce-devkit sh -c "./setup.sh && make dist"`).

Pon un `icon.jpg` (256x256) en la raíz del proyecto si quieres icono en el Homebrew Menu.

## Uso

- Con kaiHEN activo, abre un juego de Switch 2 manteniendo **R** para entrar al Homebrew Menu y lanza PSX-K2.
- Atajos en juego (mantén **−**): **+** menú · **R** guardar estado · **L** cargar estado · **ZR** avance rápido · **Y** captura · **X** siguiente disco.
- **Salida de emergencia:** mantén **− y +** durante 2 segundos: guarda la memory card y vuelve al hbmenu, aunque no se vea nada en pantalla.
- Menú: cruceta/stick para moverte, **A** confirmar, **B** volver, **L/R** pestañas, **ZL/ZR** categorías de configuración, izquierda/derecha para cambiar valores.

## Ajustes recomendados para juegos 3D (p. ej. Resident Evil 2)

`config/games/Resident Evil 2 (USA) (Disc 1).ini` trae un ejemplo:
PGXP en **memoria + CPU**, corrección de perspectiva activada, 32 bpp sin dithering, 4x de resolución
y xBR solo en los modelos 3D. PGXP corrige la imprecisión del GTE de la PSX (coordenadas enteras sin
subpíxel), que es lo que hace temblar y deformarse los polígonos de las caras y los escenarios.

## Limitaciones conocidas (léelas)

- **DirectX 12 no es posible**: es una API exclusiva de Windows/Xbox. En Switch 2 las APIs reales son Vulkan y OpenGL (OunceVK).
- **Compila en GitHub Actions, pero no se ha probado en consola todavía.** La superficie Vulkan usa `VK_NN_vi_surface`
  sobre la `NWindow`; si OunceVK crea la superficie de otra forma, ajusta `VkPresenter::createDevice()`
  siguiendo `examples/` de ounce-devkit.
- **Renderer OpenGL del núcleo desactivado**: sus símbolos GL chocan con OunceVK al enlazar. El modo Software
  sigue disponible y se muestra con OpenGL y todos sus escaladores.
- **Vulkan**: escalado de salida por `vkCmdBlitImage` (nearest/bilineal). Los shaders Sharp/Bicúbico/CRT
  están en el backend OpenGL; portarlos a SPIR-V es el siguiente paso. Capturas de pantalla solo en OpenGL por ahora.
- **4K**: con Vulkan y salida automática se pide 3840x2160 en el dock. Si el sistema lo rechaza se usa 1080p (queda en el log).
- **Rendimiento**: la CPU de la PSX se emula con intérprete. kaiHEN ofrece una API de JIT (`libkaihen/JIT.md`),
  con la que se podría activar el recompilador Lightrec del núcleo; no está implementado aún.
- **Riesgo**: kaiHEN da a los homebrew acceso de escritura a la NAND. Este emulador solo escribe en
  `sdmc:/switch/psx-k2/`, pero usa kaiHEN bajo tu responsabilidad.
- Los valores de opciones corresponden a Beetle PSX HW actual. Si una versión cambia un nombre, el log lo
  mostrará y basta con ajustar la tabla en `config.cpp`.

## Legal

No incluye BIOS ni juegos. Usa volcados de tu propia consola y de tus propios discos.
Este proyecto es GPLv2 (por el núcleo Beetle PSX). No se basa en DuckStation porque su licencia actual no permite obras derivadas.
