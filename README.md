# ESP32 Serial Studio Dashboard Generator

A lightweight C++ library for ESP32 (and compatible boards) that generates
[Serial Studio](https://serial-studio.github.io/) dashboard JSON from a
compile-time configuration.  It is decoupled from any transport layer — you
supply the configuration structs, call `serialize()`, and send the resulting
frame however you like (USB serial, TCP, WebSocket, etc.).

---

## Features

- **Zero-overhead config** — dashboard layout is defined as `constexpr` C++
  structs; no heap allocation at construction time.
- **Live value patching** — `update()` walks a flat telemetry `JsonDocument`
  and patches every dataset `"value"` field in one pass.
- **Transport-agnostic** — `serialize()` writes a `/*{…JSON…}*/\r\n` frame
  into a caller-supplied buffer; send it anywhere.
- **ArduinoJson only** — no other external dependencies.

---

## Dependencies

| Library | Version | Install via |
|---------|---------|-------------|
| [ArduinoJson](https://arduinojson.org/) | ≥ 7.x | PlatformIO / Library Manager |

For the TCP server example only:

| Library | Version | Install via |
|---------|---------|-------------|
| [AsyncTCP](https://github.com/ESP32Async/AsyncTCP) | ≥ 3.x | PlatformIO (`esp32async/AsyncTCP`) |

---

## Installation

**PlatformIO** — add to `lib_deps` in `platformio.ini`:

```ini
lib_deps =
    bblanchon/ArduinoJson@^7.0.0
    # clone or reference this library as a local lib/ folder or git submodule
```

**Arduino IDE** — copy the library folder into your `libraries/` directory.

---

## Quick Start

```cpp
#include <ArduinoJson.h>
#include "ss_dashboard.h"
#include "ss_dashboard_config.h"

// 1. Define datasets
static const ss::DatasetCfg kTempDatasets[] = {
    { .title = "Temperature", .units = "°C",
      .telemetryKey = "temp",
      .index = 1,
      .widget = ss::WidgetType::Gauge,
      .widgetMin = -20, .widgetMax = 80,
      .plotMin   = -20, .plotMax   = 80,
      .graph = true, .log = true, .overviewDisplay = true },
    { .title = "Humidity", .units = "%",
      .telemetryKey = "humidity",
      .index = 2,
      .widget = ss::WidgetType::Bar,
      .widgetMin = 0, .widgetMax = 100,
      .graph = true },
};

// 2. Define groups
static const ss::GroupCfg kGroups[] = {
    { .title = "Environment",
      .widget = ss::GroupWidget::Multiplot,
      .datasets     = kTempDatasets,
      .datasetCount = 2 },
};

// 3. Define top-level config
static const ss::DashboardCfg kCfg = {
    .title      = "My Dashboard",
    .groups     = kGroups,
    .groupCount = 1,
};

// 4. Construct the dashboard
static ss::Dashboard dashboard(kCfg);
static char txBuf[4096];

void setup() {
    Serial.begin(115200);
    dashboard.begin();
}

void loop() {
    // 5. Fill a telemetry document with current readings
    JsonDocument telemetry;
    telemetry["temp"]     = 23.4f;
    telemetry["humidity"] = 55.0f;

    // 6. Patch and serialise
    dashboard.update(telemetry);
    const size_t len = dashboard.serialize(txBuf, sizeof(txBuf));
    if (len > 0) {
        Serial.write(txBuf, len);
    }

    delay(1000);
}
```

Open Serial Studio, select your COM port, enable **Serial Studio frame
detection** (`/*` / `*/`) and the dashboard will appear automatically.

---

## Configuration Reference

### `ss::DatasetCfg`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `title` | `const char*` | `nullptr` | Dataset label shown in Serial Studio |
| `units` | `const char*` | `""` | Unit string (e.g. `"°C"`, `"m/s²"`) |
| `telemetryKey` | `const char*` | `nullptr` | Dotted key path into the telemetry `JsonDocument` passed to `update()` (e.g. `"imu.accel.x"`) |
| `index` | `uint8_t` | `0` | 1-based dataset index in the Serial Studio frame |
| `widget` | `WidgetType` | `None` | Per-dataset widget: `None`, `Gauge`, `Bar`, `Led` |
| `widgetMin/Max` | `float` | `0` | Widget display range |
| `plotMin/Max` | `float` | `0` | Graph axis range |
| `alarmLow/High` | `float` | `0` | Alarm threshold values |
| `alarmEnabled` | `bool` | `false` | Enable alarm highlighting |
| `graph` | `bool` | `false` | Show as a time-series graph |
| `log` | `bool` | `false` | Include in the data log |
| `led` | `bool` | `false` | Show as LED indicator |
| `ledHigh` | `uint8_t` | `0` | Value at which the LED shows as active |
| `overviewDisplay` | `bool` | `false` | Show in the overview panel |
| `fft` | `bool` | `false` | Enable FFT view |
| `fftSamples` | `uint16_t` | `256` | FFT sample count |
| `fftSamplingRate` | `uint16_t` | `100` | FFT sampling rate (Hz) |
| `xAxis` | `int8_t` | `-1` | Dataset index to use as X axis (`-1` = time) |

### `ss::GroupCfg`

| Field | Type | Description |
|-------|------|-------------|
| `title` | `const char*` | Group heading |
| `widget` | `GroupWidget` | Group widget: `None`, `Multiplot`, `Datagrid`, `Accelerometer` |
| `datasets` | `const DatasetCfg*` | Pointer to dataset array |
| `datasetCount` | `uint8_t` | Length of `datasets` array |

### `ss::ActionCfg`

Action buttons appear in Serial Studio's toolbar and transmit a short string
back to the device when clicked.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `title` | `const char*` | `nullptr` | Button label |
| `txData` | `const char*` | `nullptr` | String sent on click |
| `icon` | `const char*` | `nullptr` | Serial Studio icon name (e.g. `"Done"`, `"Close"`) |
| `eol` | `const char*` | `"\n"` | End-of-line appended to `txData` |

### `ss::DashboardCfg`

| Field | Type | Description |
|-------|------|-------------|
| `title` | `const char*` | Dashboard title |
| `groups` | `const GroupCfg*` | Pointer to group array |
| `groupCount` | `uint8_t` | Length of `groups` array |
| `actions` | `const ActionCfg*` | Pointer to action array (may be `nullptr`) |
| `actionCount` | `uint8_t` | Length of `actions` array |

---

## API Reference

### `ss::Dashboard`

```cpp
explicit Dashboard(const DashboardCfg& cfg);
```
Constructs a dashboard from a configuration that must outlive this object.
The config is typically `constexpr` / file-scope `static`.

```cpp
bool begin();
```
Builds the full JSON document from `cfg`.  Call once in `setup()`.
Returns `false` if the config is invalid.

```cpp
void update(const JsonDocument& telemetry);
```
Patches every dataset `"value"` field from `telemetry`.  The telemetry
document should be a nested object whose keys match the `telemetryKey` fields
in your config.

Dotted paths are supported: a `telemetryKey` of `"imu.accel.x"` looks up
`telemetry["imu"]["accel"]["x"]`.

```cpp
size_t serialize(char* buf, size_t bufLen) const;
```
Writes `/*{…JSON…}*/\r\n\r\n` into `buf`.  Returns the number of bytes
written (excluding the NUL terminator), or `0` on failure.  A buffer of
`estimateSize()` bytes is guaranteed to be sufficient.

```cpp
size_t estimateSize() const;
```
Returns the minimum buffer size needed by `serialize()`.

---

## Frame Format

Serial Studio raw-TCP / serial frame format:

```
/*{...JSON dashboard...}*/\r\n
```

The library writes `/*` as prefix and `*/\r\n\r\n` as suffix.  Serial Studio
detects these delimiters automatically in both serial and TCP (raw socket)
modes.

---

## Sizing the Transmit Buffer

Use `estimateSize()` after `begin()` to determine how large your transmit
buffer needs to be, or over-provision generously (the dashboard JSON for a
typical 20–30 dataset layout is 8–12 KB).

```cpp
dashboard.begin();
Serial.printf("Dashboard frame size: %u bytes\n", dashboard.estimateSize());
```

---

## Examples

| Example | Description |
|---------|-------------|
| [`BasicSerial`](examples/BasicSerial/BasicSerial.ino) | Environment sensors → USB serial; no WiFi required |
| [`WiFiTcpServer`](examples/WiFiTcpServer/WiFiTcpServer.ino) | WiFi + AsyncTCP raw-TCP server; non-blocking, FreeRTOS task |

---

## License

MIT
