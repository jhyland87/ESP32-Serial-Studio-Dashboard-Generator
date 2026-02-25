/**
 * @file ss_dashboard.cpp
 * @brief Serial Studio dashboard JSON generator — implementation.
 */

#include "ss_dashboard.h"
#include <cstring>
#include <cstdio>
#include <cinttypes>

namespace ss {

// ─── String helpers for enum → JSON ──────────────────────────────────────────

const char* Dashboard::widgetStr(WidgetType w) {
    switch (w) {
        case WidgetType::Gauge: return "gauge";
        case WidgetType::Bar:   return "bar";
        case WidgetType::Led:   return "led";
        default:                return "";
    }
}

const char* Dashboard::groupWidgetStr(GroupWidget w) {
    switch (w) {
        case GroupWidget::Multiplot:     return "multiplot";
        case GroupWidget::Datagrid:      return "datagrid";
        case GroupWidget::Accelerometer: return "accelerometer";
        default:                         return "";
    }
}

// ─── Construction ────────────────────────────────────────────────────────────

Dashboard::Dashboard(const DashboardCfg& cfg)
    : cfg_(cfg)
{}

// ─── begin() — build the full JSON structure once ────────────────────────────

bool Dashboard::begin() {
    doc_.clear();
    slotCount_ = 0;

    doc_["title"] = cfg_.title ? cfg_.title : "Dashboard";

    buildActions();

    doc_["checksum"]            = "";
    doc_["decoder"]             = 0;
    doc_["hexadecimalDelimiters"] = false;

    auto layout = doc_["dashboardLayout"].to<JsonObject>();
    layout["autoLayout"] = true;
    layout["windowOrder"].to<JsonArray>();

    buildGroups();

    return true;
}

// ─── buildActions() ──────────────────────────────────────────────────────────

void Dashboard::buildActions() {
    auto actions = doc_["actions"].to<JsonArray>();

    for (uint8_t i = 0; i < cfg_.actionCount; ++i) {
        const auto& a = cfg_.actions[i];
        auto obj = actions.add<JsonObject>();

        obj["autoExecuteOnConnect"] = false;
        obj["binary"]               = false;
        obj["eol"]                  = a.eol  ? a.eol  : "\n";
        obj["icon"]                 = a.icon ? a.icon : "";
        obj["timerIntervalMs"]      = 100;
        obj["timerMode"]            = 0;
        obj["title"]                = a.title  ? a.title  : "";
        obj["txData"]               = a.txData ? a.txData : "";
    }
}

// ─── buildGroups() ───────────────────────────────────────────────────────────

void Dashboard::buildGroups() {
    auto groups = doc_["groups"].to<JsonArray>();

    for (uint8_t gi = 0; gi < cfg_.groupCount; ++gi) {
        const auto& grp = cfg_.groups[gi];
        auto gObj = groups.add<JsonObject>();

        gObj["title"]  = grp.title ? grp.title : "";
        gObj["widget"] = groupWidgetStr(grp.widget);

        auto datasets = gObj["datasets"].to<JsonArray>();

        for (uint8_t di = 0; di < grp.datasetCount; ++di) {
            const auto& ds = grp.datasets[di];
            auto dObj = datasets.add<JsonObject>();

            dObj["alarmEnabled"]    = ds.alarmEnabled;
            dObj["alarmHigh"]       = ds.alarmHigh;
            dObj["alarmLow"]        = ds.alarmLow;
            dObj["fft"]             = ds.fft;
            dObj["fftMax"]          = 0;
            dObj["fftMin"]          = 0;
            dObj["fftSamples"]      = ds.fftSamples;
            dObj["fftSamplingRate"] = ds.fftSamplingRate;
            dObj["graph"]           = ds.graph;
            dObj["index"]           = ds.index;
            dObj["led"]             = ds.led;
            dObj["ledHigh"]         = ds.ledHigh;
            dObj["log"]             = ds.log;
            dObj["overviewDisplay"] = ds.overviewDisplay;
            dObj["plotMax"]         = ds.plotMax;
            dObj["plotMin"]         = ds.plotMin;
            dObj["title"]           = ds.title ? ds.title : "";
            dObj["units"]           = ds.units ? ds.units : "";
            dObj["value"]           = "0";   // placeholder — update() patches this
            dObj["widget"]          = widgetStr(ds.widget);
            dObj["widgetMax"]       = ds.widgetMax;
            dObj["widgetMin"]       = ds.widgetMin;
            dObj["xAxis"]           = ds.xAxis;

            // Register a value slot if we have a telemetry key
            if (ds.telemetryKey && ds.telemetryKey[0] != '\0' &&
                slotCount_ < kMaxSlots)
            {
                slots_[slotCount_++] = {ds.telemetryKey, gi, di};
            }
        }
    }
}

// ─── resolveKey() — navigate dotted path in JSON ─────────────────────────────

const char* Dashboard::resolveKey(const JsonDocument& doc,
                                  const char* dottedKey,
                                  char* scratch,
                                  size_t scratchLen)
{
    if (!dottedKey || dottedKey[0] == '\0') return nullptr;

    // Copy key so we can tokenise it (strtok-style, but without modifying
    // the original).  Max depth = 4 levels should be more than enough.
    char keyBuf[64];
    const size_t keyLen = strlen(dottedKey);
    if (keyLen >= sizeof(keyBuf)) return nullptr;
    memcpy(keyBuf, dottedKey, keyLen + 1);

    // Walk the JSON tree one segment at a time.
    JsonVariantConst node = doc.as<JsonVariantConst>();

    char* savePtr = nullptr;
    char* token   = strtok_r(keyBuf, ".", &savePtr);
    while (token) {
        if (!node.is<JsonObjectConst>()) return nullptr;
        node = node[token];
        if (node.isNull()) return nullptr;
        token = strtok_r(nullptr, ".", &savePtr);
    }

    // Convert the leaf to a string.
    if (node.is<const char*>()) {
        return node.as<const char*>();
    }
    if (node.is<float>()) {
        snprintf(scratch, scratchLen, "%.6g", static_cast<double>(node.as<float>()));
        return scratch;
    }
    if (node.is<int64_t>()) {
        snprintf(scratch, scratchLen, "%" PRId64, node.as<int64_t>());
        return scratch;
    }
    if (node.is<uint64_t>()) {
        snprintf(scratch, scratchLen, "%" PRIu64, node.as<uint64_t>());
        return scratch;
    }
    if (node.is<bool>()) {
        snprintf(scratch, scratchLen, "%u", node.as<bool>() ? 1u : 0u);
        return scratch;
    }
    return nullptr;
}

// ─── update() — patch all "value" fields from telemetry ──────────────────────

void Dashboard::update(const JsonDocument& telemetry) {
    char scratch[32];

    auto groups = doc_["groups"].as<JsonArray>();

    for (uint8_t s = 0; s < slotCount_; ++s) {
        const auto& slot = slots_[s];

        const char* val = resolveKey(telemetry, slot.telemetryKey,
                                     scratch, sizeof(scratch));
        if (!val) continue;

        // Navigate to the dataset and set "value".
        auto ds = groups[slot.groupIdx]["datasets"][slot.datasetIdx];
        if (!ds.isNull()) {
            ds["value"] = val;
        }
    }
}

// ─── estimateSize() ──────────────────────────────────────────────────────────

size_t Dashboard::estimateSize() const {
    // measureJson returns the compacted JSON length (no NUL).
    // We add 4 for "/*" prefix + "*/" suffix, 2 for "\r\n", plus 1 for NUL.
    return measureJson(doc_) + 7;
}

// ─── serialize() — write "/*{…JSON…}*/" into buffer ──────────────────────────

size_t Dashboard::serialize(char* buf, size_t bufLen) const {
    if (!buf || bufLen < 6) return 0;   // minimum: "/*{}*/" + NUL

    // Write prefix
    buf[0] = '/';
    buf[1] = '*';

    // Serialise JSON after the 2-byte prefix.
    // Leave room for prefix(2) + suffix(2) + CRLF(2) + NUL(1) = 7 overhead bytes.
    const size_t room    = bufLen - 7;
    const size_t jsonLen = serializeJson(doc_, static_cast<void*>(buf + 2), room);
    if (jsonLen == 0) return 0;

    // Write suffix: "*/" followed by "\r\n"
    const size_t pos = 2 + jsonLen;
    if (pos + 4 >= bufLen) return 0;    // no room for suffix + CRLF + NUL
    buf[pos]     = '*';
    buf[pos + 1] = '/';
    buf[pos + 2] = '\r';
    buf[pos + 3] = '\n';
    buf[pos + 4] = '\r';
    buf[pos + 5] = '\n';
    buf[pos + 6] = '\0';

    return pos + 6;   // total bytes written (excluding NUL)
}

} // namespace ss
