# Firmware del STM32F030C8T6 de protección.
#   make            -> build/proteccion.elf, .bin y .hex
#   make flash      -> graba con st-flash (ST-Link por el conector SWD J1)
# Necesita arm-none-eabi-gcc en el PATH (o PREFIX=/ruta/al/bin/arm-none-eabi-).

PREFIX ?= arm-none-eabi-
CC      = $(PREFIX)gcc
OBJCOPY = $(PREFIX)objcopy
SIZE    = $(PREFIX)size

TARGET = build/proteccion
SRCS   = src/main.c src/board.c src/protection.c src/i2c_slave.c src/fc41d.c src/telemetry.c src/system_stm32f0xx.c
ASMS   = src/startup_stm32f030x8.s
OBJS   = $(SRCS:src/%.c=build/%.o) $(ASMS:src/%.s=build/%.o)

CPU    = -mcpu=cortex-m0 -mthumb
CFLAGS = $(CPU) -std=c11 -Os -g -Wall -Wextra -ffunction-sections -fdata-sections \
         -DSTM32F030x8 -Iinc -Icmsis
LDFLAGS = $(CPU) -Tstm32f030c8.ld -Wl,--gc-sections -Wl,-Map=$(TARGET).map --specs=nano.specs --specs=nosys.specs -Wl,--no-warn-rwx-segments

all: $(TARGET).elf $(TARGET).bin $(TARGET).hex
	$(SIZE) $(TARGET).elf

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: src/%.s | build
	$(CC) $(CPU) -c $< -o $@

$(TARGET).elf: $(OBJS) stm32f030c8.ld
	$(CC) $(LDFLAGS) $(OBJS) -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

build:
	mkdir -p build

flash: $(TARGET).bin
	st-flash --reset write $(TARGET).bin 0x08000000

clean:
	rm -rf build

.PHONY: all flash clean
