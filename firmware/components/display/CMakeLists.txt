    idf_component_register(
        SRCS
"display_service.cpp"
"screen_parser.cpp"
"lvgl_renderer.cpp"
        INCLUDE_DIRS
"."
        REQUIRES json
    )
