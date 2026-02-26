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
#include "ss_icons.h"



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
     * @param pretty  If true, use indented (pretty-printed) JSON instead of
     *                compact JSON.  Pretty output is ~3–4× larger; ensure
     *                @p buf is sized accordingly.  Default: false.
     * @return        Bytes written (excluding NUL), or 0 on failure.
     */
    size_t serialize(char* buf, size_t bufLen, bool pretty = false) const;

    /**
     * Estimate the minimum buffer size needed by serialize(compact).
     * For pretty mode allocate at least estimateSize() * 4.
     * Includes the two-byte prefix, suffix, CRLF, and NUL overhead.
     */
    size_t estimateSize() const;

    /**
     * Convert an Icon enum value to a string.
     *
     * @param icon  Icon enum value.
     * @return      String representation of the icon.
     */
    const char* iconToString(ss::DashboardIcon icon) const;

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


//------------------------------------------------------------------------------
// Standard keys used in Serial Studio JSON files
//------------------------------------------------------------------------------

namespace Keys
{
inline constexpr auto EOL = "eol";
inline constexpr auto Icon = "icon";
inline constexpr auto Title = "title";
inline constexpr auto TxData = "txData";
inline constexpr auto Binary = "binary";
inline constexpr auto TimerMode = "timerMode";
inline constexpr auto TimerInterval = "timerIntervalMs";
inline constexpr auto AutoExecute = "autoExecuteOnConnect";

inline constexpr auto FFT = "fft";
inline constexpr auto LED = "led";
inline constexpr auto Log = "log";
inline constexpr auto Min = "min";
inline constexpr auto Max = "max";
inline constexpr auto Graph = "graph";
inline constexpr auto Index = "index";
inline constexpr auto XAxis = "xAxis";
inline constexpr auto Alarm = "alarm";
inline constexpr auto Units = "units";
inline constexpr auto Value = "value";
inline constexpr auto Widget = "widget";
inline constexpr auto FFTMin = "fftMin";
inline constexpr auto FFTMax = "fftMax";
inline constexpr auto PltMin = "plotMin";
inline constexpr auto PltMax = "plotMax";
inline constexpr auto LedHigh = "ledHigh";
inline constexpr auto WgtMin = "widgetMin";
inline constexpr auto WgtMax = "widgetMax";
inline constexpr auto AlarmLow = "alarmLow";
inline constexpr auto AlarmHigh = "alarmHigh";
inline constexpr auto FFTSamples = "fftSamples";
inline constexpr auto Overview = "overviewDisplay";
inline constexpr auto AlarmEnabled = "alarmEnabled";
inline constexpr auto FFTSamplingRate = "fftSamplingRate";

inline constexpr auto Groups = "groups";
inline constexpr auto Actions = "actions";
inline constexpr auto Datasets = "datasets";

inline constexpr auto DashboardLayout = "dashboardLayout";
inline constexpr auto ActiveGroupId = "activeGroupId";
} // namespace Keys

} // namespace ss
