#!/usr/bin/env python3
"""Generate ESP-IDF firmware directory tree with documented stub classes."""

from __future__ import annotations

from pathlib import Path
from textwrap import dedent

ROOT = Path(__file__).resolve().parents[1] / "firmware"

# (component, [(filename_stem, class_name, brief, responsibilities, non_responsibilities, methods)])
COMPONENTS: dict[str, list[tuple]] = {
    "app_core": [
        (
            "app_bootstrap",
            "AppBootstrap",
            "Owns firmware startup and subsystem wiring order.",
            ["initialize NVS and core services", "start configured subsystems"],
            ["HTTP route business logic", "hardware driver details"],
            [("bool", "start", "Starts platform services in dependency order.", [])],
        ),
        (
            "device_identity",
            "DeviceIdentity",
            "Derives immutable device identity from hardware.",
            ["produce stable device_uid", "validate inbound target_device_uid"],
            ["config storage", "HTTP handling"],
            [
                ("bool", "initialize", "Loads hardware identity.", []),
                ("const char*", "deviceUid", "Returns immutable device UID.", []),
                (
                    "bool",
                    "matchesTarget",
                    "Returns true when target matches local UID.",
                    [("const char*", "targetDeviceUid")],
                ),
            ],
        ),
    ],
    "config": [
        (
            "config_models",
            "ConfigModels",
            "Defines typed configuration schema structures.",
            ["central config field definitions"],
            ["validation logic", "NVS persistence"],
            [],
            True,
        ),
        (
            "config_validator",
            "ConfigValidator",
            "Validates config patches and full config objects.",
            ["reject invalid ranges and missing required fields"],
            ["NVS I/O", "REST parsing"],
            [("bool", "validatePatch", "Validates a partial config patch.", [])],
        ),
        (
            "config_store",
            "ConfigStore",
            "Persists saved configuration in NVS.",
            ["load and save config blobs"],
            ["RAM active config management", "validation rules"],
            [
                ("bool", "loadSaved", "Loads saved config from NVS.", []),
                ("bool", "saveActive", "Persists active config to NVS.", []),
            ],
        ),
        (
            "config_manager",
            "ConfigManager",
            "Manages defaults, saved, and active config layers.",
            ["apply patches to active RAM config", "revert and explicit save"],
            ["HTTP request parsing", "Wi-Fi driver control"],
            [("bool", "applyPatch", "Applies patch to active config.", [])],
        ),
    ],
    "network": [
        (
            "wifi_manager",
            "WifiManager",
            "Owns Wi-Fi station connection lifecycle.",
            ["connect, disconnect, expose Wi-Fi state"],
            ["config persistence", "HTTP API handling"],
            [("bool", "start", "Applies Wi-Fi config and connects.", [])],
        ),
        (
            "time_sync_service",
            "TimeSyncService",
            "Owns SNTP time synchronization state.",
            ["sync system time", "expose trusted/untrusted state"],
            ["HTTP route parsing", "config storage"],
            [("bool", "start", "Starts SNTP synchronization.", [])],
        ),
        (
            "auth_context",
            "AuthContext",
            "Validates auth tokens for protected endpoints.",
            ["token presence and validity checks"],
            ["user account management", "secret logging"],
            [("bool", "authorize", "Returns true for valid admin token.", [])],
        ),
        (
            "retry_policy",
            "RetryPolicy",
            "Computes bounded exponential retry delays.",
            ["backoff sequence and cap handling"],
            ["network transport", "HTTP handlers"],
            [("uint32_t", "nextDelayMs", "Returns next retry delay in ms.", [])],
        ),
    ],
    "api": [
        (
            "http_server_service",
            "HttpServerService",
            "Owns local HTTP server lifecycle.",
            ["start/stop esp_http_server", "delegate routes to registry"],
            ["per-route business logic"],
            [("bool", "start", "Starts HTTP server on configured port.", [])],
        ),
        (
            "route_registry",
            "RouteRegistry",
            "Registers REST route handlers with the HTTP server.",
            ["map paths to thin handlers"],
            ["hardware access", "config validation internals"],
            [("bool", "registerAll", "Registers all /api/v1 routes.", [])],
        ),
        (
            "json_response_builder",
            "JsonResponseBuilder",
            "Builds consistent JSON success responses.",
            ["include version and identity fields"],
            ["business rule evaluation"],
            [("bool", "buildHealth", "Builds health JSON payload.", [])],
        ),
        (
            "error_response_factory",
            "ErrorResponseFactory",
            "Builds structured API error responses.",
            ["stable error schema", "request_id propagation"],
            ["route-specific validation"],
            [("bool", "buildInvalidRequest", "Builds INVALID_REQUEST payload.", [])],
        ),
    ],
    "audio": [
        (
            "audio_capture_service",
            "AudioCaptureService",
            "Captures microphone audio frames.",
            ["start/stop capture pipeline"],
            ["VAD decisions", "HTTP upload"],
            [("bool", "start", "Starts audio capture.", [])],
        ),
        (
            "vad_service",
            "VadService",
            "Detects speech activity in captured audio.",
            ["speech start/stop events"],
            ["upload transport", "playback"],
            [("bool", "start", "Starts VAD processing.", [])],
        ),
        (
            "audio_upload_service",
            "AudioUploadService",
            "Streams utterance audio to backend server.",
            ["chunked upload during speech"],
            ["VAD detection", "playback scheduling"],
            [("bool", "startUtterance", "Begins streaming current utterance.", [])],
        ),
        (
            "audio_playback_service",
            "AudioPlaybackService",
            "Plays async audio commanded by server.",
            ["queue and play remote audio payloads"],
            ["speech capture", "display rendering"],
            [("bool", "play", "Queues playback payload.", [])],
        ),
        (
            "utterance_state_machine",
            "UtteranceStateMachine",
            "Tracks utterance lifecycle from VAD events.",
            ["conservative finalize after silence"],
            ["HTTP client details", "microphone driver"],
            [("bool", "onSpeechStart", "Handles speech start event.", [])],
        ),
    ],
    "display": [
        (
            "display_service",
            "DisplayService",
            "Applies declarative display commands to screen.",
            ["full-screen replacement updates"],
            ["REST parsing", "LVGL draw primitives"],
            [("bool", "showScreen", "Renders a parsed screen model.", [])],
        ),
        (
            "screen_model",
            "ScreenModel",
            "Defines supported declarative screen schema types.",
            ["screen and component model definitions"],
            ["JSON parsing", "rendering"],
            [],
            True,
        ),
        (
            "screen_parser",
            "ScreenParser",
            "Parses display JSON into screen models.",
            ["schema validation and rejection"],
            ["LVGL rendering", "HTTP transport"],
            [("bool", "parse", "Parses JSON into ScreenModel.", [])],
        ),
        (
            "lvgl_renderer",
            "LvglRenderer",
            "Renders screen models with LVGL.",
            ["draw supported component kinds"],
            ["JSON parsing", "REST handlers"],
            [("bool", "render", "Renders screen to display.", [])],
        ),
    ],
    "sensors": [
        (
            "radar_service",
            "RadarService",
            "Reads radar-based presence when enabled.",
            ["presence state updates"],
            ["alarm evaluation", "HTTP handlers"],
            [("bool", "start", "Starts radar sampling.", [])],
        ),
        (
            "environment_service",
            "EnvironmentService",
            "Reads temperature and humidity sensors.",
            ["sample and expose environment values"],
            ["alarm threshold logic", "REST parsing"],
            [("bool", "start", "Starts environment sampling.", [])],
        ),
        (
            "battery_service",
            "BatteryService",
            "Reads battery voltage, percent, and charging state.",
            ["battery telemetry"],
            ["alarm evaluation", "HTTP handlers"],
            [("bool", "start", "Starts battery monitoring.", [])],
        ),
        (
            "alarm_evaluator",
            "AlarmEvaluator",
            "Evaluates threshold crossings with hysteresis.",
            ["trigger/clear thresholds", "duration and cooldown gates"],
            ["sensor driver access", "REST parsing"],
            [("bool", "evaluate", "Updates alarm state from sample.", [])],
        ),
    ],
    "ir": [
        (
            "ir_code_models",
            "IrCodeModels",
            "Defines learned and raw IR code structures.",
            ["IR payload model definitions"],
            ["learning workflow", "send driver"],
            [],
            True,
        ),
        (
            "ir_learn_service",
            "IrLearnService",
            "Runs modal IR learning with timeout.",
            ["learn start, success, timeout, cancel"],
            ["HTTP route parsing", "send path"],
            [("bool", "startLearn", "Enters IR learning mode.", [])],
        ),
        (
            "ir_send_service",
            "IrSendService",
            "Transmits learned or raw IR codes.",
            ["validate target and emit IR"],
            ["learning workflow", "REST parsing"],
            [("bool", "send", "Transmits IR code payload.", [])],
        ),
    ],
    "cli": [
        (
            "serial_cli_service",
            "SerialCliService",
            "Owns USB serial CLI session lifecycle.",
            ["read commands and print human output"],
            ["config validation internals", "HTTP server"],
            [("bool", "start", "Starts serial CLI loop.", [])],
        ),
        (
            "cli_command_registry",
            "CliCommandRegistry",
            "Registers and dispatches CLI commands.",
            ["help, status, config, wifi commands"],
            ["NVS driver access", "Wi-Fi driver internals"],
            [("bool", "registerDefaults", "Registers required CLI commands.", [])],
        ),
    ],
    "diagnostics": [
        (
            "health_service",
            "HealthService",
            "Builds health and status snapshots.",
            ["uptime, heap, wifi, dirty flag, time trust"],
            ["HTTP server lifecycle", "sensor drivers"],
            [("bool", "collect", "Collects current health snapshot.", [])],
        ),
        (
            "metrics_service",
            "MetricsService",
            "Exposes runtime counters and gauges.",
            ["metrics aggregation for /metrics"],
            ["health summary composition", "REST routing"],
            [("bool", "collect", "Collects metrics snapshot.", [])],
        ),
        (
            "recent_log_buffer",
            "RecentLogBuffer",
            "Retains a bounded ring of recent log lines.",
            ["append and retrieve recent logs"],
            ["ESP-IDF log hook installation details in routes"],
            [("bool", "append", "Appends one log line.", [])],
        ),
    ],
}

