# Plataforma Switch 2 (kaiHEN / libounce) para Beetle PSX HW — insertada por setup.sh
else ifeq ($(platform), switch2)
   TARGET := $(TARGET_NAME)_libretro_switch2.a
   # STATIC_LINKING no: así el núcleo incluye su libretro-common. Empaquetamos con ar igualmente.
   STATIC_LINKING_AR = 1
   fpic := -fPIE
   CC  ?= aarch64-none-elf-gcc
   CXX ?= aarch64-none-elf-g++
   AR  := aarch64-none-elf-ar
   FLAGS += -D__SWITCH__ -DSWITCH -DHAVE_LIBNX -D_GNU_SOURCE
   HAVE_CDROM = 0
   HAVE_LIGHTREC = 0
