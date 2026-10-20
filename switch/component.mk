# Shared native Switch build. Invoke from a component directory.
ifeq ($(strip $(DEVKITPRO)),)
$(error Set DEVKITPRO to a devkitPro installation)
endif
COMPONENT_DIR := $(abspath $(dir $(firstword $(MAKEFILE_LIST))))
ROOT := $(abspath $(COMPONENT_DIR)/../..)
GAMELEX_VERSION := $(shell sed -n 's/^inline constexpr char ReleaseVersion\[\] = "\([^"]*\)";.*/\1/p' $(ROOT)/core/include/gamelex/release.hpp)
ifeq ($(strip $(GAMELEX_VERSION)),)
$(error Cannot read GameLex version)
endif
include $(DEVKITPRO)/libnx/switch_rules
BUILD := build
SOURCES := source
INCLUDES := $(ROOT)/core/include $(ROOT)/vendor $(ROOT)/switch
CORE_FILES := core.cpp
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS := -g -Os -Wall -ffunction-sections -fdata-sections $(ARCH) -D__SWITCH__ -DJSON_NOEXCEPTION
CFLAGS += $(INCLUDE)
CXXFLAGS = $(CFLAGS) -std=gnu++17 -fexceptions -fno-rtti
LDFLAGS = -specs=$(DEVKITPRO)/libnx/switch.specs $(ARCH) -Wl,-Map,$(notdir $*.map),--gc-sections
LIBDIRS := $(PORTLIBS) $(LIBNX)
LIBS := -lnx

ifeq ($(KIND),overlay)
include $(ROOT)/vendor/libultrahand/ultrahand.mk
CXXFLAGS = $(CFLAGS) -std=gnu++26 -fno-exceptions -fno-rtti
CFLAGS += -DUI_OVERRIDE_PATH='"/config/gamelex/"' -DAPP_VERSION='"$(GAMELEX_VERSION)"' -DIS_LAUNCHER_DIRECTIVE=0 -DGAMELEX_CHINESE_FONT -DGAMELEX_CAPTURE_GUARD
LIBS := -lcurl -lz -lminizip -lmbedtls -lmbedx509 -lmbedcrypto -lnx
NO_ICON := 1
NO_NACP := 1
EXT := ovl
else ifeq ($(KIND),sysmodule)
CORE_FILES += image.cpp network.cpp form_http.cpp
LDFLAGS += -Wl,--wrap=sslConnectionGetVerifyCertError
LIBS := -ljpeg $(shell $(DEVKITPRO)/portlibs/switch/bin/curl-config --libs) -lmbedcrypto
EXT := nsp
# Keep the sysmodule free of exception unwinding and implicit JSON conversions.
CXXFLAGS += -fno-exceptions -fno-rtti -DJSON_USE_IMPLICIT_CONVERSIONS=0
else ifeq ($(KIND),minsys)
# C-only diagnostic sysmodule; no core, no C++ runtime objects.
CORE_FILES :=
LIBS := -lnx
EXT := nsp
else
APP_TITLE := GameLex Device Probe
APP_AUTHOR := GameLex contributors
APP_VERSION := $(GAMELEX_VERSION)
NO_ICON := 1
EXT := nro
endif

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export VPATH := $(foreach dir,$(SOURCES),$(if $(filter /%,$(dir)),$(dir),$(CURDIR)/$(dir))) $(ROOT)/core/src
export DEPSDIR := $(CURDIR)/$(BUILD)
CPPFILES := $(CORE_FILES) $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
export OFILES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o)
export LD := $(CXX)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(dir)) $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/build
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
ifeq ($(KIND),sysmodule)
export APP_JSON := $(CURDIR)/gamelex-sys.json
endif
ifeq ($(KIND),minsys)
export APP_JSON := $(CURDIR)/minprobe.json
endif
ifeq ($(KIND),diagnostics)
export NROFLAGS := --nacp=$(OUTPUT).nacp
endif
.PHONY: all clean $(BUILD)
all: $(BUILD)
$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $@ -f $(CURDIR)/Makefile
clean:
	rm -rf build $(TARGET).elf $(TARGET).map $(TARGET).nso $(TARGET).npdm $(TARGET).nsp $(TARGET).ovl $(TARGET).nro $(TARGET).nacp
else
.PHONY: all
all: $(OUTPUT).$(EXT)
$(OUTPUT).elf: $(OFILES)
ifeq ($(KIND),minsys)
$(OUTPUT).nsp: $(OUTPUT).nso $(OUTPUT).npdm
$(OUTPUT).nso: $(OUTPUT).elf
else ifeq ($(KIND),sysmodule)
$(OUTPUT).nsp: $(OUTPUT).nso $(OUTPUT).npdm
$(OUTPUT).nso: $(OUTPUT).elf
else ifeq ($(KIND),overlay)
$(OUTPUT).ovl: $(OUTPUT).elf
	elf2nro $< $@ --nacp=$(OUTPUT).nacp
$(OUTPUT).nacp: $(ROOT)/core/include/gamelex/release.hpp
	nacptool --create 'GameLex' 'GameLex contributors' '$(GAMELEX_VERSION)' $@
$(OUTPUT).ovl: $(OUTPUT).nacp
else
$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp
endif
-include $(OFILES:.o=.d)
endif
