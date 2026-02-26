/**
 * @file test_ss_dashboard.cpp
 * @brief Native unit tests for the ss::Dashboard library.
 *
 * Verifies JSON generation, value updates, and serialisation without
 * any hardware dependencies.
 *
 * This file has no main().  It exposes run_dashboard_tests() which is
 * called from test_state_machine.cpp::main().
 *
 * Run with:  pio test -e native
 */

#include <unity.h>
#include <cstring>
#include <ArduinoJson.h>
#include "ss_dashboard.h"
#include "ss_dashboard_config.h"

// ─── Minimal test configuration ─────────────────────────────────────────────

static const ss::DatasetCfg kTestDatasets[] = {
    {
        .title        = "Temp K",
        .units        = "K",
        .telemetryKey = "temperature.k",
        .index        = 4,
        .widget       = ss::WidgetType::Gauge,
        .widgetMin    = 60,    .widgetMax  = 300,
        .plotMin      = 60,    .plotMax    = 310,
        .alarmLow     = 60,    .alarmHigh  = 300,
        .graph        = true,  .log = true,
        .overviewDisplay = true,
    },
    {
        .title        = "State",
        .units        = "",
        .telemetryKey = "state.name",
        .index        = 2,
    },
};

static const ss::GroupCfg kTestGroups[] = {
    {
        .title        = "Test Group",
        .widget       = ss::GroupWidget::Multiplot,
        .datasets     = kTestDatasets,
        .datasetCount = 2,
    },
};

static const ss::ActionCfg kTestActions[] = {
    { .title = "Go", .txData = "go", .icon = "Play", .eol = "\n" },
};

static const ss::DashboardCfg kTestCfg = {
    .title       = "Test Dashboard",
    .groups      = kTestGroups,
    .groupCount  = 1,
    .actions     = kTestActions,
    .actionCount = 1,
};

// ─── Tests ───────────────────────────────────────────────────────────────────

void test_dashboard_begin_creates_valid_json(void) {
    ss::Dashboard dash(kTestCfg);
    TEST_ASSERT_TRUE(dash.begin());
    TEST_ASSERT_GREATER_THAN(0, dash.estimateSize());
}

void test_dashboard_serialize_has_delimiters(void) {
    ss::Dashboard dash(kTestCfg);
    dash.begin();

    char buf[4096];
    size_t len = dash.serialize(buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(4, len);

    // Check /* prefix
    TEST_ASSERT_EQUAL_CHAR('/', buf[0]);
    TEST_ASSERT_EQUAL_CHAR('*', buf[1]);

    // Check */\r\n suffix
    TEST_ASSERT_EQUAL_CHAR('*', buf[len - 4]);
    TEST_ASSERT_EQUAL_CHAR('/', buf[len - 3]);
    TEST_ASSERT_EQUAL_CHAR('\r', buf[len - 2]);
    TEST_ASSERT_EQUAL_CHAR('\n', buf[len - 1]);
}

void test_dashboard_serialize_contains_title(void) {
    ss::Dashboard dash(kTestCfg);
    dash.begin();

    char buf[4096];
    dash.serialize(buf, sizeof(buf));

    TEST_ASSERT_NOT_NULL(strstr(buf, "\"Test Dashboard\""));
}

void test_dashboard_serialize_contains_group(void) {
    ss::Dashboard dash(kTestCfg);
    dash.begin();

    char buf[4096];
    dash.serialize(buf, sizeof(buf));

    TEST_ASSERT_NOT_NULL(strstr(buf, "\"Test Group\""));
}

void test_dashboard_serialize_contains_dataset(void) {
    ss::Dashboard dash(kTestCfg);
    dash.begin();

    char buf[4096];
    dash.serialize(buf, sizeof(buf));

    TEST_ASSERT_NOT_NULL(strstr(buf, "\"Temp K\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"gauge\""));
}

void test_dashboard_serialize_contains_action(void) {
    ss::Dashboard dash(kTestCfg);
    dash.begin();

    char buf[4096];
    dash.serialize(buf, sizeof(buf));

    TEST_ASSERT_NOT_NULL(strstr(buf, "\"Go\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"go\""));
}

void test_dashboard_update_patches_numeric_value(void) {
    ss::Dashboard dash(kTestCfg);
    dash.begin();

    // Simulate telemetry JSON: { "temperature": { "k": 78.45 } }
    JsonDocument telemetry;
    telemetry["temperature"]["k"] = 78.45f;
    telemetry["state"]["name"]    = "CoarseCooldown";

    dash.update(telemetry);

    char buf[4096];
    dash.serialize(buf, sizeof(buf));

    // The temperature value should appear (formatted as a number string)
    TEST_ASSERT_NOT_NULL(strstr(buf, "78.45"));
}

void test_dashboard_update_patches_string_value(void) {
    ss::Dashboard dash(kTestCfg);
    dash.begin();

    JsonDocument telemetry;
    telemetry["temperature"]["k"] = 100.0f;
    telemetry["state"]["name"]    = "FineCooldown";

    dash.update(telemetry);

    char buf[4096];
    dash.serialize(buf, sizeof(buf));

    TEST_ASSERT_NOT_NULL(strstr(buf, "FineCooldown"));
}

void test_dashboard_update_preserves_structure(void) {
    ss::Dashboard dash(kTestCfg);
    dash.begin();

    // First update
    JsonDocument t1;
    t1["temperature"]["k"] = 200.0f;
    t1["state"]["name"]    = "Off";
    dash.update(t1);

    // Second update
    JsonDocument t2;
    t2["temperature"]["k"] = 78.0f;
    t2["state"]["name"]    = "Operating";
    dash.update(t2);

    char buf[4096];
    dash.serialize(buf, sizeof(buf));

    // Should have new values, not old
    TEST_ASSERT_NOT_NULL(strstr(buf, "Operating"));
    TEST_ASSERT_NULL(strstr(buf, "\"Off\""));  // "Off" should be gone
    // Structure should still be intact
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"Test Group\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"multiplot\""));
}