API_ROUTES = [
    ("play_route", "PlayRoute", "Handles POST /api/v1/play."),
    ("display_route", "DisplayRoute", "Handles POST /api/v1/display."),
    ("config_route", "ConfigRoute", "Handles /api/v1/config routes."),
    ("health_route", "HealthRoute", "Handles GET /api/v1/health."),
    ("battery_route", "BatteryRoute", "Handles GET /api/v1/battery."),
    ("environment_route", "EnvironmentRoute", "Handles GET /api/v1/environment."),
    ("ir_route", "IrRoute", "Handles /api/v1/ir routes."),
    ("time_route", "TimeRoute", "Handles /api/v1/time routes."),
]


def snake_to_pascal(name: str) -> str:
    return "".join(part.capitalize() for part in name.split("_"))


def write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def render_header(
    stem: str,
    class_name: str,
    brief: str,
    responsibilities: list[str],
    non_responsibilities: list[str],
    methods: list,
    header_only: bool = False,
) -> str:
    resp = "\n".join(f" * - {item}" for item in responsibilities) or " * - (stub)"
    non_resp = "\n".join(f" * - {item}" for item in non_responsibilities) or " * - (none)"
    method_docs = []
    decls = []
    for method in methods:
        if isinstance(method, tuple) and len(method) == 4:
            ret, name, doc, params = method
        else:
            continue
        param_str = ", ".join(f"{ptype} {pname}" for ptype, pname in params)
        param_doc = "\n".join(f" * @param {pname} {ptype}." for ptype, pname in params)
        method_docs.append(
            f"    /**\n     * @brief {doc}\n{param_doc + chr(10) if param_doc else ''}     */"
        )
        decls.append(f"    {ret} {name}({param_str});")

    body = "\n".join(method_docs + decls)
    pragma = "#pragma once\n\n"
    includes = '#include <stdint.h>\n#include <stdbool.h>\n\n' if methods else ""
    if header_only and not methods:
        includes = "#include <stdint.h>\n\n"

    return dedent(
        f"""\
        /**
         * @file {stem}.hpp
         * @brief {brief}
         *
         * Responsibilities:
{resp}
         *
         * Non-responsibilities:
{non_resp}
         */
        {pragma}{includes}/**
         * @brief {brief}
         */
        class {class_name} {{
        public:
        {body if body else "    // Stub model definitions will be added during implementation."}
        }};
        """
    )


