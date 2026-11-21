#---------------------------------------------------------------------------------
# PSX-K2 — Emulador de PlayStation 1 para Switch 2 con kaiHEN (homebrew .nro)
# Requiere devkitPro + ounce-devkit (libounce, OunceVK, libkaihen):
#   sudo dkp-pacman -S switch-dev switch-glad switch-zlib
#   sudo dkp-pacman -Sy libounce ouncevk libkaihen        (repositorio [ounce])
# Antes de compilar ejecuta ./setup.sh (descarga y compila el núcleo Beetle PSX HW e ImGui)
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "Define DEVKITPRO en tu entorno: export DEVKITPRO=/opt/devkitpro")
endif

TOPDIR ?= $(CURDIR)
# libounce sustituye a libnx: mismas APIs, adaptadas a Switch 2
include $(DEVKITPRO)/libounce/switch2_rules

TARGET      := psx-k2
BUILD       := build
SOURCES     := source third_party/imgui
DATA        :=
INCLUDES    := source third_party/imgui third_party/imgui_backend third_party/stb third_party/gen \
               third_party/beetle-psx/libretro-common/include
APP_TITLE   := PSX-K2
APP_AUTHOR  := PSX-K2 Team
APP_VERSION	:=	0.8.1
ICON        := icon.jpg

ARCH     := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
DEFINES  := -D__SWITCH__ -D__SWITCH2__ -DIMGUI_IMPL_OPENGL_LOADER_CUSTOM -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS -DIMGUI_DISABLE_DEMO_WINDOWS -DIMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS
CFLAGS   := -g -Wall -O2 -ffunction-sections $(ARCH) $(DEFINES)
CFLAGS   += $(INCLUDE)
CXXFLAGS := $(CFLAGS) -std=gnu++17 -fno-exceptions -fno-rtti
ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

# libbeetlepsx.a la genera setup.sh (núcleo libretro estático: Vulkan + OpenGL, PGXP, CHD)
# -lnx enlaza libounce; -lEGL (libouncevk-gl) trae EGL/OpenGL y también Vulkan completo:
# no se enlaza además -lvulkan (libouncevk) porque duplicaría el driver
LIBS    := -lbeetlepsx -lglad -lEGL -lz -lnx -lm -lstdc++
LIBDIRS := $(PORTLIBS) $(DEVKITPRO)/libounce $(TOPDIR)/third_party/lib

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------
export OUTPUT  := $(CURDIR)/$(TARGET)
export TOPDIR  := $(CURDIR)
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

export LD := $(CXX)
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES     := $(OFILES_SRC)
export INCLUDE    := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                     $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS   := $(foreach dir,$(LIBDIRS),-L$(dir)/lib) -L$(TOPDIR)/third_party/lib

ifneq ($(wildcard $(TOPDIR)/$(ICON)),)
export APP_ICON := $(TOPDIR)/$(ICON)
endif
export NROFLAGS += --icon=$(APP_ICON)

.PHONY: $(BUILD) clean all dist

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf dist

# Empaqueta la estructura lista para copiar a la raíz de la microSD
dist: all
	@rm -rf dist && cp -r sdcard dist && cp $(TARGET).nro dist/switch/psx-k2/
	@cp config/psx-k2.ini dist/switch/psx-k2/config/
	@echo "Copia el contenido de dist/ a la raíz de tu microSD"

#---------------------------------------------------------------------------------
else
.PHONY: all
DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).nro

$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp
$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)
endif
