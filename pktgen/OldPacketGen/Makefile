.PHONY: all clean

CC := gcc
CFLAGS := -g -O2 -I ./include/
LINK := -lpthread

SOURCE_FILES := udpgen.c log.c
TARGET := ./build/udpgen



all: $(TARGET)

clean:
	rm -rf ./build/

./build:
	if [ ! -d ./build/ ]; then mkdir ./build; fi

$(TARGET): ./build $(SOURCE_FILES)
	$(CC) $(CFLAGS) $(SOURCE_FILES) $(LINK) -o $(TARGET)
