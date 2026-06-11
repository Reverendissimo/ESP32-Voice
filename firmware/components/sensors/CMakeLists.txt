    idf_component_register(
        SRCS
"radar_service.cpp"
"environment_service.cpp"
"battery_service.cpp"
"alarm_evaluator.cpp"
        INCLUDE_DIRS
"."
        REQUIRES esp_event
    )
