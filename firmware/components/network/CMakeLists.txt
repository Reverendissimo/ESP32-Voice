idf_component_register(
    SRCS
        "wifi_manager.cpp"
        "time_sync_service.cpp"
        "auth_context.cpp"
        "retry_policy.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_wifi esp_netif esp_event config
)
