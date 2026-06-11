    idf_component_register(
        SRCS
"audio_capture_service.cpp"
"vad_service.cpp"
"audio_upload_service.cpp"
"audio_playback_service.cpp"
"utterance_state_machine.cpp"
        INCLUDE_DIRS
"."
        REQUIRES esp_event
    )
