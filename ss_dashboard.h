/**
 * @file ss_dashboard.h
 * @brief Serial Studio dashboard JSON generator.
 *
 * Builds and maintains a Serial Studio–compatible JSON document from a
 * compile-time configuration (ss::DashboardCfg).  On each telemetry update
 * the caller provides a JsonDocument containing the latest sensor readings;
 * the Dashboard class patches the "value" field of every dataset and
 * serialises the result, wrapped in the required  / *  …  * /  delimiters,
 * ready for WebSocket broadcast.
 *
 * The library depends only on ArduinoJson — it is intentionally decoupled
 * from FrameBuilder and any other project-specific types.
 */

#pragma once

#include <ArduinoJson.h>
#include "ss_dashboard_config.h"

namespace ss {

class Dashboard {
public:
    // Maximum number of dataset→telemetry mappings.
    static constexpr uint8_t kMaxSlots = 48;

    /**
     * Construct a Dashboard from the supplied configuration.
     *
     * @param cfg  Dashboard configuration — must outlive the Dashboard object.
     */
    explicit Dashboard(const DashboardCfg& cfg);

    /**
     * Build the initial JSON document from the configuration.
     * Call once during setup().
     *
     * @return true on success; false if the config is invalid.
     */
    bool begin();

    /**
     * Update every dataset "value" field from the latest telemetry.
     *
     * @param telemetry  Nested JSON produced by FrameBuilder::fillJson().
     *                   Keys are dot-separated paths matching the DatasetCfg
     *                   telemetryKey fields (e.g. {"temperature":{"k":78.4}}).
     */
    void update(const JsonDocument& telemetry);

    /**
     * Serialise the dashboard JSON, wrapped in  / *  …  * /  delimiters.
     *
     * @param buf     Destination buffer.
     * @param bufLen  Size of @p buf in bytes.
     * @return        Bytes written (excluding NUL), or 0 on failure.
     */
    size_t serialize(char* buf, size_t bufLen) const;

    /**
     * Estimate the buffer size needed by serialize().
     * Includes the two-byte prefix, two-byte suffix, and NUL.
     */
    size_t estimateSize() const;

private:
    const DashboardCfg& cfg_;
    JsonDocument        doc_;

    // ── Pre-computed value-slot table ────────────────────────────────────────
    //
    // Built once in begin().  Each slot records the telemetry key and the
    // (group, dataset) position inside doc_ so that update() can patch
    // values without re-walking the config.

    struct ValueSlot {
        const char* telemetryKey;   ///< Dotted path (borrowed from config)
        uint8_t     groupIdx;
        uint8_t     datasetIdx;
    };

    ValueSlot slots_[kMaxSlots];
    uint8_t   slotCount_ = 0;

    // ── Internal helpers ─────────────────────────────────────────────────────

    void buildActions();
    void buildGroups();

    static const char* widgetStr(WidgetType w);
    static const char* groupWidgetStr(GroupWidget w);

    /**
     * Resolve a dotted key path (e.g. "temperature.k") inside a JsonDocument.
     *
     * If the leaf is numeric the value is formatted into @p scratch and a
     * pointer to that buffer is returned.  If it is already a string the
     * pointer into the JsonDocument is returned directly.
     *
     * @return Pointer to value string, or nullptr if the path does not exist.
     */
    static const char* resolveKey(const JsonDocument& doc,
                                  const char* dottedKey,
                                  char* scratch,
                                  size_t scratchLen);
};

} // namespace ss
