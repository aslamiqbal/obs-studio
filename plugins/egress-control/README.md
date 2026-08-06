# Egress Control

Adds two things to OBS Studio:

1. A **Stream Console** — a simplified operator window with a program preview,
   scene selection, streaming start/stop, stream destination settings, and
   Egress start/stop. It opens automatically at startup.
2. An **Egress Control** dock for starting and stopping a server-side Egress
   service.

OBS acts only as a remote control for Egress: it does not join a room, encode,
hold platform tokens, or stream to the platform itself. Every Egress decision is
made by the backend, which stays the source of truth.

The normal OBS main window is **not modified**. It stays fully functional behind
the console, and every existing dock, menu, and control still works.

## Stream Console

Opens at startup, and can be reopened from **Tools → Stream Console**. Closing
it only hides the window; OBS keeps running.

```
+--------------------------------------------------+
|              program preview (live)              |
+--------------------------------------------------+
| Scene: [LiveKit v] [Start Streaming] [Stream      |
|                     Settings]  Not streaming      |
| Egress Control [Start Service] [Stop Service] ... |
+--------------------------------------------------+
```

The preview renders the composited **program output** — the same feed OBS
streams — using `obs_display_create()` plus `obs_render_main_texture()`, the
same mechanism the built-in preview uses. It is read-only: no selection, no
transform handles.

Streaming uses `obs_frontend_streaming_start()` / `_stop()`, so it behaves
exactly like the button in the Controls dock, and the two stay in sync because
both react to the same frontend events.

### Stream Settings

A compact editor for the custom RTMP destination (server URL + stream key). It
deliberately covers only that case:

- If a **preset service** (Twitch, YouTube, …) is configured, the fields become
  read-only and the dialog points at the main OBS settings. It will not silently
  replace an OAuth-backed service.
- If **streaming is active**, saving is refused — the running output would
  ignore the change anyway.

The stream key is masked on screen and never written to the log.

## Fleet reporting (admin dashboard)

Once signed in, the client registers itself and then heartbeats, which is what
populates the `/admin/nvs` dashboard.

```
sign in  ->  POST /api/nvs/clients/register      (device_id, label, version, platform)
         ->  POST /api/nvs/clients/{id}/heartbeat   every 15s
                 up:   egress_state, is_streaming, status, acks for prior commands
                 down: is_blocked, interval, commands to run now
```

**Commands arrive in the heartbeat response** rather than being pushed. The
desktop sits behind arbitrary NAT, so there is no inbound path to it; the
heartbeat already exists and doubles as the command channel. The cost is up to
one interval of latency.

Supported: `start_egress`, `stop_egress`, `start_stream`, `stop_stream`,
`show_console`, `sign_out`, `refresh`.

A command is executed, then acknowledged on the **following** heartbeat, so the
dashboard shows what actually happened rather than what was dispatched. The one
exception is `sign_out`, which flushes its acknowledgement first — signing out
stops the heartbeat that would otherwise carry it.

Other behaviours worth knowing:

- **Device identity** is a UUID minted on first run and kept in
  `nvs-device.json` in the module config directory. It is opaque and carries
  nothing about the user or the hardware, and it keeps one machine as one row in
  the dashboard across restarts.
- **Blocked clients** keep reporting but refuse every control command except
  `refresh`, so an admin can disable a machine without losing visibility of it.
- **A rejected heartbeat** (400/404) clears the client id and re-registers,
  rather than heartbeating into a void after the server forgets the client.
- **Signing out stops reporting** entirely: the fleet is keyed on the account.
- The wire value for state comes from `EgressStateName()`, deliberately separate
  from the locale key, so translating the UI can never change the protocol.

## Notification area and hide mode

NVS does **not** create its own tray icon. It finds the one the frontend already
owns and appends a section above `Exit`, so there is a single icon and every
existing entry (Show/Hide, streaming, recording, projectors) is untouched:

