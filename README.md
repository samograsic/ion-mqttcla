# ION MQTT Convergence Layer Adapter

A fully functional MQTT-based Convergence Layer Adapter (CLA) for NASA/JPL's ION Delay-Tolerant Networking (DTN) implementation. This adapter enables DTN bundle transmission and reception over MQTT protocol, making it ideal for IoT, space communications, and environments where MQTT infrastructure is already deployed.

## Author

**Samo Grasic** <samo@grasic.net>

## Features

- **MQTT Protocol Support**: Uses Eclipse Paho MQTT C library for reliable pub/sub messaging
- **Bundle Transmission**: Outduct adapter (`mqttclo`) publishes DTN bundles to MQTT topics
- **Bundle Reception**: Induct adapter (`mqttcli`) subscribes to MQTT topics and receives bundles
- **Configurable QoS**: Supports MQTT QoS levels 0, 1, and 2
- **External Configuration**: All MQTT credentials and parameters in separate config file
- **Automatic Reconnection**: Handles connection loss with automatic reconnection logic
- **Topic-based Routing**: Uses `ipn/{node_number}` topic naming convention
- **Flexible Configuration**: Multiple configuration file search paths

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                     ION DTN Node                        │
│                                                         │
│  ┌──────────┐                          ┌──────────┐   │
│  │ Bundle   │◄────────────────────────►│ Bundle   │   │
│  │ Protocol │                          │ Protocol │   │
│  │  (BP)    │                          │  (BP)    │   │
│  └────┬─────┘                          └─────┬────┘   │
│       │                                      │         │
│  ┌────▼──────┐                        ┌─────▼────┐   │
│  │  mqttclo  │                        │ mqttcli  │   │
│  │ (outduct) │                        │ (induct) │   │
│  └─────┬─────┘                        └─────┬────┘   │
│        │                                    │         │
└────────┼────────────────────────────────────┼─────────┘
         │                                    │
         │          MQTT Broker              │
         │      (e.g., broker.hivemq.com)    │
         │                                    │
    ┌────▼────────────────────────────────────▼────┐
    │    PUBLISH: ipn/1                            │
    │    SUBSCRIBE: ipn/2                          │
    └──────────────────────────────────────────────┘
```

## Requirements

### System Requirements
- Linux operating system (tested on Raspberry Pi OS and standard Linux)
- ION DTN installation (ione-code or official ION)
- GCC compiler
- Standard build tools (make)

### Dependencies
- **ION DTN**: Bundle Protocol libraries (libici, ltp, lbp)
- **Eclipse Paho MQTT C**: v1.3.13 or later (automatically built by Makefile)
- **pthread**: POSIX threads library
- **Standard libraries**: libm, librt

## Installation

### 1. Clone the Repository

```bash
git clone https://github.com/yourusername/ion-mqttcl.git
cd ion-mqttcl
```

### 2. Configure ION Paths

Edit the `Makefile` and update the `INCLUDES` path to match your ION installation:

```makefile
# Example for custom ION installation
INCLUDES = -I/path/to/ion/include -I./paho-mqtt/src -I.

# Or for system-wide ION installation
INCLUDES = -I/usr/local/include/ion -I./paho-mqtt/src -I.
```

### 3. Build the Convergence Layer

The Makefile will automatically download and build the Paho MQTT library:

```bash
make all
```

This will:
- Clone and build Eclipse Paho MQTT C library
- Compile `mqttcli` (induct) and `mqttclo` (outduct) daemons
- Generate executables in the current directory

### 4. Install

```bash
# Option 1: Install to ION directory
sudo cp mqttcli mqttclo /path/to/ion/bin/

# Option 2: Install to system path
sudo cp mqttcli mqttclo /usr/local/bin/

