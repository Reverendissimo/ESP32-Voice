idf_component_register(
    SRCS
        "serial_cli_service.cpp"
        "cli_command_registry.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        audio display console config network diagnostics json esp_app_format
)
