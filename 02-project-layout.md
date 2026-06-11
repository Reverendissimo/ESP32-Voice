# Required Project Layout

## Objective

Keep the project small, modular, and easy for another agent or human to navigate.

## Required structure

```text
firmware/
  main/
    app_main.cpp

  components/
    app_core/
      app_bootstrap.hpp
      app_bootstrap.cpp
      device_identity.hpp
      device_identity.cpp

    config/
      config_models.hpp
      config_validator.hpp
      config_validator.cpp
      config_store.hpp
      config_store.cpp
      config_manager.hpp
      config_manager.cpp

    network/
      wifi_manager.hpp
      wifi_manager.cpp
      time_sync_service.hpp
      time_sync_service.cpp
      auth_context.hpp
      auth_context.cpp
      retry_policy.hpp
      retry_policy.cpp

    api/
      http_server_service.hpp
      http_server_service.cpp
      route_registry.hpp
      route_registry.cpp
      json_response_builder.hpp
      json_response_builder.cpp
      error_response_factory.hpp
      error_response_factory.cpp

      routes/
        play_route.hpp
        play_route.cpp
        display_route.hpp
        display_route.cpp
        config_route.hpp
        config_route.cpp
        health_route.hpp
        health_route.cpp
        battery_route.hpp
        battery_route.cpp
        environment_route.hpp
        environment_route.cpp
        ir_route.hpp
        ir_route.cpp
        time_route.hpp
        time_route.cpp

    audio/
      audio_capture_service.hpp
      audio_capture_service.cpp
      vad_service.hpp
      vad_service.cpp
      audio_upload_service.hpp
      audio_upload_service.cpp
      audio_playback_service.hpp
      audio_playback_service.cpp
      utterance_state_machine.hpp
      utterance_state_machine.cpp

    display/
      display_service.hpp
      display_service.cpp
      screen_model.hpp
      screen_parser.hpp
      screen_parser.cpp
      lvgl_renderer.hpp
      lvgl_renderer.cpp

    sensors/
      radar_service.hpp
      radar_service.cpp
      environment_service.hpp
      environment_service.cpp
      battery_service.hpp
      battery_service.cpp
      alarm_evaluator.hpp
      alarm_evaluator.cpp

    ir/
      ir_learn_service.hpp
      ir_learn_service.cpp
      ir_send_service.hpp
      ir_send_service.cpp
      ir_code_models.hpp

    cli/
      serial_cli_service.hpp
      serial_cli_service.cpp
      cli_command_registry.hpp
      cli_command_registry.cpp

    diagnostics/
      health_service.hpp
      health_service.cpp
      metrics_service.hpp
      metrics_service.cpp
      recent_log_buffer.hpp
      recent_log_buffer.cpp
```

## Layout rules

- If a file grows too much, split by responsibility.
- Route handlers must stay thin.
- Hardware-facing services must not know HTTP details.
- Config persistence must not know CLI parsing details.
- Display rendering must not know REST request parsing details.
