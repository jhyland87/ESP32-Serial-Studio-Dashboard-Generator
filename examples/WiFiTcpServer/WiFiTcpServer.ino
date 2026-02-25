/**
 * @file WiFiTcpServer.ino
 * @brief WiFi + AsyncTCP raw-TCP server example for Serial Studio.
 *
 * Streams Serial Studio dashboard frames to TCP clients on port 8080.
 * A dedicated FreeRTOS task (Core 0) handles all TCP I/O, so the Arduino
 * loop (Core 1) is never blocked.
 *
 * Background — why a separate task?
 * ───────────────────────────────────
 * AsyncTCP's add()/send() route through tcpip_api_call(), which is a
 * synchronous cross-task IPC: the calling task blocks until the lwIP TCPIP
 * task processes the request.  A ~10 KB dashboard frame may require several
 * such calls (one per chunk that fits in tcp_sndbuf, typically 5–6 KB).
 * Doing this on Core 1 would stall the main loop; Core 0 can block freely
 * because it is the WiFi / TCPIP core.
 *
 * Serial Studio setup:
 *   1. Open Serial Studio → File → Connect → Network → TCP Client
 *   2. Host: <board IP printed on Serial>, Port: 8080
 *   3. The dashboard will appear automatically once connected.
 *   4. Use the action buttons to send commands back to the device.
 *
 * Dependencies:
 *   - bblanchon/ArduinoJson  @ ^7.0.0
 *   - esp32async/AsyncTCP    @ ^3.4.10   (NOT the deprecated me-no-dev version)
 *
 * platformio.ini:
 *   lib_deps =
 *       bblanchon/ArduinoJson@^7.0.0
 *       esp32async/AsyncTCP@^3.4.10
 *   lib_ignore =
 *       ESPAsyncTCP
 *       RPAsyncTCP
 */

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "ss_dashboard.h"
#include "ss_dashboard_config.h"

// ─── Credentials — change before flashing ────────────────────────────────────

static constexpr char kSsid[]     = "YourSSID";
static constexpr char kPassword[] = "YourPassword";
static constexpr uint16_t kPort   = 8080;

// ─── Dashboard configuration ──────────────────────────────────────────────────

static const ss::ActionCfg kActions[] = {
    { .title = "Start",  .txData = "start",  .icon = "Done"     },
    { .title = "Stop",   .txData = "stop",   .icon = "Close"    },
    { .title = "Status", .txData = "status", .icon = "System Task" },
};

