# Egress Control

Adds an **Egress Control** dock to OBS Studio for starting and stopping a
server-side Egress service. OBS acts only as a remote control: it does not join
a room, encode, hold platform tokens, or stream. Every decision is made by the
backend, which stays the source of truth.

## Architecture

| File | Responsibility |
| --- | --- |
| `egress-control-plugin.cpp` | Module declaration, dock registration, frontend event hookup |
| `egress-control-dock.*` | Qt UI and the state machine; no networking |
| `egress-api-client.*` | All backend calls, JSON parsing, error classification |
| `egress-config.*` | Backend URL, paths, timeouts, access-token provider |
| `egress-state.hpp` | `EgressState` enum and its locale keys |

The UI never touches the network and the client never touches widgets, so the
API contract can change without reworking the dock.

### States

`Offline → Starting → Live → Stopping → Offline`, with `Error` reachable from a
failed request.

| State | Start | Stop |
| --- | --- | --- |
| Offline | Enabled | Disabled |
| Starting | Disabled | Enabled (cancels the pending start) |
| Live | Disabled | Enabled |
| Stopping | Disabled | Disabled |
| Error | Enabled | Enabled only if a session may still exist |

## Configuration

No URL or secret is compiled in. On load the plugin reads an optional JSON file
from the module config directory:

```
%APPDATA%\obs-studio\plugin_config\egress-control\egress-control.json
```

```json
{
  "base_url": "https://api.example.com",
  "start_path": "/api/v1/egress/start",
  "stop_path": "/api/v1/egress/stop",
  "status_path": "/api/v1/egress/status",
  "request_timeout_ms": 15000,
  "query_status_on_startup": true,
  "stop_egress_on_exit": false
}
```

Only `base_url` is required; every other key falls back to the value above.
Until it is set, the dock loads and reports "No backend server is configured."

`stop_egress_on_exit` is opt-in. Left at `false`, closing OBS leaves a running
Egress session alone.

### Environment overrides

| Variable | Purpose |
| --- | --- |
| `EGRESS_CONTROL_BASE_URL` | Overrides `base_url` |
| `EGRESS_CONTROL_ACCESS_TOKEN` | Bearer token sent with each request |

`https` is required. Plain `http` is allowed only against `localhost`,
`127.0.0.1`, or `::1`; any other `http` address is refused before a request is
sent, so a misconfiguration cannot leak a token in clear text.

## Replacing the access-token provider

`EnvironmentAccessTokenProvider` is a development placeholder. To integrate real
authentication, implement `AccessTokenProvider`:

```cpp
class MyTokenProvider : public AccessTokenProvider {
public:
	std::string GetAccessToken() const override { return FetchTokenFromMyAuthSystem(); }
};
```

and assign it in `EgressConfig::Load()`:

```cpp
config.tokenProvider = std::make_shared<MyTokenProvider>();
```

`GetAccessToken()` is called per request, so a provider is free to refresh a
short-lived token. Nothing else in the plugin needs to change.

## Backend contract

```
POST {base_url}/api/v1/egress/start   → { "success": true, "serviceId": "...", "egressId": "...", "status": "starting" }
POST {base_url}/api/v1/egress/stop    → { "success": true }
GET  {base_url}/api/v1/egress/status  → { "success": true, "status": "live", "serviceId": "..." }
```

Requests carry `Authorization: Bearer <token>` and `Content-Type:
application/json`. The start body is `{"client":"obs-studio","platform":"windows"}`.

Response handling notes:

- `success` is optional; a 2xx without it counts as success, an explicit `false`
  is honoured.
- `404` on stop is treated as success — stop is idempotent.
- `status` is matched case-insensitively against `live`, `active`, `started`,
  and `running` when deciding whether an existing session is running.

## Logging

Requests, outcomes, and HTTP status codes are logged. Authorization headers,
tokens, and response bodies are never logged — an unparseable response is
reported without echoing its contents.

## Testing

| Scenario | Setup | Expected |
| --- | --- | --- |
| Start | Backend returns 200 | `Starting` → `Live`, Stop enabled |
| Stop | Backend returns 200 | `Stopping` → `Offline`, Start enabled |
| Double click | Click Start twice quickly | One request; the client drops the second |
| Timeout | Backend delays past `request_timeout_ms` | `Error` + "Egress may still be active", Stop stays enabled |
| Server error | Backend returns 500 | `Error` + "The server reported an error." |
| No session | Stop returns 404 | `Offline` + "No active Egress session was found." |
| Malformed JSON | Backend returns non-JSON | `Error` + unexpected-response message; body not logged |
| Restart with session | `status` returns `live`, restart OBS | Dock loads as `Live` |
| Not configured | No `base_url` | Dock loads, reports not configured, no request sent |
| Insecure URL | `base_url` set to remote `http://` | Request refused before sending |

The UI must stay responsive throughout — all requests are asynchronous.

## Merging into a newer OBS release

The feature is confined to `plugins/egress-control/` plus **one** line in
`plugins/CMakeLists.txt`:

```cmake
add_obs_plugin(egress-control)
```

To reapply on a newer OBS checkout, copy the directory and re-add that line.
Nothing in `frontend/`, `libobs/`, or any existing plugin is modified.

Public APIs relied on, all stable frontend API:

- `obs_frontend_add_dock_by_id()` / `obs_frontend_remove_dock()`
- `obs_frontend_add_event_callback()` / `obs_frontend_remove_event_callback()`
- `OBS_FRONTEND_EVENT_FINISHED_LOADING`, `OBS_FRONTEND_EVENT_EXIT`

If a future release removes `obs_frontend_add_dock_by_id()`, switch to
`obs_frontend_add_custom_qdock()` and wrap the widget in a `QDockWidget`; that
call is confined to `obs_module_load()`.
