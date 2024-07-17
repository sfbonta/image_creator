.POSIX:
.PHONY: build clean

INCLUDE_DIRS = -Isources/write_image \
			   -Isources/guid_provider \
			   -Isources/fat32_system_format

SOURCES = sources/main.c \
	      sources/write_image/write_image.c \
		  sources/guid_provider/guid_provider.c \
		  sources/fat32_system_format/fat32_system_format.c

OBJS = $(SOURCES:.c=.o)
DEPENDENCIES = $(SOURCES:.c=.d)

BUILD_TARGET = image_creator

CC = clang

LDFLAGS = 

CFLAGS = \
	-std=c17 \
	-Wall \
	-Wextra \
	-Wpedantic

build: $(BUILD_TARGET)

$(BUILD_TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)
	rm -rf build
	mkdir build
	mv $(BUILD_TARGET) build/$(BUILD_TARGET)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDE_DIRS) -c $< -o $@

-include $(DEPENDS)

clean:
	rm -rf build $(BUILD_TARGET) $(OBJS) $(DEPENDENCIES)