# Install Paho MQTT library
sudo cp paho-mqtt/build/output/libpaho-mqtt3c.so* /usr/local/lib/
sudo ldconfig
```

### 5. Configure MQTT Connection

Copy the example configuration and edit with your MQTT broker credentials:

```bash
cp mqtt.conf.example mqtt.conf
chmod 600 mqtt.conf  # Protect credentials
```

Edit `mqtt.conf`:

```ini
# MQTT Broker Address (tcp://hostname:port or ssl://hostname:port)
broker_address=tcp://broker.hivemq.com:1883

# MQTT Authentication (leave empty if not required)
username=
password=

# MQTT QoS Level (0, 1, or 2)
qos=1

# Keepalive Interval (seconds)
keepalive_interval=20

# Connection Timeout (milliseconds)
connect_timeout=10000

# Disconnect Timeout (milliseconds)
disconnect_timeout=10000

# Client ID Prefix
client_id_prefix=ion_dtn_
```

**Configuration File Search Paths** (checked in order):
1. `./mqtt.conf` (current directory)
2. `/usr/local/etc/ion/mqtt.conf`
3. `/etc/ion/mqtt.conf`

## Configuration

### ION Configuration

See the `examples/` directory for complete configuration examples.

#### 1. Add MQTT Protocol to bpadmin

Edit your ION configuration file (e.g., `host.rc`):

```bash
## begin bpadmin

# Add MQTT protocol (max payload 1400 bytes, 100 bundles)
a protocol mqtt 1400 100

# Add induct for receiving bundles
# Syntax: a induct <protocol> <duct_name> <CLI_command>
a induct mqtt 1 mqttcli

# Add outduct for sending bundles
# Syntax: a outduct <protocol> <duct_name> <CLO_command>
a outduct mqtt 2 mqttclo

# Start the protocol
s

## end bpadmin
```

#### 2. Configure Routing (ipnadmin)

```bash
## begin ipnadmin

# Add route to destination node via MQTT
# Syntax: a plan <destination_node> <outduct_protocol>/<outduct_name>
a plan 2 mqtt/2

# Start routing
s

## end ipnadmin
```

#### 3. Start ION

```bash
ionstart -I your_config_file.rc
```

## Usage

### Understanding mqttcli and mqttclo

The MQTT Convergence Layer consists of two daemons:

#### **mqttcli** (Induct - Receives bundles)
- Subscribes to an MQTT topic for your node
- Receives bundles published to that topic
- Delivers bundles to ION's Bundle Protocol Agent
- Runs continuously as a daemon

**Command-line syntax:**
```bash
mqttcli <duct_name>

# Examples:
mqttcli 1              # Subscribes to topic: ipn/1
mqttcli ipn/1          # Same as above
mqttcli ipn:1          # Same as above
```

**How it works:**
1. ION starts `mqttcli` automatically when you run `ionstart`
2. `mqttcli` connects to the MQTT broker specified in `mqtt.conf`
3. It subscribes to the topic matching your node number (e.g., `ipn/1`)
4. When bundles arrive via MQTT, it delivers them to ION
5. Runs until ION shuts down or the daemon is manually stopped

#### **mqttclo** (Outduct - Sends bundles)
- Publishes DTN bundles to MQTT topics
- Reads bundles from ION's outbound queue
- Sends them to remote nodes via MQTT broker
- Runs continuously as a daemon

**Command-line syntax:**
```bash
mqttclo <duct_name>

# Examples:
mqttclo 2              # Publishes to topic: ipn/2
mqttclo ipn/2          # Same as above
mqttclo ipn:2          # Same as above
```

**How it works:**
1. ION starts `mqttclo` automatically when you run `ionstart`
2. `mqttclo` connects to the MQTT broker specified in `mqtt.conf`
3. It monitors ION's outbound queue for bundles destined to its duct
4. When a bundle is queued, it publishes to the destination topic (e.g., `ipn/2`)
5. Waits for MQTT delivery confirmation before marking bundle as sent
6. Runs until ION shuts down or the daemon is manually stopped

### Manual Testing (Advanced)

You can manually start the daemons for testing (ION must already be running):

```bash
# Terminal 1: Start induct for node 1
./mqttcli 1

