# Makefile for dxlog_mult_listener
# Raspberry Pi 4 / Raspberry Pi OS (Bullseye / Bookworm)
#
# --------------------------------------------------------------------------
# Toolchain
# --------------------------------------------------------------------------
CC      := gcc
CFLAGS  := -O2 -Wall -Wextra
LDFLAGS :=
LIBS    := -lm

# --------------------------------------------------------------------------
# Targets
# --------------------------------------------------------------------------
TARGET  := listener
SRC     := listener.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)
	@echo "Built $@"

# --------------------------------------------------------------------------
# Clean
# --------------------------------------------------------------------------
clean:
	rm -f $(TARGET)
	@echo "Cleaned."

