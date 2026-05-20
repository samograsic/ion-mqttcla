#
# Makefile for ION MQTT Convergence Layer
# Based on ION DTN TCPCLv4 implementation
#

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -g -fPIC -D_REENTRANT -D_GNU_SOURCE
INCLUDES = -I../ione-code/ici/include -I../ione-code/ltp/include \
           -I../ione-code/bpv7/include -I../ione-code/bpv7/library \
           -I./paho-mqtt/src -I.
LDFLAGS = -L../ione-code -L./paho-mqtt/build/output -Wl,-rpath,../ione-code -Wl,-rpath,./paho-mqtt/build/output
LIBS = -lici -lltp -lbp -lpthread -lm -lrt -lpaho-mqtt3c

# Paho MQTT library
PAHO_DIR = paho-mqtt
PAHO_BUILD_DIR = $(PAHO_DIR)/build
PAHO_LIB = $(PAHO_BUILD_DIR)/output/libpaho-mqtt3c.so

# Source files
MQTTCLI_SOURCES = mqttcli.c mqtt_utils.c mqtt_config.c
MQTTCLO_SOURCES = mqttclo.c mqtt_utils.c mqtt_config.c

# Object files
MQTTCLI_OBJECTS = $(MQTTCLI_SOURCES:.c=.o)
MQTTCLO_OBJECTS = $(MQTTCLO_SOURCES:.c=.o)

# Executables
EXECUTABLES = mqttcli mqttclo

# Default target
all: paho $(EXECUTABLES)

# Build Paho MQTT library
paho: $(PAHO_LIB)

$(PAHO_LIB):
	@echo "Building Paho MQTT C library..."
	./build_paho.sh

# MQTT Input daemon
mqttcli: $(MQTTCLI_OBJECTS) $(PAHO_LIB)
	$(CC) $(LDFLAGS) -o $@ $(MQTTCLI_OBJECTS) $(LIBS)

# MQTT Output daemon
mqttclo: $(MQTTCLO_OBJECTS) $(PAHO_LIB)
	$(CC) $(LDFLAGS) -o $@ $(MQTTCLO_OBJECTS) $(LIBS)

# Compile source files to object files
%.o: %.c mqttcli.h $(PAHO_LIB)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Clean build artifacts
clean:
	rm -f $(MQTTCLI_OBJECTS) $(MQTTCLO_OBJECTS) $(EXECUTABLES) *.o

# Deep clean including Paho MQTT library
distclean: clean
	rm -rf $(PAHO_BUILD_DIR)

# Install targets
install: $(EXECUTABLES)
	@echo "Installing MQTT convergence layer daemons..."
	cp mqttcli ../ione-code/ 2>/dev/null || cp mqttcli /usr/local/bin/
	cp mqttclo ../ione-code/ 2>/dev/null || cp mqttclo /usr/local/bin/
	@echo "Installing Paho MQTT library..."
	cp $(PAHO_LIB)* /usr/local/lib/ 2>/dev/null || echo "Warning: Could not install Paho library to /usr/local/lib"
	ldconfig 2>/dev/null || echo "Run 'sudo ldconfig' to update library cache"
	@echo "Installation complete."

# Test build
test: clean all
	@echo "Build test successful - all executables compiled."

# Debug build
debug: CFLAGS += -DDEBUG -O0
debug: clean all

# Show help
help:
	@echo "ION MQTT Convergence Layer Makefile"
	@echo "===================================="
	@echo "Targets:"
	@echo "  all       - Build Paho MQTT library and both mqttcli and mqttclo daemons"
	@echo "  paho      - Build only the Paho MQTT C library"
	@echo "  mqttcli   - Build MQTT input daemon only"
	@echo "  mqttclo   - Build MQTT output daemon only"
	@echo "  clean     - Remove build artifacts (keep Paho library)"
	@echo "  distclean - Remove all build artifacts including Paho library"
	@echo "  install   - Install executables and libraries"
	@echo "  test      - Clean build test"
	@echo "  debug     - Build with debug symbols"
	@echo "  help      - Show this help message"

# Dependencies
mqttcli.o: mqttcli.c mqttcli.h
mqttclo.o: mqttclo.c mqttcli.h

.PHONY: all clean distclean install test debug help paho
