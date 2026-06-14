idf_component_register(
    SRCS "ota_service.cpp"
    INCLUDE_DIRS "."
    REQUIRES app_update esp_http_client json esp_app_format audio config
)
