.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is not set. Install devkitPro and configure the environment)
endif
ifeq ($(strip $(DEVKITARM)),)
$(error DEVKITARM is not set. Install devkitARM and configure the environment)
endif

TARGET := andorstrail-3ds
BUILD := build
SOURCES := source/core source/platform/3ds
INCLUDES := include source/core
ROMFS := romfs

APP_TITLE := Andor's Trail
APP_DESCRIPTION := Native Nintendo 3DS port
APP_AUTHOR := Andor's Trail contributors

ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard
CFLAGS := -g -Wall -Wextra -O2 -ffunction-sections -fdata-sections $(ARCH) -D__3DS__
CFLAGS += $(INCLUDE)
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS := -g $(ARCH)
LDFLAGS = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS := -lctru -lz
CTRULIB ?= $(DEVKITPRO)/libctru
LIBDIRS := $(CTRULIB) $(DEVKITPRO)/portlibs/3ds

include $(DEVKITARM)/3ds_rules

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export PROJECT_ROOT := $(CURDIR)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)
export _3DSXDEPS := $(OUTPUT).smdh
export _3DSXFLAGS += --smdh=$(OUTPUT).smdh --romfs=$(PROJECT_ROOT)/$(ROMFS)
CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
export LD := $(CC)
export OFILES := $(CFILES:.c=.o)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: all clean test $(BUILD)

all: $(BUILD)

$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@rm -rf $(BUILD) $(TARGET).elf $(TARGET).3dsx $(TARGET).smdh $(TARGET).lst $(TARGET).map

test: $(BUILD)/host-tests/test_game_json $(BUILD)/host-tests/test_game_resources
	$(BUILD)/host-tests/test_game_json
	$(BUILD)/host-tests/test_game_resources romfs/game_data.pack
	python3 -m unittest discover -s tests -p 'test_build_game_data.py' -v

$(BUILD)/host-tests:
	@mkdir -p $@

$(BUILD)/host-tests/test_game_json: source/core/game_json.c tests/test_game_json.c | $(BUILD)/host-tests
	gcc -std=c11 -Wall -Wextra -Werror -Isource/core $^ -o $@

$(BUILD)/host-tests/test_game_resources: source/core/game_json.c source/core/game_resources.c source/core/game_resource_index.c tests/test_game_resources.c | $(BUILD)/host-tests
	gcc -std=c11 -Wall -Wextra -Werror -Isource/core $^ -lz -o $@
else
DEPENDS := $(OFILES:.o=.d)
$(OUTPUT).3dsx: $(OUTPUT).elf $(OUTPUT).smdh $(PROJECT_ROOT)/$(ROMFS)/game_data.pack
$(OUTPUT).elf: $(OFILES)
-include $(DEPENDS)
endif