def render_source(stem: str, class_name: str, methods: list) -> str:
    impls = []
    for method in methods:
        if isinstance(method, tuple) and len(method) == 4:
            ret, name, _doc, params = method
        else:
            continue
        param_str = ", ".join(f"{ptype} {pname}" for ptype, pname in params)
        if ret == "bool":
            body = "    return false;"
        elif ret == "uint32_t":
            body = "    return 0;"
        elif ret == "const char*":
            body = '    return "";'
        else:
            body = "    return false;"
        impls.append(
            dedent(
                f"""
                {ret} {class_name}::{name}({param_str}) {{
                {body}
                }}
                """
            ).strip()
        )

    impl_block = "\n\n".join(impls) if impls else f"// Stub implementation for {class_name}."
    return dedent(
        f"""\
        /**
         * @file {stem}.cpp
         * @brief Implementation of {class_name}.
         */
        #include "{stem}.hpp"

        {impl_block}
        """
    )


def component_cmake(
    component: str,
    sources: list[str],
    requires: list[str],
    include_dirs: list[str] | None = None,
) -> str:
    src_list = "\n".join(f'    "{s}"' for s in sources)
    req = " ".join(requires)
    dirs = include_dirs or ["."]
    dir_list = "\n".join(f'    "{d}"' for d in dirs)
    return dedent(
        f"""\
        idf_component_register(
            SRCS
{src_list}
            INCLUDE_DIRS
{dir_list}
            REQUIRES {req}
        )
        """
    )


