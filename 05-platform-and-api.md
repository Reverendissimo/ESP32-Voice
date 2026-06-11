# Platform and API Directive

## Platform requirements

Implement these platform features first:

- device identity from hardware MAC/eFuse
- auth token support
- Wi-Fi station mode
- SNTP time sync
- local HTTP server
- health endpoint
- metrics endpoint
- logs endpoint
- firmware/API version endpoint

## Required endpoints

Prefix all routes with `/api/v1`.

### Core control
- `POST /play`
- `POST /display`
- `GET /status`
- `GET /health`
- `GET /metrics`
- `GET /logs/recent`
- `GET /api/version`
- `GET /time`
- `POST /time/sync`

### Config
- `GET /config`
- `GET /config/saved`
- `POST /config/patch`
- `POST /config/apply`
- `POST /config/save`
- `POST /config/load`
- `POST /config/revert`
- `POST /config/reset`
- `POST /config/reset_saved`

### Wi-Fi
- `GET /wifi/status`
- `GET /wifi/scan`
- `POST /wifi/test`

### Sensors and peripherals
- `GET /battery`
- `GET /power`
- `GET /environment`
- `POST /ir/learn/start`
- `POST /ir/send`

### Outbound device events
- `POST /speech`
- `POST /speech/finalize`
- `POST /ui-event`
- `POST /power/event`
- `POST /environment/event`
- `POST /ir/event`
- `POST /device/heartbeat`

## Mandatory request fields

Use these consistently:

```json
{
  "v": 1,
  "device_uid": "espbox-94b97ef13a2c",
  "device_name": "kitchen-box",
  "request_id": "req_123",
  "command_id": "cmd_123",
  "session_id": "sess_123"
}
```

Not every field is required on every endpoint, but `device_uid` and request correlation must be used consistently.

## Error format

Use this everywhere:

```json
{
  "v": 1,
  "error": {
    "code": "INVALID_REQUEST",
    "message": "Missing target_device_uid",
    "retryable": false,
    "request_id": "req_123"
  }
}
```

## Inbound command validation

For all server -> ESP commands:
- require `target_device_uid`
- reject if it does not match local `device_uid`
- return structured error

## Time sync

Use SNTP.

Expose:
- trusted/untrusted time state
- last successful sync
- timezone
- sync interval

## Diagnostics

At minimum include in `/health` or `/metrics`:
- firmware version
- API version
- uptime
- Wi-Fi state
- RSSI
- heap
- current main state
- battery percentage if available
- time trusted flag
- config dirty flag
