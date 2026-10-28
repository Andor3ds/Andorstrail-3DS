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
CXXFLAGS += -std=gnu++11
ASFLAGS := -g $(ARCH)
LDFLAGS = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS := -lcitro2d -lcitro3d -lctru -ltinyxml2 -lpng -lz
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
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
ifeq ($(strip $(CPPFILES)),)
export LD := $(CC)
else
export LD := $(CXX)
endif
export OFILES := $(CFILES:.c=.o) $(CPPFILES:.cpp=.o)
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
	@rm -f $(ROMFS)/gfx/*.t3x

test: $(BUILD)/host-tests/test_game_json $(BUILD)/host-tests/test_game_resources $(BUILD)/host-tests/test_game_map $(BUILD)/host-tests/test_game_interactions $(BUILD)/host-tests/test_game_core
	$(BUILD)/host-tests/test_game_json
	$(BUILD)/host-tests/test_game_resources romfs/game_data.pack
	$(BUILD)/host-tests/test_game_map romfs/maps
	$(BUILD)/host-tests/test_game_interactions romfs/game_data.pack $(BUILD)/host-tests/game-combat-save.tmp
	$(BUILD)/host-tests/test_game_core $(BUILD)/host-tests/game-core-save.tmp
	python3 -m unittest discover -s tests -p 'test_build_*.py' -v

$(BUILD)/host-tests:
	@mkdir -p $@

$(BUILD)/host-tests/test_game_json: source/core/game_json.c tests/test_game_json.c | $(BUILD)/host-tests
	gcc -std=c11 -Wall -Wextra -Werror -Isource/core $^ -o $@

$(BUILD)/host-tests/test_game_core: source/core/game_core.c tests/test_game_core.c | $(BUILD)/host-tests
	gcc -std=c11 -Wall -Wextra -Werror -Isource/core $^ -o $@

$(BUILD)/host-tests/test_game_resources: source/core/game_json.c source/core/game_resources.c source/core/game_resource_index.c tests/test_game_resources.c | $(BUILD)/host-tests
	gcc -std=c11 -Wall -Wextra -Werror -Isource/core $^ -lz -o $@

$(BUILD)/host-tests/test_game_map: source/core/game_map.cpp tests/test_game_map.cpp | $(BUILD)/host-tests
	g++ -std=c++11 -Wall -Wextra -Werror -Isource/core $^ -ltinyxml2 -lz -o $@

$(BUILD)/host-tests/game_json.o: source/core/game_json.c | $(BUILD)/host-tests
	gcc -std=c11 -Wall -Wextra -Werror -Isource/core -c $< -o $@

$(BUILD)/host-tests/game_resources.o: source/core/game_resources.c | $(BUILD)/host-tests
	gcc -std=c11 -Wall -Wextra -Werror -Isource/core -c $< -o $@

$(BUILD)/host-tests/game_resource_index.o: source/core/game_resource_index.c | $(BUILD)/host-tests
	gcc -std=c11 -Wall -Wextra -Werror -Isource/core -c $< -o $@

$(BUILD)/host-tests/game_core.o: source/core/game_core.c | $(BUILD)/host-tests
	gcc -std=c11 -Wall -Wextra -Werror -Isource/core -c $< -o $@

$(BUILD)/host-tests/test_game_interactions: source/core/game_map.cpp source/core/game_dialogue.cpp source/core/game_items.cpp source/core/game_combat.cpp tests/test_game_interactions.cpp $(BUILD)/host-tests/game_json.o $(BUILD)/host-tests/game_resources.o $(BUILD)/host-tests/game_resource_index.o $(BUILD)/host-tests/game_core.o | $(BUILD)/host-tests
	g++ -std=c++11 -Wall -Wextra -Werror -Isource/core $^ -ltinyxml2 -lz -o $@
else
DEPENDS := $(OFILES:.o=.d)
MAP_ROMFS_FILES := $(wildcard $(PROJECT_ROOT)/$(ROMFS)/maps/*.tmx) \
				   $(PROJECT_ROOT)/$(ROMFS)/maps/worldmap.xml \
				   $(PROJECT_ROOT)/$(ROMFS)/maps/maps.list
MAP_T3X_FILES := $(patsubst $(PROJECT_ROOT)/assets/map/home/%.png,$(PROJECT_ROOT)/$(ROMFS)/gfx/%.t3x,$(wildcard $(PROJECT_ROOT)/assets/map/home/*.png))
$(OUTPUT).3dsx: $(OUTPUT).elf $(OUTPUT).smdh $(PROJECT_ROOT)/$(ROMFS)/game_data.pack $(MAP_ROMFS_FILES) $(PROJECT_ROOT)/$(ROMFS)/gfx/tilesets.list $(MAP_T3X_FILES)
$(OUTPUT).elf: $(OFILES)
$(PROJECT_ROOT)/$(ROMFS)/gfx/%.t3x: $(PROJECT_ROOT)/assets/map/home/%.png
	@mkdir -p $(dir $@)
	tex3ds -f rgba8888 -z auto -o $@ $<
-include $(DEPENDS)
endif