```
Show
Open Preview Projector    >
Open Program Projector    >
Start Streaming
Start Recording
Start Replay Buffer
Start Virtual Camera
------------------------------
Stream Console                 <- NVS
Sign In / Sign Out (name)      <- NVS
Start Service (Offline)        <- NVS, state shown inline
Stop Service                   <- NVS, enabled only when stoppable
------------------------------
Exit
```

The entries are removed again on module unload, since they live in a menu owned
by the main window and would otherwise outlive the plugin.

### Hide mode

"Start with Windows" writes this to `HKCU\...\CurrentVersion\Run`:

```
"C:\path\to\nvs64.exe" --minimize-to-tray
```

`--minimize-to-tray` is a stock OBS option: the frontend keeps the main window
hidden at startup whenever the tray is available (`SysTrayEnabled`, on by
default). NVS additionally suppresses the Stream Console auto-open when that
flag is present — otherwise a "hidden" launch would still pop a window. The
console stays one click away in the tray menu.

Nothing is written to the registry unless the user ticks the checkbox, and the
checkbox reflects the *current* executable, so a stale entry from another build
reads as disabled rather than silently claiming to be on.

## Architecture

| File | Responsibility |
| --- | --- |
| `egress-control-plugin.cpp` | Module declaration, dock + console registration, frontend events |
| `stream-console-window.*` | Simplified operator window |
| `program-preview-widget.*` | OBS display embedded in a plugin-owned window |
| `stream-settings-dialog.*` | Custom RTMP destination editor |
| `egress-controller.*` | Egress state machine, shared by every view |
| `egress-control-dock.*` | Dock view onto the controller |
| `egress-api-client.*` | All backend calls, JSON parsing, error classification |
| `egress-config.*` | Backend URL, paths, timeouts, access-token provider |
| `egress-state.hpp` | `EgressState` enum and its locale keys |
| `nvs-identity.*` | Loopback + PKCE sign-in, token refresh |
| `nvs-credential-store.*` | DPAPI-sealed credential storage |
| `nvs-startup.*` | Windows startup entry (writes the hide-mode flag) |
| `nvs-tray.*` | Notification-area entries, hide-mode detection |
| `nvs-fleet-client.*` | Registration, heartbeat, remote command execution |

The UI never touches the network and the client never touches widgets, so the
API contract can change without reworking the UI.

`EgressController` is the single owner of Egress state. The dock and the console
are both views onto it, so there is exactly one API client and one state machine
no matter how many widgets show Egress controls — they cannot drift apart.

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

Public APIs relied on, all stable frontend or libobs API:

- `obs_frontend_add_dock_by_id()` / `obs_frontend_remove_dock()`
- `obs_frontend_add_tools_menu_item()`
- `obs_frontend_add_event_callback()` / `obs_frontend_remove_event_callback()`
- `obs_frontend_get_main_window()`
- `obs_frontend_streaming_start()` / `_stop()` / `_active()`
- `obs_frontend_get_scenes()` / `obs_frontend_set_current_scene()` / `obs_frontend_get_current_scene()`
- `obs_frontend_get_streaming_service()` / `obs_frontend_set_streaming_service()` / `obs_frontend_save_streaming_service()`
- `obs_display_create()` / `obs_display_resize()` / `obs_display_destroy()`, `obs_render_main_texture()`
- `OBS_FRONTEND_EVENT_FINISHED_LOADING`, `OBS_FRONTEND_EVENT_EXIT`, the streaming
  events, and the scene list/change events

No private frontend class is referenced. `ProgramPreviewWidget` reimplements the
minimum of what `OBSQTDisplay` does rather than forking it, because that class
lives in `frontend/` and is not available to plugins.

**Platform note:** the preview attaches to a native window handle. Windows and
macOS are handled; on Linux, X11 works and Wayland does not, because the surface
plumbing the frontend uses for Wayland is not public API. On Wayland the console
still works and only the preview area stays blank.

If a future release removes `obs_frontend_add_dock_by_id()`, switch to
`obs_frontend_add_custom_qdock()` and wrap the widget in a `QDockWidget`; that
call is confined to `obs_module_load()`.
