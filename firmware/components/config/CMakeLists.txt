    idf_component_register(
        SRCS
"config_defaults.cpp"
"config_validator.cpp"
"config_store.cpp"
"config_manager.cpp"
        INCLUDE_DIRS
"."
        REQUIRES nvs_flash json
    )
