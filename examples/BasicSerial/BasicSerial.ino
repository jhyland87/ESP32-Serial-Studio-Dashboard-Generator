/**
 * @file BasicSerial.ino
 * @brief Minimal ESP32-Serial-Studio-Dashboard-Generator example.
 *
 * Simulates an environment sensor (temperature, humidity, pressure, light)
 * and streams Serial Studio dashboard frames over USB-CDC serial at 1 Hz.
 *
 * No WiFi required — connect Serial Studio directly to the COM / tty port.
 *
 * Serial Studio setup:
 *   1. Open Serial Studio → File → Connect → Serial port
 *   2. Select your board's port and 115200 baud
 *   3. The dashboard will appear automatically once frames arrive
 *
 * Dependencies:
 *   - bblanchon/ArduinoJson @ ^7.0.0
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include "ss_dashboard.h"
#include "ss_dashboard_config.h"

// ─── Dashboard configuration ──────────────────────────────────────────────────

static const ss::ActionCfg kActions[] = {
    { .title = "Reset",     .txData = "reset",  .icon = "Done"  },
    { .title = "Calibrate", .txData = "cal",    .icon = "Settings" },
};

static const ss::DatasetCfg kEnvDatasets[] = {
    { .title = "Temperature", .units = "°C",
      .telemetryKey = "temp",
      .index = 1,
      .widget = ss::WidgetType::Gauge,
      .widgetMin = -20.0f, .widgetMax = 80.0f,
      .plotMin   = -20.0f, .plotMax   = 80.0f,
      .alarmEnabled = true, .alarmHigh = 40.0f, .alarmLow = -10.0f,
      .graph = true, .log = true, .overviewDisplay = true },

    { .title = "Humidity", .units = "%RH",
      .telemetryKey = "humidity",
      .index = 2,
      .widget = ss::WidgetType::Bar,
      .widgetMin = 0.0f, .widgetMax = 100.0f,
      .plotMin   = 0.0f, .plotMax   = 100.0f,
      .graph = true, .log = true, .overviewDisplay = true },

    { .title = "Pressure", .units = "hPa",
      .telemetryKey = "pressure",
      .index = 3,
      .plotMin = 900.0f, .plotMax = 1100.0f,
      .graph = true, .log = true },
};

static const ss::DatasetCfg kSysDatasets[] = {
    { .title = "Light", .units = "lux",
      .telemetryKey = "light",
      .index = 4,
      .widget = ss::WidgetType::Bar,
      .widgetMin = 0.0f, .widgetMax = 100000.0f,
      .graph = true },

    { .title = "Uptime", .units = "s",
      .telemetryKey = "uptime",
      .index = 5,
      .graph = true },
};

static const ss::GroupCfg kGroups[] = {
    { .title = "Environment",
      .widget = ss::GroupWidget::Multiplot,
      .datasets     = kEnvDatasets,
      .datasetCount = sizeof(kEnvDatasets) / sizeof(kEnvDatasets[0]) },

    { .title = "System",
      .widget = ss::GroupWidget::Datagrid,
      .datasets     = kSysDatasets,
      .datasetCount = sizeof(kSysDatasets) / sizeof(kSysDatasets[0]) },
};

static const ss::DashboardCfg kDashboardCfg = {
    .title      = "Environment Monitor",
    .groups     = kGroups,
    .groupCount = sizeof(kGroups) / sizeof(kGroups[0]),
    .actions     = kActions,
    .actionCount = sizeof(kActions) / sizeof(kActions[0]),
};

// ─── Module globals ───────────────────────────────────────────────────────────

static ss::Dashboard  dashboard(kDashboardCfg);
static char           txBuf[8192];

static uint32_t lastEmitMs = 0;
static constexpr uint32_t kEmitIntervalMs = 1000;

// ─── Simulated sensor readings ────────────────────────────────────────────────

static float simulateTemp()     { return 20.0f + 10.0f * sinf(millis() / 10000.0f); }
static float simulateHumidity() { return 50.0f + 20.0f * cosf(millis() /  7000.0f); }
static float simulatePressure() { return 1013.0f + 5.0f * sinf(millis() / 30000.0f); }
static float simulateLight()    { return 5000.0f + 4000.0f * sinf(millis() / 15000.0f); }

// ─── Setup ────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);   // wait for USB-CDC enumeration

    Serial.println("[app] Environment Monitor starting");

    dashboard.begin();
    Serial.printf("[app] Dashboard frame size: ~%u bytes\n",
                  static_cast<unsigned>(dashboard.estimateSize()));
}

// ─── Loop ─────────────────────────────────────────────────────────────────────

void loop() {
    const uint32_t now = millis();
    if (now - lastEmitMs < kEmitIntervalMs) return;
    lastEmitMs = now;

    // Build flat telemetry document.
    JsonDocument telemetry;
    telemetry["temp"]     = simulateTemp();
    telemetry["humidity"] = simulateHumidity();
    telemetry["pressure"] = simulatePressure();
    telemetry["light"]    = simulateLight();
    telemetry["uptime"]   = now / 1000UL;

    // Patch dashboard values and serialise.
    dashboard.update(telemetry);
    const size_t len = dashboard.serialize(txBuf, sizeof(txBuf));
    if (len > 0) {
        Serial.write(txBuf, len);
    } else {
        Serial.println("[app] serialize() failed — buffer too small?");
    }
}