static const ss::DatasetCfg kEnvDatasets[] = {
    { .title = "Temperature", .units = "°C",
      .telemetryKey = "temp",
      .index = 1,
      .widget = ss::WidgetType::Gauge,
      .widgetMin = -20.0f, .widgetMax = 80.0f,
      .plotMin   = -20.0f, .plotMax   = 80.0f,
      .alarmEnabled = true, .alarmHigh = 50.0f,
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

static const ss::DatasetCfg kImuDatasets[] = {
    { .title = "Accel X", .units = "m/s²",
      .telemetryKey = "imu.ax",
      .index = 4, .graph = true },
    { .title = "Accel Y", .units = "m/s²",
      .telemetryKey = "imu.ay",
      .index = 5, .graph = true },
    { .title = "Accel Z", .units = "m/s²",
      .telemetryKey = "imu.az",
      .index = 6,
      .widget = ss::WidgetType::Gauge,
      .widgetMin = -20.0f, .widgetMax = 20.0f,
      .graph = true },
};

static const ss::DatasetCfg kSysDatasets[] = {
    { .title = "Uptime", .units = "s",
      .telemetryKey = "uptime",
      .index = 7, .graph = true },

    { .title = "Free Heap", .units = "bytes",
      .telemetryKey = "heap",
      .index = 8, .graph = true },
};

static const ss::GroupCfg kGroups[] = {
    { .title = "Environment",
      .widget = ss::GroupWidget::Multiplot,
      .datasets     = kEnvDatasets,
      .datasetCount = sizeof(kEnvDatasets) / sizeof(kEnvDatasets[0]) },

    { .title = "IMU",
      .widget = ss::GroupWidget::Accelerometer,
      .datasets     = kImuDatasets,
      .datasetCount = sizeof(kImuDatasets) / sizeof(kImuDatasets[0]) },

    { .title = "System",
      .widget = ss::GroupWidget::Datagrid,
      .datasets     = kSysDatasets,
      .datasetCount = sizeof(kSysDatasets) / sizeof(kSysDatasets[0]) },
};

static const ss::DashboardCfg kDashboardCfg = {
    .title      = "ESP32 Monitor",
    .groups     = kGroups,
    .groupCount = sizeof(kGroups) / sizeof(kGroups[0]),
    .actions     = kActions,
    .actionCount = sizeof(kActions) / sizeof(kActions[0]),
};

// ─── TCP server state ─────────────────────────────────────────────────────────

static constexpr uint8_t kMaxClients = 4;

static AsyncServer  tcpServer(kPort);
static AsyncClient* clients[kMaxClients] = {};
static ss::Dashboard dashboard(kDashboardCfg);

// Transmit buffer — large enough for the full JSON frame.
static constexpr size_t kTxBufSize = 16384;
static char txBuf[kTxBufSize];

// ─── Client management ────────────────────────────────────────────────────────

static void removeClient(AsyncClient* c) {
    for (auto& slot : clients) {
        if (slot == c) { slot = nullptr; return; }
    }
}

static void onClientDisconnect(void* /*arg*/, AsyncClient* c) {
    Serial.printf("[tcp] Client %s disconnected\n",
                  c->remoteIP().toString().c_str());
    removeClient(c);
}

static void onClientError(void* /*arg*/, AsyncClient* c, int8_t err) {
    Serial.printf("[tcp] Client %s error %d\n",
                  c->remoteIP().toString().c_str(), (int)err);
    removeClient(c);
}

static void onClientData(void* /*arg*/, AsyncClient* c, void* data, size_t len) {
    // Commands from Serial Studio action buttons arrive here (e.g. "start\n").
    // data is NOT null-terminated; cap and null-terminate it before use.
    static constexpr size_t kMaxCmd = 80;
    char buf[kMaxCmd + 1];
    const size_t copyLen = (len < kMaxCmd) ? len : kMaxCmd;
    memcpy(buf, data, copyLen);
    buf[copyLen] = '\0';

    // Strip trailing CR / LF.
    size_t end = copyLen;
    while (end > 0 && (buf[end - 1] == '\r' || buf[end - 1] == '\n')) --end;
    buf[end] = '\0';

    if (end > 0) {
        Serial.printf("[tcp] Client %s cmd: %s\n",
                      c->remoteIP().toString().c_str(), buf);
        // Handle the command here (or dispatch to your command handler).
    }
}

static void onNewClient(void* /*arg*/, AsyncClient* c) {
    for (auto& slot : clients) {
        if (!slot) {
            slot = c;
            c->onDisconnect(&onClientDisconnect, nullptr);
            c->onError(&onClientError, nullptr);
            c->onData(&onClientData, nullptr);
            Serial.printf("[tcp] Client %s connected\n",
                          c->remoteIP().toString().c_str());
            return;
        }
    }
    Serial.println("[tcp] Max clients reached, rejecting");
    c->close();
}

// ─── Chunked TCP send ─────────────────────────────────────────────────────────

/**
 * Send the full frame to a client in chunks, yielding between each chunk.
 *
 * tcp_sndbuf() is typically only 5744 bytes, so a 10 KB frame must be
 * split.  vTaskDelay() yields to the TCPIP task so it can drain the window
 * before the next chunk is queued.  This must run in a FreeRTOS task
 * (not the Arduino loop) so the vTaskDelay does not stall the main loop.
 */
static void sendChunked(AsyncClient* c, const char* data, size_t len) {
    size_t offset = 0;
    while (offset < len) {
        if (!c || !c->connected()) return;

        if (!c->canSend()) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        const size_t written = c->add(data + offset, len - offset);
        if (written == 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        c->send();
        offset += written;

        if (offset < len) {
            vTaskDelay(pdMS_TO_TICKS(5));  // let TCPIP drain the window
        }
    }
}

// ─── Dashboard FreeRTOS task ──────────────────────────────────────────────────

// NOTE: on ESP-IDF, the usStackDepth parameter to xTaskCreatePinnedToCore
// is in BYTES (unlike vanilla FreeRTOS where it is in words).
static constexpr uint32_t    kTaskStackBytes = 8192;
static constexpr UBaseType_t kTaskPriority   = 1;
static constexpr uint32_t    kBroadcastMs    = 1000;

static void dashboardTask(void* /*arg*/) {
    TickType_t lastWake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(kBroadcastMs));

        // Check for at least one connected client.
        bool anyConnected = false;
        for (const auto* c : clients) {
            if (c && c->connected()) { anyConnected = true; break; }
        }
        if (!anyConnected) continue;

        // Build telemetry document.  Replace with real sensor reads.
        JsonDocument telemetry;
        telemetry["temp"]     = 20.0f + 5.0f * sinf(millis() / 8000.0f);
        telemetry["humidity"] = 55.0f;
        telemetry["pressure"] = 1013.0f;
        telemetry["imu"]["ax"] = 0.1f;
        telemetry["imu"]["ay"] = 0.2f;
        telemetry["imu"]["az"] = 9.81f;
        telemetry["uptime"]   = millis() / 1000UL;
        telemetry["heap"]     = static_cast<uint32_t>(ESP.getFreeHeap());

        dashboard.update(telemetry);
        const size_t len = dashboard.serialize(txBuf, kTxBufSize);
        if (len == 0) {
            Serial.println("[dashboard] serialize() failed");
            continue;
        }

        for (auto* c : clients) {
            if (!c || !c->connected()) continue;
            sendChunked(c, txBuf, len);
        }
    }
}

// ─── WiFi setup ──────────────────────────────────────────────────────────────

static void connectWifi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(kSsid, kPassword);
    Serial.print("[wifi] Connecting");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    Serial.printf("[wifi] IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[tcp]  Connect Serial Studio to %s:%u\n",
                  WiFi.localIP().toString().c_str(), kPort);
}

// ─── Setup / Loop ─────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);

    connectWifi();

    dashboard.begin();
    Serial.printf("[dashboard] Frame size: ~%u bytes\n",
                  static_cast<unsigned>(dashboard.estimateSize()));

    tcpServer.onClient(&onNewClient, nullptr);
    tcpServer.begin();

    // Pin the dashboard task to Core 0 (the WiFi/TCPIP core) so that
    // tcpip_api_call() round-trips never block the Arduino loop on Core 1.
    xTaskCreatePinnedToCore(
        dashboardTask, "dashboard",
        kTaskStackBytes, nullptr,
        kTaskPriority, nullptr,
        0  // Core 0
    );
}

void loop() {
    // Main application logic here — sensors, state machine, etc.
    // The dashboard task runs independently on Core 0.
    delay(10);
}
