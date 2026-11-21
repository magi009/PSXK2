#!/usr/bin/env bash
# Prepara las dependencias de PSX-K2 para Switch 2 (kaiHEN). Ejecutar en tu PC.
set -euo pipefail
cd "$(dirname "$0")"
# En CI (contenedor como root) no hay sudo
SUDO=""; [ "$(id -u)" -ne 0 ] && SUDO="sudo"
: "${DEVKITPRO:?Define DEVKITPRO, p.ej. export DEVKITPRO=/opt/devkitpro}"

# 0) ounce-devkit (libounce + OunceVK + libkaihen) desde el repositorio [ounce] de dkp-pacman
if [ ! -f "$DEVKITPRO/libounce/switch2_rules" ]; then
  echo "Instalando ounce-devkit..."
  curl -fsSLO https://pkg.ouncebrew.org/ounce.gpg
  $SUDO dkp-pacman-key --add ounce.gpg
  $SUDO dkp-pacman-key --lsign-key DFEAE7FA965D1E6C0D41D4D75DB9FDBFA4A4DE29
  # Solo sincronizamos [ounce]: los paquetes de devkitPro ya vienen en su imagen/instalación
  # (y pkg.devkitpro.org a veces rechaza a los servidores de CI con 403)
  CONF="$DEVKITPRO/pacman/etc/pacman.conf"
  awk '/^\[/{ if ($0 != "[options]") exit } { print }' "$CONF" > /tmp/ounce-pacman.conf
  printf '\n[ounce]\nServer = https://pkg.ouncebrew.org\n' >> /tmp/ounce-pacman.conf
  for try in 1 2 3; do
    $SUDO dkp-pacman --config /tmp/ounce-pacman.conf -Sy --noconfirm --needed libounce ouncevk libkaihen && break
    echo "reintento $try..."; sleep 15
  done
  [ -f "$DEVKITPRO/libounce/switch2_rules" ] || { echo "No se pudo instalar ounce-devkit"; exit 1; }
fi
# Entorno de Switch 2 (compiladores, rutas de libounce/OunceVK)
source "$DEVKITPRO/switch2vars.sh"

mkdir -p third_party/lib third_party/imgui_backend

# 1) Núcleo Beetle PSX HW (GPLv2): renderer Vulkan + software, PGXP, escalado interno, CHD
#    (su renderer OpenGL choca con los símbolos GL de OunceVK, así que se desactiva)
if [ ! -d third_party/beetle-psx ]; then
  git clone --depth 1 https://github.com/libretro/beetle-psx-libretro.git third_party/beetle-psx
fi
# Beetle no trae plataforma Switch: añadimos "switch2" antes del bloque de Windows
if ! grep -q 'platform), switch2' third_party/beetle-psx/Makefile; then
  python3 - <<'PY'
p = "third_party/beetle-psx/Makefile"
s = open(p).read()
block = open("compat/beetle-switch2.mk").read()
marker = "# Windows\nelse\n"
assert marker in s, "no se encontró el bloque de Windows en el Makefile de Beetle"
s = s.replace(marker, block + "\n" + marker, 1)
rule = "$(TARGET): $(OBJECTS)\nifeq ($(STATIC_LINKING), 1)"
assert rule in s, "no se encontró la regla de enlace de Beetle"
s = s.replace(rule, "$(TARGET): $(OBJECTS)\nifneq (,$(filter 1,$(STATIC_LINKING) $(STATIC_LINKING_AR)))", 1)
open(p, "w").write(s)
PY
fi

