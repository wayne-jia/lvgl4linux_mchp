SDK=/home/wayne/sdk/arm9x7-dev/arm-buildroot-linux-gnueabi_sdk-buildroot
SRC_DIR=../src
CXX=$(SDK)/bin/arm-buildroot-linux-gnueabi-gcc
CXXFLAGS=`$(SDK)/bin/pkg-config libegt --cflags --libs`
CXXFLAGS+=`$(SDK)/bin/pkg-config libplanes --cflags --libs`
CXXFLAGS+=`$(SDK)/bin/pkg-config libdrm --cflags --libs`
CXXFLAGS+= -I../include
SRC               := $(SRC_DIR)/*.c
TARGET            := app

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@

clean:
	rm -f $(TARGET)

