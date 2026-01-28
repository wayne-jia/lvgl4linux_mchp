SDK=/home/wayne/sdk/sama5/arm-buildroot-linux-gnueabihf_sdk-buildroot
SRC_DIR=../src
SRCS := $(shell find $(SRC_DIR) -name "*.c")
CC=$(SDK)/bin/arm-buildroot-linux-gnueabihf-gcc
CFLAGS+=`$(SDK)/bin/pkg-config libplanes --cflags --libs`
CFLAGS+=`$(SDK)/bin/pkg-config libdrm --cflags --libs`
CFLAGS+=`$(SDK)/bin/pkg-config libinput --cflags --libs`
CFLAGS+= -I../include -I../src/lvgl -I../src/lvgl/demos
LIB     := -ludev -lcairo
TARGET  := app

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $^ -o $@ $(LIB)

clean:
	rm -f $(TARGET)
