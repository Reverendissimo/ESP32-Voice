idf_component_register(
    SRCS
        "display_service.cpp"
        "screen_model.cpp"
        "screen_parser.cpp"
        "lvgl_renderer.cpp"
        "ui_event_client.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        json esp-box-3 esp_http_client config freertos
)
