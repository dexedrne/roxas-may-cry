KH_EXE ?= kh2fm.exe
BUILD := run-native
DLL := $(BUILD)/rmc-native.dll
SITES := $(wildcard *.sites)
SRC := settings.c menu_input.c names.c look.c campaign.c campaign_actor.c logo.c cheats.c native.c debug.c config.c gameplay.c difficulty.c heapflight.c tweaks.c $(filter-out %_test.c,$(wildcard tweaks_*.c))
OBJ := $(SRC:%.c=$(BUILD)/%.o)
MINGW ?= x86_64-w64-mingw32-gcc
WINE_DEV ?= /usr
CFLAGS_WIN := -std=gnu11 -O2 -Wall -Wextra -fno-strict-aliasing -I$(BUILD) -MMD -MP
ifeq ($(shell command -v $(MINGW)),)
WINCC := clang --target=x86_64-w64-windows-gnu
CFLAGS_WIN += -isystem $(WINE_DEV)/include/wine/windows -isystem $(WINE_DEV)/include/wine/msvcrt -D_UCRT
LINK := $(WINCC) -fuse-ld=lld -nostdlib -shared -Wl,--entry,DllMain
LIBS := -L$(WINE_DEV)/lib/wine/x86_64-windows -lwinecrt0 -lucrtbase -luser32 -lkernel32 -lcompiler-rt
else
WINCC := $(MINGW)
LINK := $(WINCC) -shared -static-libgcc
LIBS := -luser32 -lkernel32
endif

all: $(DLL)
$(BUILD):
	mkdir -p $@
$(BUILD)/sites.inc: tools/sites.py $(SITES) $(KH_EXE) | $(BUILD)
	python3 $< "$(KH_EXE)" "$@" $(SITES)
$(BUILD)/native.o: $(BUILD)/sites.inc
$(BUILD)/%.o: %.c | $(BUILD)
	$(WINCC) $(CFLAGS_WIN) -c $< -o $@
$(DLL): $(OBJ)
	$(LINK) -o $@ $(OBJ) $(LIBS)
-include $(OBJ:.o=.d)

$(BUILD)/config-test: config.c config.h config_test.c config_fields.inc | $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 config.c config_test.c -o $@
$(BUILD)/heapflight-test.so: heapflight_test.c heapflight.c rmc.h | $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -shared -fPIC heapflight_test.c -o $@
test: $(BUILD)/config-test
	$(BUILD)/config-test
ifneq ($(GEN),)
check-heap: $(BUILD)/heapflight-test.so
	python3 heapflight_verify.py $(BUILD)/heapflight-test.so "$(KH_EXE)" "$(GEN)"

$(BUILD)/tweak-check: tools/tweak_check.c tweaks.c config.c config_fields.inc tweaks_edits.inc $(GEN)/mod_static_table.h | $(BUILD)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -I"$(GEN)" tools/tweak_check.c tweaks.c config.c -o $@
check-tweaks: $(BUILD)/tweak-check
	$< "$(KH_EXE)"
test: check-tweaks check-heap
endif
.PHONY: all test check-heap check-tweaks
$(BUILD)/difficulty-test: config.c difficulty.c difficulty_test.c difficulty.h tweaks_game.h config_fields.inc | $(BUILD)
	$(CC) -std=c11 -O2 config.c difficulty.c difficulty_test.c -o $@
$(BUILD)/tweaks-test: tweaks_test.c tweaks_actor.c tweaks_menu.c tweaks_motion.c config.c config_fields.inc tweaks_game.h | $(BUILD)
	$(CC) -std=gnu11 -O2 tweaks_test.c tweaks_actor.c tweaks_menu.c tweaks_motion.c config.c -o $@
check-logic: $(BUILD)/difficulty-test $(BUILD)/tweaks-test
	$(BUILD)/difficulty-test
	$(BUILD)/tweaks-test
test: check-logic
.PHONY: check-logic
$(BUILD)/gameplay-test: gameplay_test.c gameplay.c script_entries.inc config.c config.h config_fields.inc rmc.h | $(BUILD)
	$(CC) -std=c11 -O2 -fno-strict-aliasing gameplay_test.c config.c -o $@
check-scripts: $(BUILD)/gameplay-test
	$<
test: check-scripts
.PHONY: check-scripts

$(BUILD)/debug-test: debug.c debug_test.c difficulty.h config.h | $(BUILD)
	$(CC) -std=gnu11 -I$(WINE_DEV)/include/wine/windows -O2 debug_test.c -o $@
check-debug: $(BUILD)/debug-test
	$< off
	$< on
	$< reset
test: check-debug
.PHONY: check-debug
$(BUILD)/branch-check: tools/branch_check.c branch.h site.h $(BUILD)/sites.inc | $(BUILD)
	$(CC) -std=c11 -O2 -I$(BUILD) tools/branch_check.c -o $@
check-branches: $(BUILD)/branch-check
	$< "$(KH_EXE)"
test: check-branches
.PHONY: check-branches
PLAN_SRC := $(filter-out native.c menu_input.c debug.c,$(SRC))
$(BUILD)/combined-check: tools/combined_check.c $(PLAN_SRC) $(wildcard *.h *.inc) $(BUILD)/sites.inc
	$(CC) -std=gnu11 -O2 -fno-strict-aliasing -I$(BUILD) tools/combined_check.c $(PLAN_SRC) -o $@
check-combined: $(BUILD)/combined-check
	$< "$(KH_EXE)" 0
	$< "$(KH_EXE)" 1
test: check-combined
.PHONY: check-combined

$(BUILD)/cheats-test: cheats_test.c cheats.c config.c config.h config_fields.inc | $(BUILD)
	$(CC) -std=gnu11 -O2 cheats_test.c config.c -o $@
check-cheats: $(BUILD)/cheats-test
	$<
test: check-cheats
.PHONY: check-cheats

$(BUILD)/settings-test: settings_test.c settings.c names.c config.c config_fields.inc settings.h campaign.h look.h | $(BUILD)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -Werror settings_test.c names.c config.c -o $@
$(BUILD)/logo-test: logo_test.c logo.c config.h settings.h | $(BUILD)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -Werror logo_test.c -o $@
check-settings: $(BUILD)/settings-test $(BUILD)/logo-test
	$(BUILD)/settings-test
	$(BUILD)/logo-test
test: check-settings
.PHONY: check-settings

$(BUILD)/campaign-test: campaign_test.c campaign.c campaign_actor.c campaign_fields.inc castle_rules.inc campaign.h tweaks_game.h | $(BUILD)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -Werror $< -o $@
$(BUILD)/look-test: look_test.c look.c look.h look_voice.inc tweaks_game.h | $(BUILD)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -Werror $< -o $@
check-character: $(BUILD)/campaign-test $(BUILD)/look-test
	$(BUILD)/campaign-test
	$(BUILD)/look-test
test: check-character
.PHONY: check-character
