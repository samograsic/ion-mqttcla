#!/bin/bash
#
# Build Paho MQTT C library without CMake
# Builds only the synchronous client (libpaho-mqtt3c)
#

set -e

PAHO_SRC="paho-mqtt/src"
BUILD_DIR="paho-mqtt/build"
OUTPUT_DIR="$BUILD_DIR/output"

# Create build directories
mkdir -p "$BUILD_DIR"
mkdir -p "$OUTPUT_DIR"

echo "Building Paho MQTT C library (synchronous client only)..."

# Source files needed for MQTTClient (synchronous)
SOURCES="
$PAHO_SRC/MQTTClient.c
$PAHO_SRC/Heap.c
$PAHO_SRC/LinkedList.c
$PAHO_SRC/Log.c
$PAHO_SRC/Messages.c
$PAHO_SRC/MQTTPacket.c
$PAHO_SRC/MQTTPacketOut.c
$PAHO_SRC/MQTTPersistence.c
$PAHO_SRC/MQTTPersistenceDefault.c
$PAHO_SRC/MQTTProtocolClient.c
$PAHO_SRC/MQTTProtocolOut.c
$PAHO_SRC/Socket.c
$PAHO_SRC/SocketBuffer.c
$PAHO_SRC/Tree.c
$PAHO_SRC/utf-8.c
$PAHO_SRC/MQTTTime.c
$PAHO_SRC/Thread.c
$PAHO_SRC/MQTTProperties.c
$PAHO_SRC/MQTTReasonCodes.c
$PAHO_SRC/Base64.c
$PAHO_SRC/SHA1.c
$PAHO_SRC/WebSocket.c
$PAHO_SRC/Proxy.c
$PAHO_SRC/StackTrace.c
$PAHO_SRC/Clients.c
"

# Compile to object files
OBJECTS=""
for src in $SOURCES; do
    if [ -f "$src" ]; then
        obj="$BUILD_DIR/$(basename ${src%.c}.o)"
        echo "Compiling $src..."
        gcc -c -fPIC -O2 -Wall -I"$PAHO_SRC" -DPAHO_MQTT_EXPORTS -D_GNU_SOURCE \
            "$src" -o "$obj"
        OBJECTS="$OBJECTS $obj"
    fi
done

# Create shared library
echo "Creating shared library..."
gcc -shared -Wl,-soname,libpaho-mqtt3c.so.1 -o "$OUTPUT_DIR/libpaho-mqtt3c.so.1.3.13" $OBJECTS -lpthread

# Create symbolic links
cd "$OUTPUT_DIR"
ln -sf libpaho-mqtt3c.so.1.3.13 libpaho-mqtt3c.so.1
ln -sf libpaho-mqtt3c.so.1 libpaho-mqtt3c.so
cd - > /dev/null

echo "Paho MQTT C library built successfully!"
echo "Library location: $OUTPUT_DIR/libpaho-mqtt3c.so"
