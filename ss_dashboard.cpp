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

    doc_[ss::Keys::Title] = cfg_.title ? cfg_.title : "Dashboard";

    buildActions();

    doc_["checksum"]            = "";
    doc_["decoder"]             = 0;
    doc_["hexadecimalDelimiters"] = false;

    auto layout = doc_[ss::Keys::DashboardLayout].to<JsonObject>();
    layout["autoLayout"] = true;
    layout["windowOrder"].to<JsonArray>();

    buildGroups();

    return true;
}

// ─── buildActions() ──────────────────────────────────────────────────────────

void Dashboard::buildActions() {
    auto actions = doc_[ss::Keys::Actions].to<JsonArray>();

    for (uint8_t i = 0; i < cfg_.actionCount; ++i) {
        const auto& a = cfg_.actions[i];
        auto obj = actions.add<JsonObject>();

        obj[ss::Keys::AutoExecute] = false;
        obj[ss::Keys::Binary]               = false;
        obj[ss::Keys::EOL]                  = a.eol  ? a.eol  : "\n";
        obj[ss::Keys::Icon]                 = a.icon ? a.icon : "";
        obj[ss::Keys::TimerInterval]        = 100;
        obj[ss::Keys::TimerMode]            = 0;
        obj[ss::Keys::Title]                = a.title  ? a.title  : "";
        obj[ss::Keys::TxData]               = a.txData ? a.txData : "";
    }
}

// ─── buildGroups() ───────────────────────────────────────────────────────────

void Dashboard::buildGroups() {
    auto groups = doc_[ss::Keys::Groups].to<JsonArray>();

    for (uint8_t gi = 0; gi < cfg_.groupCount; ++gi) {
        const auto& grp = cfg_.groups[gi];
        auto gObj = groups.add<JsonObject>();

        gObj[ss::Keys::Title]  = grp.title ? grp.title : "";
        gObj[ss::Keys::Widget] = groupWidgetStr(grp.widget);

        auto datasets = gObj[ss::Keys::Datasets].to<JsonArray>();

        for (uint8_t di = 0; di < grp.datasetCount; ++di) {
            const auto& ds = grp.datasets[di];
            auto dObj = datasets.add<JsonObject>();

            dObj[ss::Keys::AlarmEnabled]    = ds.alarmEnabled;
            dObj[ss::Keys::AlarmHigh]       = ds.alarmHigh;
            dObj[ss::Keys::AlarmLow]        = ds.alarmLow;
            dObj[ss::Keys::FFT]             = ds.fft;
            dObj[ss::Keys::FFTMax]          = 0;
            dObj[ss::Keys::FFTMin]          = 0;
            dObj[ss::Keys::FFTSamples]      = ds.fftSamples;
            dObj[ss::Keys::FFTSamplingRate] = ds.fftSamplingRate;
            dObj[ss::Keys::Graph]           = ds.graph;
            dObj[ss::Keys::Index]           = ds.index;
            dObj[ss::Keys::LED]             = ds.led;
            dObj[ss::Keys::LedHigh]         = ds.ledHigh;
            dObj[ss::Keys::Log]             = ds.log;
            dObj[ss::Keys::Overview]        = ds.overviewDisplay;
            dObj[ss::Keys::PltMax]          = ds.plotMax;
            dObj[ss::Keys::PltMin]          = ds.plotMin;
            dObj[ss::Keys::Title]           = ds.title ? ds.title : "";
            dObj[ss::Keys::Units]           = ds.units ? ds.units : "";
            dObj[ss::Keys::Value]           = "0";   // placeholder — update() patches this
            dObj[ss::Keys::Widget]          = widgetStr(ds.widget);
            dObj[ss::Keys::WgtMax]          = ds.widgetMax;
            dObj[ss::Keys::WgtMin]          = ds.widgetMin;
            dObj[ss::Keys::XAxis]           = ds.xAxis;

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

    auto groups = doc_[ss::Keys::Groups].as<JsonArray>();

    for (uint8_t s = 0; s < slotCount_; ++s) {
        const auto& slot = slots_[s];

        const char* val = resolveKey(telemetry, slot.telemetryKey,
                                     scratch, sizeof(scratch));
        if (!val) continue;

        // Navigate to the dataset and set "value".
        auto ds = groups[slot.groupIdx][ss::Keys::Datasets][slot.datasetIdx];
        if (!ds.isNull()) {
            ds[ss::Keys::Value] = val;
        }
    }
}

static const char* iconToString(DashboardIcon icon) {
    auto it = DashboardIconMap.find(icon);
    if (it != DashboardIconMap.end()) {
        return it->second.c_str();
    }
    return nullptr;
}

// ─── estimateSize() ──────────────────────────────────────────────────────────

size_t Dashboard::estimateSize() const {
    // measureJson returns the compacted JSON length (no NUL).
    // We add 4 for "/*" prefix + "*/" suffix, 2 for "\r\n", plus 1 for NUL.
    return measureJson(doc_) + 7;
}

// ─── serialize() — write "/*{…JSON…}*/" into buffer ──────────────────────────

size_t Dashboard::serialize(char* buf, size_t bufLen, bool pretty) const {
    if (!buf || bufLen < 6) {
#ifdef ARDUINO
        Serial.printf("[ss] serialize: null buf or bufLen(%u) < 6\n",
                      static_cast<unsigned>(bufLen));
#endif
        return 0;
    }

    // Write prefix.
    buf[0] = '/';
    buf[1] = '*';

    // Leave room for: prefix(2) + suffix(2) + CRLF×2(4) + NUL(1) = 9 bytes
    // overhead, plus one extra '\n' before '*/' in pretty mode.
    // pretty-printed JSON is ~3–4× the compact size; callers must supply
    // a buffer sized accordingly (estimateSize() * 4 is a safe bound).
    if (bufLen < 9) {
#ifdef ARDUINO
        Serial.printf("[ss] serialize: bufLen(%u) < 9\n",
                      static_cast<unsigned>(bufLen));
#endif
        return 0;
    }
    const size_t room = bufLen - 9;

    const size_t jsonLen = pretty
        ? serializeJsonPretty(doc_, static_cast<void*>(buf + 2), room)
        :         serializeJson(doc_, static_cast<void*>(buf + 2), room);

    if (jsonLen == 0) {
#ifdef ARDUINO
        Serial.printf("[ss] serialize: serializeJson returned 0 "
                      "(doc empty? room=%u)\n", static_cast<unsigned>(room));
#endif
        return 0;
    }

    // Write suffix: "\n*/\r\n\r\n" in pretty mode (delimiter on its own line);
    //               "*/\r\n\r\n"  in compact mode.
    size_t pos = 2 + jsonLen;
    const size_t need = pretty ? 8u : 7u;  // bytes needed after pos (incl. NUL)
    if (pos + need > bufLen) {
#ifdef ARDUINO
        Serial.printf("[ss] serialize: suffix overflow "
                      "(pos=%u need=%u bufLen=%u jsonLen=%u)\n",
                      static_cast<unsigned>(pos),
                      static_cast<unsigned>(need),
                      static_cast<unsigned>(bufLen),
                      static_cast<unsigned>(jsonLen));
#endif
        return 0;
    }

    if (pretty) buf[pos++] = '\n';  // newline before */ in pretty mode
    buf[pos++] = '*';
    buf[pos++] = '/';
    buf[pos++] = '\r';
    buf[pos++] = '\n';
    buf[pos++] = '\r';
    buf[pos++] = '\n';
    buf[pos]   = '\0';

    return pos;   // bytes written, excluding NUL
}

} // namespace ss
