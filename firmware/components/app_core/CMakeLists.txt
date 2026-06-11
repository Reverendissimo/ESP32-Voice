idf_component_register(
    SRCS
        "app_bootstrap.cpp"
        "device_identity.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        nvs_flash esp_event config network diagnostics
)
