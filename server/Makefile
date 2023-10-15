#CROSS_COMPILE=aarch64-none-linux-gnu-
CROSS_COMPILE ?=
CC ?= gcc
TARGET ?= aesdsocket
OBJFILES ?= aesdsocket.o
CFLAGS ?= -g -Wall -Werror
LDFLAGS ?= -lpthread -lrt

COMPILER = $(if $(CROSS_COMPILE),$(CROSS_COMPILE)$(CC),$(CC))
EXTRA_FLAGS = $(if $(CROSS_COMPILE),,-g)

default: all

all: $(TARGET)

$(TARGET): $(OBJFILES)
	$(COMPILER) $(EXTRA_FLAGS) -o $(TARGET) $(OBJFILES) $(CFLAGS) $(LDFLAGS)

$(OBJFILES): $(TARGET).c
	$(COMPILER) -c $(TARGET).c $(EXTRA_FLAGS) -o $(OBJFILES) $(CFLAGS)

clean:
	@rm -f $(TARGET) $(OBJFILES)
