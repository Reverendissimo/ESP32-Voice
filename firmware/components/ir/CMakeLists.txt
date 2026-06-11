    idf_component_register(
        SRCS
"ir_learn_service.cpp"
"ir_send_service.cpp"
        INCLUDE_DIRS
"."
        REQUIRES esp_event
    )
