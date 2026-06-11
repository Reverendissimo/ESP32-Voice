    idf_component_register(
        SRCS
"health_service.cpp"
"metrics_service.cpp"
"recent_log_buffer.cpp"
        INCLUDE_DIRS
"."
        REQUIRES esp_timer
    )
