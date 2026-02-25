/**
 * @file ss_dashboard_config.h
 * @brief Configuration structs for Serial Studio dashboard generation.
 *
 * Users define their dashboard layout using these plain C++ structs.
 * Aggregate initialization makes configuration concise and readable.
 *
 * Example:
 * @code
 *   static const ss::DatasetCfg tempDatasets[] = {
 *       { .title = "Temperature", .units = "K",
 *         .telemetryKey = "temperature.k", .index = 4,
 *         .widget = ss::WidgetType::Gauge,
 *         .widgetMin = 60, .widgetMax = 300,
 *         .plotMin = 60,   .plotMax = 310,
 *         .graph = true, .log = true, .overviewDisplay = true },
 *   };
 * @endcode
 */

#pragma once

#include <cstdint>

namespace ss {

// ─── Widget types ────────────────────────────────────────────────────────────

/** Per-dataset widget type. */
enum class WidgetType : uint8_t {
    None,           ///< No widget
    Gauge,          ///< Radial gauge
    Bar,            ///< Horizontal / vertical bar
    Led             ///< LED indicator
};

/** Per-group widget type. */
enum class GroupWidget : uint8_t {
    None,           ///< No group widget
    Multiplot,      ///< Overlaid line graphs
    Datagrid,       ///< Tabular data view
    Accelerometer   ///< 3-axis accelerometer view
};

// ─── Dataset configuration ───────────────────────────────────────────────────

/**
 * Configuration for a single dataset (data channel) in the dashboard.
 *
 * @note Fields that aren't explicitly initialised in aggregate init
 *       will be zero-initialised (false, 0, 0.0f, nullptr).
 */
struct DatasetCfg {
    const char* title           = nullptr;
    const char* units           = "";
    const char* telemetryKey    = nullptr;   ///< Dotted path into telemetry JSON
    uint8_t     index           = 0;         ///< 1-based Serial Studio dataset index
    WidgetType  widget          = WidgetType::None;
    float       widgetMin       = 0.0f;
    float       widgetMax       = 0.0f;
    float       plotMin         = 0.0f;
    float       plotMax         = 0.0f;
    float       alarmLow        = 0.0f;
    float       alarmHigh       = 0.0f;
    bool        alarmEnabled    = false;
    bool        graph           = false;
    bool        log             = false;
    bool        led             = false;
    uint8_t     ledHigh         = 0;
    bool        overviewDisplay = false;
    bool        fft             = false;
    uint16_t    fftSamples      = 256;
    uint16_t    fftSamplingRate = 100;
    int8_t      xAxis           = -1;
};

// ─── Group configuration ─────────────────────────────────────────────────────

/** Configuration for a dashboard group (collection of datasets). */
struct GroupCfg {
    const char*       title        = nullptr;
    GroupWidget       widget       = GroupWidget::None;
    const DatasetCfg* datasets     = nullptr;
    uint8_t           datasetCount = 0;
};

// ─── Action configuration ────────────────────────────────────────────────────

/** Configuration for a Serial Studio action button. */
struct ActionCfg {
    const char* title   = nullptr;
    const char* txData  = nullptr;  ///< Data string sent on button press
    const char* icon    = nullptr;  ///< Serial Studio icon name
    const char* eol     = "\n";     ///< End-of-line appended to txData
};

// ─── Top-level dashboard configuration ───────────────────────────────────────

/** Complete dashboard configuration. */
struct DashboardCfg {
    const char*       title       = nullptr;
    const GroupCfg*   groups      = nullptr;
    uint8_t           groupCount  = 0;
    const ActionCfg*  actions     = nullptr;
    uint8_t           actionCount = 0;
};

} // namespace ss
