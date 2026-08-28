HEAP_SIZE = 8388208
STACK_SIZE = 61800
PRODUCT = Jellydate.pdx

SDK = $(PLAYDATE_SDK_PATH)
ifeq ($(SDK),)
SDK = $(shell awk -F= '/^SDKRoot/ { print $$2; exit }' ~/.Playdate/config 2>/dev/null)
endif
ifeq ($(SDK),)
$(error Playdate SDK not found. Set PLAYDATE_SDK_PATH or install the SDK)
endif

VPATH += src
SRC = src/main.c src/app.c src/network.c src/protocol.c src/video.c src/audio.c src/controls.c src/settings.c src/ui.c
UINCDIR = src

include $(SDK)/C_API/buildsupport/common.mk