# Terminal 2: Start outduct for node 2
./mqttclo 2

# Terminal 3: Send a test bundle
echo "Test message" | bpsource ipn:2.1
```

**Note:** In production, ION manages these daemons automatically based on your `.rc` configuration file.

### Sending Bundles

Use the standard ION `bpsource` or `bpsendfile` commands. When bundles are sent to a node with an MQTT outduct, `mqttclo` automatically handles transmission:

```bash
# Send a text message to node 2 (via MQTT)
echo "Hello MQTT DTN World!" | bpsource ipn:2.1

# Send a file to node 2 (via MQTT)
bpsendfile ipn:1.1 ipn:2.1 myfile.txt

# Send with time-to-live (TTL) of 3600 seconds
echo "Important data" | bpsource ipn:2.1 3600

# Check outbound queue status
bpstats
```

**What happens behind the scenes:**
1. Bundle is created by `bpsource` and queued in ION
2. ION routing determines it should use MQTT (via the plan)
3. Bundle is placed in the MQTT outduct queue
4. `mqttclo` dequeues the bundle
5. `mqttclo` publishes it to MQTT topic `ipn/2`
6. Remote node's `mqttcli` receives and delivers it

### Receiving Bundles

Use the standard ION `bpsink` or `bprecvfile` commands. The `mqttcli` daemon automatically receives bundles from MQTT and delivers them to ION:

```bash
# Receive and display bundles at endpoint ipn:1.1
bpsink ipn:1.1

# Receive files at endpoint ipn:1.1
bprecvfile ipn:1.1

# Count received bundles (use Ctrl+C to stop and see count)
bpcounter ipn:1.1
```

**What happens behind the scenes:**
1. Remote node publishes bundle to MQTT topic `ipn/1`
2. `mqttcli` receives the MQTT message (it's subscribed to `ipn/1`)
3. `mqttcli` delivers the bundle to ION's Bundle Protocol Agent
4. ION routes the bundle to the correct endpoint (e.g., `ipn:1.1`)
5. `bpsink` receives and displays the bundle

### Monitoring

Check ION logs for MQTT activity:

```bash
tail -f ion.log
```

Expected log entries:
```
[i] mqttcli: connecting to broker: tcp://broker.hivemq.com:1883
[i] mqttcli: connected to MQTT broker
[i] mqttcli: subscribed to topic: ipn/1
[i] mqttclo: connecting to broker: tcp://broker.hivemq.com:1883
[i] mqttclo: connected to MQTT broker
[i] mqttclo: will publish to topic: ipn/2
[i] mqttclo: bundle sent successfully
```

## Topic Naming Convention

The convergence layer uses the following topic format:

```
ipn/{node_number}
```

Examples:
- Node `ipn:1.x` subscribes to topic: `ipn/1`
- Node sends to `ipn:2.x` by publishing to: `ipn/2`

### Flexible Duct Name Formats

The CLA accepts multiple duct name formats in ION configuration:

```bash
# Full topic path
a induct mqtt ipn/1 mqttcli

# Just the node number (recommended)
a induct mqtt 1 mqttcli

