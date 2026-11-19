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

# 1) Núcleo Beetle PSX HW (GPLv2): Vulkan + OpenGL, PGXP, escalado interno, CHD
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
open(p, "w").write(s)
PY
fi

# Cabeceras: GL/EGL de OunceVK (preferimos portlibs/switch2) y libounce.
# Vulkan NO: el núcleo usa sus propias cabeceras Khronos (parallel-psx/khronos).
EXTRA_INC="-I$PWD/compat"
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
     platform=switch2 HAVE_HW=1 HAVE_OPENGL=1 HAVE_VULKAN=1 HAVE_CHD=1 HAVE_LIGHTREC=0
cp third_party/beetle-psx/mednafen_psx_hw_libretro_switch2.a third_party/lib/libbeetlepsx.a

# 2) Interfaz: Dear ImGui (MIT) con backends OpenGL3 y Vulkan
if [ ! -d third_party/imgui ]; then
  git clone --depth 1 --branch v1.91.5 https://github.com/ocornut/imgui.git third_party/imgui
fi
for f in imgui_impl_opengl3.h imgui_impl_opengl3.cpp imgui_impl_opengl3_loader.h imgui_impl_vulkan.h imgui_impl_vulkan.cpp; do
  cp third_party/imgui/backends/$f third_party/imgui_backend/
done

echo "Listo. Ahora ejecuta: make dist"