void test_dashboard_serialize_buffer_too_small(void) {
    ss::Dashboard dash(kTestCfg);
    dash.begin();

    char tiny[4];
    size_t len = dash.serialize(tiny, sizeof(tiny));
    TEST_ASSERT_EQUAL(0, len);
}

void test_dashboard_initial_values_are_zero(void) {
    ss::Dashboard dash(kTestCfg);
    dash.begin();

    char buf[4096];
    dash.serialize(buf, sizeof(buf));

    // Before any update, values should be "0" (the placeholder)
    // Parse the JSON to check
    JsonDocument doc;
    // Skip the /* prefix
    DeserializationError err = deserializeJson(doc, buf + 2);
    TEST_ASSERT_TRUE(err == DeserializationError::Ok);

    const char* val = doc["groups"][0]["datasets"][0]["value"];
    TEST_ASSERT_NOT_NULL(val);
    TEST_ASSERT_EQUAL_STRING("0", val);
}

void test_dashboard_serialize_pretty_has_delimiters(void) {
    ss::Dashboard dash(kTestCfg);
    dash.begin();

    char buf[16384];
    const size_t len = dash.serialize(buf, sizeof(buf), /*pretty=*/true);
    TEST_ASSERT_GREATER_THAN(4, len);

    // Prefix must still be /*
    TEST_ASSERT_EQUAL_CHAR('/', buf[0]);
    TEST_ASSERT_EQUAL_CHAR('*', buf[1]);

    // Suffix must end with \r\n\r\n
    TEST_ASSERT_EQUAL_CHAR('\r', buf[len - 2]);
    TEST_ASSERT_EQUAL_CHAR('\n', buf[len - 1]);

    // In pretty mode a '\n' appears before the closing */
    // i.e. …JSON…\n*/\r\n\r\n  →  buf[len-6] == '\n', buf[len-5] == '*', buf[len-4] == '/'
    TEST_ASSERT_EQUAL_CHAR('\n', buf[len - 6]);
    TEST_ASSERT_EQUAL_CHAR('*',  buf[len - 5]);
    TEST_ASSERT_EQUAL_CHAR('/',  buf[len - 4]);
}

void test_dashboard_serialize_pretty_is_larger(void) {
    ss::Dashboard dash(kTestCfg);
    dash.begin();

    char buf[16384];
    const size_t compactLen = dash.serialize(buf, sizeof(buf), /*pretty=*/false);
    const size_t prettyLen  = dash.serialize(buf, sizeof(buf), /*pretty=*/true);

    // Pretty output must be strictly larger (indentation adds bytes).
    TEST_ASSERT_GREATER_THAN(compactLen, prettyLen);
}

void test_dashboard_serialize_pretty_is_valid_json(void) {
    ss::Dashboard dash(kTestCfg);
    dash.begin();

    char buf[16384];
    const size_t len = dash.serialize(buf, sizeof(buf), /*pretty=*/true);
    TEST_ASSERT_GREATER_THAN(4, len);

    // The content between /* and the closing \n*/ must be valid JSON.
    // Find the closing \n*/ by scanning backwards from the end.
    // buf layout: /*{…pretty JSON…}\n*/\r\n\r\n\0
    // JSON starts at buf+2; ends at buf[len-6] which is '\n' before */
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, buf + 2);
    TEST_ASSERT_TRUE(err == DeserializationError::Ok);
    TEST_ASSERT_NOT_NULL(doc["title"].as<const char*>());
}

// ─── Test runner ─────────────────────────────────────────────────────────────

void run_dashboard_tests() {
    RUN_TEST(test_dashboard_begin_creates_valid_json);
    RUN_TEST(test_dashboard_serialize_has_delimiters);
    RUN_TEST(test_dashboard_serialize_contains_title);
    RUN_TEST(test_dashboard_serialize_contains_group);
    RUN_TEST(test_dashboard_serialize_contains_dataset);
    RUN_TEST(test_dashboard_serialize_contains_action);
    RUN_TEST(test_dashboard_update_patches_numeric_value);
    RUN_TEST(test_dashboard_update_patches_string_value);
    RUN_TEST(test_dashboard_update_preserves_structure);
    RUN_TEST(test_dashboard_serialize_buffer_too_small);
    RUN_TEST(test_dashboard_initial_values_are_zero);
    RUN_TEST(test_dashboard_serialize_pretty_has_delimiters);
    RUN_TEST(test_dashboard_serialize_pretty_is_larger);
    RUN_TEST(test_dashboard_serialize_pretty_is_valid_json);
}