# With 'ipn:' prefix
a induct mqtt ipn:1 mqttcli
```

All formats are automatically converted to the standard `ipn/{node_number}` topic.

## Quality of Service (QoS)

### QoS Levels

- **QoS 0** (At most once): Fire and forget, no acknowledgment
- **QoS 1** (At least once): Acknowledged delivery, possible duplicates (recommended)
- **QoS 2** (Exactly once): Guaranteed single delivery, highest overhead

Configure QoS in `mqtt.conf`:
```ini
qos=1
```

## File Structure

```
ion-mqttcl/
├── mqttcli.c           # Induct daemon (receives bundles)
├── mqttclo.c           # Outduct daemon (sends bundles)
├── mqttcli.h           # Common header definitions
├── mqtt_utils.c        # Utility functions (client ID, topic parsing)
├── mqtt_config.c       # Configuration file parser
├── mqtt.conf.example   # Example MQTT configuration file
├── Makefile            # Build script
├── build_paho.sh       # Paho MQTT library build script
├── examples/           # Example ION configuration files
│   ├── example.ionconfig
│   └── example_node.rc
├── .gitignore          # Git ignore file
└── README.md           # This file
```

## Troubleshooting

### Library Loading Errors

```
error while loading shared libraries: libpaho-mqtt3c.so.1
```

**Solution:**
```bash
sudo cp paho-mqtt/build/output/libpaho-mqtt3c.so* /usr/local/lib/
sudo ldconfig
```

Verify installation:
```bash
ldconfig -p | grep paho
```

### Connection Failures

**Check broker accessibility:**
```bash
ping broker.hivemq.com
telnet broker.hivemq.com 1883
```

**Verify credentials:**
- Ensure `mqtt.conf` has correct username/password (or empty if not required)
- Check broker logs for authentication errors

**Check ION logs:**
```bash
tail -100 ion.log | grep mqtt
```

### Bundle Not Received

1. **Verify MQTT daemons are running:**
   ```bash
   ps aux | grep mqtt
   ```

2. **Check subscriptions:**
   - Induct subscribes to its own node number topic
   - Outduct publishes to destination node number topic

3. **Test with MQTT tools:**
   ```bash
   # Subscribe to your node's topic
   mosquitto_sub -h broker.hivemq.com -p 1883 -t "ipn/1"

   # Publish a test message
   mosquitto_pub -h broker.hivemq.com -p 1883 -t "ipn/2" -m "test"
   ```

### Build Errors

**Missing ION headers:**
- Ensure ION is installed and paths in Makefile are correct
- Update `INCLUDES` in Makefile to point to your ION installation

**Paho MQTT build fails:**
- Ensure you have git installed: `sudo apt-get install git`
- The build script will clone Paho MQTT automatically

## Performance Considerations

- **Maximum Bundle Size**: 10 MB (configurable in `mqttcli.h`)
- **QoS 1 Overhead**: ~4 bytes per message for acknowledgment
- **Keepalive Interval**: 20 seconds (configurable in `mqtt.conf`)
- **Network Latency**: Depends on MQTT broker location and network conditions

## Security Notes

- **Authentication**: Uses username/password authentication
- **Encryption**: Add SSL/TLS by using `ssl://` broker address and configuring certificates
- **Credentials**: Store `mqtt.conf` with restrictive permissions (`chmod 600`)
- **Production Use**: Consider using certificate-based authentication and TLS encryption

## Development

### Building Debug Version

```bash
make debug
```

### Cleaning Build Artifacts

```bash
# Clean objects and executables (keep Paho library)
make clean

# Clean everything including Paho library
make distclean
```

### Testing

```bash
# Build and verify
make test
```

## Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Make your changes
4. Test thoroughly with ION
5. Commit your changes (`git commit -m 'Add amazing feature'`)
6. Push to the branch (`git push origin feature/amazing-feature`)
7. Open a Pull Request

## License

Copyright (c) 2026, California Institute of Technology.
ALL RIGHTS RESERVED. U.S. Government Sponsorship acknowledged.

Based on ION DTN TCPCL v4 implementation.

## References

- [ION DTN Project](https://sourceforge.net/projects/ion-dtn/)
- [Eclipse Paho MQTT](https://www.eclipse.org/paho/)
- [MQTT Protocol Specification](https://mqtt.org/)
- [Bundle Protocol RFC 9171](https://www.rfc-editor.org/rfc/rfc9171.html)

## Acknowledgments

- Based on ION TCPCL v4 implementation
- Uses Eclipse Paho MQTT C library
- ION DTN developed by NASA/JPL

## Support

For issues, questions, or contributions:
- **GitHub Issues**: https://github.com/yourusername/ion-mqttcl/issues
- **Email**: samo@grasic.net
- **ION DTN Mailing List**: ion-dtn-users@lists.sourceforge.net
