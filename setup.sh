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
  grep -q '^\[ounce\]' "$DEVKITPRO/pacman/etc/pacman.conf" || \
    printf '\n[ounce]\nServer = https://pkg.ouncebrew.org\n' | $SUDO tee -a "$DEVKITPRO/pacman/etc/pacman.conf"
  $SUDO dkp-pacman -Sy --noconfirm --needed switch-glad switch-zlib libounce ouncevk libkaihen
fi
# Entorno de Switch 2 (compiladores, rutas de libounce/OunceVK)
source "$DEVKITPRO/switch2vars.sh"

mkdir -p third_party/lib third_party/imgui_backend

# 1) Núcleo Beetle PSX HW (GPLv2): Vulkan + OpenGL, PGXP, escalado interno, CHD
if [ ! -d third_party/beetle-psx ]; then
  git clone --depth 1 https://github.com/libretro/beetle-psx-libretro.git third_party/beetle-psx
fi
# Su plataforma "libnx" apunta a libnx de Switch 1: la redirigimos a libounce
sed -i 's|$(DEVKITPRO)/libnx/switch_rules|$(DEVKITPRO)/libounce/switch2_rules|g; s|$(DEVKITPRO)/libnx|$(DEVKITPRO)/libounce|g' \
  third_party/beetle-psx/Makefile
# Rutas de cabeceras de OunceVK (GL/gl.h, EGL, Vulkan) y de libounce
EXTRA_INC=""
for h in GL/gl.h EGL/egl.h vulkan/vulkan.h switch.h; do
  f=$(find "$DEVKITPRO" -path "*/include/$h" 2>/dev/null | grep -v '/libnx/' | head -1 || true)
  echo "cabecera $h -> ${f:-NO ENCONTRADA}"
  [ -n "$f" ] && EXTRA_INC="$EXTRA_INC -I${f%/$h}"
done
export CFLAGS="${CFLAGS:-} $EXTRA_INC -D__SWITCH__"
export CXXFLAGS="${CXXFLAGS:-} $EXTRA_INC -D__SWITCH__"
echo "EXTRA_INC=$EXTRA_INC"
make -C third_party/beetle-psx clean >/dev/null 2>&1 || true
make -C third_party/beetle-psx -j"$(nproc)" \
     platform=libnx HAVE_HW=1 HAVE_OPENGL=1 HAVE_VULKAN=1 HAVE_CHD=1 HAVE_LIGHTREC=0 STATIC_LINKING=1
cp third_party/beetle-psx/mednafen_psx_hw_libretro_libnx.a third_party/lib/libbeetlepsx.a

# 2) Interfaz: Dear ImGui (MIT) con backends OpenGL3 y Vulkan
if [ ! -d third_party/imgui ]; then
  git clone --depth 1 --branch v1.91.5 https://github.com/ocornut/imgui.git third_party/imgui
fi
for f in imgui_impl_opengl3.h imgui_impl_opengl3.cpp imgui_impl_opengl3_loader.h imgui_impl_vulkan.h imgui_impl_vulkan.cpp; do
  cp third_party/imgui/backends/$f third_party/imgui_backend/
done

echo "Listo. Ahora ejecuta: make dist"