def main() -> None:
    ROOT.mkdir(parents=True, exist_ok=True)

    component_requires = {
        "app_core": ["nvs_flash", "esp_event"],
        "config": ["nvs_flash", "json"],
        "network": ["esp_wifi", "esp_netif", "esp_event"],
        "api": ["esp_http_server", "json"],
        "audio": ["esp_event"],
        "display": ["json"],
        "sensors": ["esp_event"],
        "ir": ["esp_event"],
        "cli": ["console"],
        "diagnostics": ["esp_timer"],
    }

    for component, entries in COMPONENTS.items():
        sources: list[str] = []
        comp_dir = ROOT / "components" / component
        for entry in entries:
            if len(entry) == 6:
                stem, class_name, brief, resp, non_resp, methods = entry
                header_only = False
            else:
                stem, class_name, brief, resp, non_resp, methods, *rest = entry
                header_only = bool(rest and rest[0])

            header = render_header(stem, class_name, brief, resp, non_resp, methods, header_only)
            write(comp_dir / f"{stem}.hpp", header)
            if not header_only:
                write(comp_dir / f"{stem}.cpp", render_source(stem, class_name, methods))
                sources.append(f"{stem}.cpp")

        if component == "api":
            routes_dir = comp_dir / "routes"
            for stem, class_name, brief in API_ROUTES:
                methods = [("bool", "registerRoute", f"Registers {brief}", [])]
                write(
                    routes_dir / f"{stem}.hpp",
                    render_header(
                        stem,
                        class_name,
                        brief,
                        [brief],
                        ["business logic in services"],
                        methods,
                    ),
                )
                write(
                    routes_dir / f"{stem}.cpp",
                    render_source(stem, class_name, methods),
                )
                sources.append(f"routes/{stem}.cpp")

        include_dirs = [".", "routes"] if component == "api" else ["."]
        write(
            comp_dir / "CMakeLists.txt",
            component_cmake(component, sources, component_requires[component], include_dirs),
        )
        (comp_dir / "test").mkdir(exist_ok=True)
        (comp_dir / "host_test").mkdir(exist_ok=True)

    write(
        ROOT / "CMakeLists.txt",
        dedent(
            """\
            cmake_minimum_required(VERSION 3.16)

            include($ENV{IDF_PATH}/tools/cmake/project.cmake)
            project(esp32-voice)
            """
        ),
    )

    write(
        ROOT / "main" / "CMakeLists.txt",
        dedent(
            """\
            idf_component_register(
                SRCS "app_main.cpp"
                INCLUDE_DIRS "."
                REQUIRES app_core
            )
            """
        ),
    )

    write(
        ROOT / "main" / "app_main.cpp",
        dedent(
            """\
            /**
             * @file app_main.cpp
             * @brief Firmware entry point for ESP32-S3-BOX-3 voice terminal.
             */
            #include "app_bootstrap.hpp"
            #include "esp_log.h"

            static const char* kTag = "app_main";

            extern "C" void app_main(void) {
                ESP_LOGI(kTag, "ESP32-Voice firmware starting");
                AppBootstrap bootstrap;
                if (!bootstrap.start()) {
                    ESP_LOGE(kTag, "Bootstrap failed");
                }
            }
            """
        ),
    )

    write(
        ROOT / "sdkconfig.defaults",
        dedent(
            """\
            CONFIG_IDF_TARGET="esp32s3"
            CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
            CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y
            CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
            """
        ),
    )

    print(f"Scaffolded firmware tree under {ROOT}")


if __name__ == "__main__":
    main()