# Cabeceras: GL/EGL de OunceVK (preferimos portlibs/switch2) y libounce.
# Vulkan NO: el núcleo usa sus propias cabeceras Khronos (parallel-psx/khronos).
EXTRA_INC="-I$PWD/compat -include $PWD/compat/vk_compat.h"
for h in GL/gl.h EGL/egl.h KHR/khrplatform.h switch.h; do
  f=$( (find "$DEVKITPRO/portlibs/switch2" "$DEVKITPRO/libounce" -path "*/include/$h" 2>/dev/null; \
        find "$DEVKITPRO/portlibs/switch" -path "*/include/$h" 2>/dev/null) | head -1 || true)
  echo "cabecera $h -> ${f:-NO ENCONTRADA}"
  [ -n "$f" ] && EXTRA_INC="$EXTRA_INC -I${f%/$h}"
done
export CFLAGS="${CFLAGS:-} $EXTRA_INC"
export CXXFLAGS="${CXXFLAGS:-} $EXTRA_INC"
echo "EXTRA_INC=$EXTRA_INC"
make -C third_party/beetle-psx clean >/dev/null 2>&1 || true
make -C third_party/beetle-psx -j"$(nproc)" \
     platform=switch2 HAVE_HW=1 HAVE_OPENGL=0 HAVE_VULKAN=1 HAVE_CHD=1 HAVE_LIGHTREC=0
cp third_party/beetle-psx/mednafen_psx_hw_libretro_switch2.a third_party/lib/libbeetlepsx.a

# El núcleo declara punteros globales vkXxx (los rellena con get_instance_proc_addr del frontend)
# con los mismos nombres que las funciones reales de OunceVK (-lvulkan) que usa el frontend.
# Se renombran solo dentro del núcleo a rhi_vkXxx para que no choquen al enlazar.
aarch64-none-elf-nm third_party/beetle-psx/rhi/rhi_lib_vulkan.o \
  | awk '$2 ~ /^[BbDdCcSs]$/ && $3 ~ /^vk[A-Z]/ { print $3 " rhi_" $3 }' | sort -u > third_party/vk-rename.txt
echo "símbolos Vulkan renombrados en el núcleo: $(wc -l < third_party/vk-rename.txt)"
aarch64-none-elf-objcopy --redefine-syms=third_party/vk-rename.txt third_party/lib/libbeetlepsx.a

# 2) Interfaz: Dear ImGui (MIT) con backends OpenGL3 y Vulkan
if [ ! -d third_party/imgui ]; then
  git clone --depth 1 --branch v1.91.5 https://github.com/ocornut/imgui.git third_party/imgui
fi
for f in imgui_impl_opengl3.h imgui_impl_opengl3.cpp imgui_impl_opengl3_loader.h imgui_impl_vulkan.h imgui_impl_vulkan.cpp; do
  cp third_party/imgui/backends/$f third_party/imgui_backend/
done

# 3) stb_image (dominio público) para decodificar portadas PNG/JPG
if [ ! -d third_party/stb ]; then
  git clone --depth 1 https://github.com/nothings/stb.git third_party/stb
fi

# 4) Shaders de antialiasing (GLSL -> SPIR-V embebido en cabeceras)
command -v glslangValidator >/dev/null || { echo "Falta glslangValidator (apt install glslang-tools)"; exit 1; }
mkdir -p third_party/gen
glslangValidator -V --vn spv_fullscreen_vert -o third_party/gen/spv_fullscreen_vert.h shaders/fullscreen.vert
glslangValidator -V --vn spv_present_frag   -o third_party/gen/spv_present_frag.h   shaders/present.frag
glslangValidator -V --vn spv_taa_frag       -o third_party/gen/spv_taa_frag.h       shaders/taa.frag
glslangValidator -V --vn spv_blend_frag     -o third_party/gen/spv_blend_frag.h     shaders/blend.frag
glslangValidator -V --vn spv_pixelscale_frag -o third_party/gen/spv_pixelscale_frag.h shaders/pixelscale.frag
for i in 0 1 2 3 4; do
    glslangValidator -V --vn spv_scalefx${i}_frag -o third_party/gen/spv_scalefx${i}_frag.h shaders/scalefx${i}.frag
done

echo "Listo. Ahora ejecuta: make dist"
