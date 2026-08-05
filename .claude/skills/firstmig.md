Copy and give this prompt to the coding AI agent:



\---



\# AI Coding Agent Prompt: Add Egress Start/Stop Control to OBS Studio



You are an expert C++/Qt developer with strong knowledge of the OBS Studio source code, OBS Frontend API, CMake, Windows 11 builds, networking, and maintainable plugin architecture.



\## Project objective



Add one isolated feature to the latest official OBS Studio source code:



> Allow the user to start and stop a server-side Egress service from a new OBS GUI panel.



The Egress process runs on my backend server. OBS does not need to join the room, encode the room, manage YouTube/Facebook tokens, or broadcast directly.



OBS will only send Start and Stop commands to my backend.



\## Critical requirements



1\. Do not delete, replace, rename, or redesign any existing OBS GUI.

2\. Keep every existing OBS feature working exactly as it currently works.

3\. Do not modify the existing Browser Source.

4\. Do not modify OBS streaming, recording, encoder, output, scene, source, or authentication systems.

5\. Do not place this feature deeply inside OBS core code.

6\. Implement it as an isolated OBS frontend plugin whenever technically possible.

7\. Make future merges with the latest official OBS GitHub repository as effortless as possible.

8\. Avoid modifying existing OBS files unless absolutely necessary.

9\. Prefer adding new files and a separate CMake target.

10\. Clearly document every modification made outside the new plugin directory.

11\. Do not implement unrelated features.

12\. Do not remove or refactor existing code.



\## System behavior



My backend already handles:



\* Creating or retrieving a room

\* Silently joining the room

\* Generating room access tokens

\* Retrieving YouTube/Facebook authorization

\* Starting Egress

\* Sending the Egress output to YouTube/Facebook

\* Stopping Egress

\* Managing Egress status and failures



OBS only acts as a desktop controller.



```text

OBS Egress Control GUI

&#x20;       |

&#x20;       | HTTPS request

&#x20;       v

My Backend API

&#x20;       |

&#x20;       | Start or stop Egress

&#x20;       v

Server-side room and Egress

&#x20;       |

&#x20;       +--> YouTube

&#x20;       |

&#x20;       +--> Facebook

```



\## Required GUI



Create a new dockable OBS panel named:



```text

Egress Control

```



Use the official OBS Frontend API to register the dock.



The panel must not replace or modify existing OBS docks.



The minimum GUI should contain:



```text

Egress Control



Status: Offline



\[ Start Service ]

\[ Stop Service ]



Optional status message:

Ready / Starting / Live / Stopping / Error

```



\### Button behavior



\#### Start Service



When clicked:



1\. Disable the Start button immediately.

2\. Change status to `Starting`.

3\. Send an authenticated HTTPS request to my backend Start endpoint.

4\. Do not create a Browser Source.

5\. Do not join the room locally.

6\. Do not retrieve YouTube or Facebook tokens.

7\. Do not start normal OBS streaming.

8\. When the backend confirms that Egress has started, show `Live`.

9\. Enable the Stop button.

10\. Store the returned service ID or Egress session ID in memory.



\#### Stop Service



When clicked:



1\. Disable the Stop button immediately.

2\. Change status to `Stopping`.

3\. Send an authenticated HTTPS request to my backend Stop endpoint.

4\. When successful, show `Offline`.

5\. Enable the Start button.

6\. Clear the active local service-session information.



\## Service state model



Use these states:



```text

Offline

Starting

Live

Stopping

Error

```



The GUI behavior must follow this state table:



| State    | Start button |                                        Stop button |

| -------- | -----------: | -------------------------------------------------: |

| Offline  |      Enabled |                                           Disabled |

| Starting |     Disabled |                                            Enabled |

| Live     |     Disabled |                                            Enabled |

| Stopping |     Disabled |                                           Disabled |

| Error    |      Enabled | Disabled, unless an active service may still exist |



The Stop button may remain enabled during `Starting` so the user can cancel an in-progress operation.



\## Backend API abstraction



Do not hardcode the real production URL throughout the source code.



Use a centralized configuration structure.



Example placeholders:



```text

POST {BACKEND\_BASE\_URL}/api/v1/egress/start

POST {BACKEND\_BASE\_URL}/api/v1/egress/stop

GET  {BACKEND\_BASE\_URL}/api/v1/egress/status

```



Example Start request:



```http

POST /api/v1/egress/start

Authorization: Bearer APP\_ACCESS\_TOKEN

Content-Type: application/json

```



```json

{

&#x20; "client": "obs-studio",

&#x20; "platform": "windows"

}

```



Example response:



```json

{

&#x20; "success": true,

&#x20; "serviceId": "svc\_12345",

&#x20; "egressId": "egress\_12345",

&#x20; "status": "starting"

}

```



Example Stop request:



```http

POST /api/v1/egress/stop

Authorization: Bearer APP\_ACCESS\_TOKEN

Content-Type: application/json

```



```json

{

&#x20; "serviceId": "svc\_12345"

}

```



The exact API paths, JSON fields, and authentication provider may change later. Therefore, keep API communication isolated behind an interface.



\## Required internal architecture



Use an additive structure similar to:



```text

plugins/

└── egress-control/

&#x20;   ├── CMakeLists.txt

&#x20;   ├── egress-control-plugin.cpp

&#x20;   ├── egress-control-dock.hpp

&#x20;   ├── egress-control-dock.cpp

&#x20;   ├── egress-api-client.hpp

&#x20;   ├── egress-api-client.cpp

&#x20;   ├── egress-config.hpp

&#x20;   ├── egress-state.hpp

&#x20;   ├── locale/

&#x20;   │   └── en-US.ini

&#x20;   └── README.md

```



Use names that match current OBS coding conventions if the latest repository uses a different preferred structure.



\### Responsibilities



\#### `egress-control-plugin`



\* Declare the OBS module

\* Load translations

\* Create the dock

\* Register the dock through the OBS Frontend API

\* Cleanly unregister and destroy resources during module unload



\#### `EgressControlDock`



\* Own the Qt GUI

\* Display status

\* Handle Start and Stop button clicks

\* Update button states

\* Display backend errors

\* Never block the OBS UI thread



\#### `EgressApiClient`



\* Perform Start, Stop, and optional Status requests

\* Handle HTTP status codes

\* Parse JSON safely

\* Apply request timeouts

\* Return structured success/error results

\* Keep networking implementation separate from the GUI



\#### `EgressConfig`



\* Backend base URL

\* API paths

\* Timeout values

\* Optional authorization token provider

\* No production secret hardcoded into source files



\#### `EgressState`



\* Offline

\* Starting

\* Live

\* Stopping

\* Error



\## Networking requirements



1\. Network requests must be asynchronous.

2\. Never block the OBS main/UI thread.

3\. Use the networking mechanism already preferred or available in the current OBS/Qt build.

4\. Apply a reasonable timeout.

5\. Handle malformed JSON.

6\. Handle HTTP errors.

7\. Handle network disconnection.

8\. Handle duplicate button clicks.

9\. Prevent duplicate Start requests.

10\. Treat Stop as idempotent where possible.

11\. Do not log access tokens or sensitive response data.

12\. Use HTTPS only for production configuration.



\## Authentication boundary



Do not implement Google login, YouTube OAuth, or Facebook OAuth in this task.



Create a clean interface where an application access token can later be supplied:



```cpp

class AccessTokenProvider {

public:

&#x20;   virtual \~AccessTokenProvider() = default;

&#x20;   virtual std::string GetAccessToken() const = 0;

};

```



An equivalent design matching the current OBS/Qt coding style is acceptable.



For development, a token may be loaded from a local configuration or environment variable, but:



\* Never commit a real token

\* Never print the complete token in logs

\* Keep token retrieval replaceable



\## Startup and recovery



When the plugin loads:



1\. Show the dock as Offline initially.

2\. Optionally call the backend status endpoint.

3\. If an active service already exists for the authenticated user, show `Live`.

4\. Store the active service ID returned by the backend.

5\. Do not automatically start Egress without explicit user action.



When OBS closes while Egress is active:



\* Do not automatically stop Egress unless this behavior is controlled by a clearly named configuration option.

\* The server remains the source of truth.

\* Clean up local networking and Qt objects safely.



\## Merge-friendly implementation



This is extremely important.



I will later download new versions of OBS Studio from the official GitHub repository and merge this feature into them.



Therefore:



1\. Keep the feature inside one independent plugin directory.

2\. Avoid editing `frontend/`, `libobs/`, `libobs-d3d11/`, `plugins/obs-browser/`, or other existing modules.

3\. If the root plugin CMake list must be changed, make only the smallest possible additive change.

4\. Do not reorder or reformat unrelated code.

5\. Do not apply repository-wide formatting changes.

6\. Do not copy or fork large existing OBS classes.

7\. Use stable public OBS Frontend APIs.

8\. Avoid relying on private internal frontend classes.

9\. Include a patch or commit separation that allows the feature to be reapplied easily.

10\. Place all configuration and API details behind interfaces.



Preferred integration:



```cmake

add\_subdirectory(egress-control)

```



Only add this line to the appropriate existing plugin CMake file if required by the current repository structure.



\## Do not implement



Do not add any of the following:



\* Browser URL loading

\* Browser Source customization

\* Local room joining

\* Local WebRTC room rendering

\* Local video encoding

\* YouTube token retrieval

\* Facebook token retrieval

\* OBS normal Start Streaming automation

\* OBS recording automation

\* Multistream output plugins

\* Room participant controls

\* Chat

\* Scene automation

\* New login screens

\* Replacement of existing OBS controls

\* Deletion of any existing code or UI



\## Logging



Use the standard OBS logging facilities.



Log safe events such as:



```text

Egress Control plugin loaded

Starting Egress request

Egress started successfully

Stopping Egress request

Egress stopped successfully

Backend request failed with HTTP 500

```



Never log:



```text

Authorization headers

Access tokens

Platform refresh tokens

YouTube/Facebook stream keys

Complete sensitive backend responses

```



\## User-visible error examples



The dock should display clear messages such as:



```text

Unable to connect to the server.

The request timed out.

The server rejected the request.

No active Egress session was found.

Egress could not be started.

Egress may still be active. Check its status before retrying.

```



Do not expose raw stack traces to normal users.



\## Build target



Primary target:



```text

Windows 11

Visual Studio 2022

x64

Latest official OBS Studio master branch

```



Follow the build conventions and dependency versions in the checked-out OBS repository. Do not assume old OBS build instructions.



\## Acceptance criteria



The task is complete only when:



1\. The latest OBS source builds successfully on Windows 11.

2\. All existing OBS GUIs remain available and unchanged.

3\. A separate `Egress Control` dock is available.

4\. The dock can be shown, hidden, moved, and docked like other OBS docks.

5\. Start sends one backend Start request.

6\. Stop sends one backend Stop request.

7\. The UI remains responsive during requests.

8\. Status and button states update correctly.

9\. Network and API failures are handled safely.

10\. No sensitive token is logged.

11\. No existing OBS code is deleted.

12\. Existing Browser Source behavior remains unchanged.

13\. Existing OBS streaming and recording behavior remains unchanged.

14\. The feature is isolated enough to be reapplied to a newer OBS source version with minimal conflict.

15\. The plugin unloads without crashes or resource leaks.



\## Required final output from the coding agent



After implementation, provide:



1\. A summary of the architecture used.

2\. A complete list of new files.

3\. A complete list of existing files modified.

4\. The reason for every existing-file modification.

5\. Full source code for the new plugin.

6\. CMake integration instructions.

7\. Windows 11 build commands.

8\. Backend API configuration instructions.

9\. Instructions for replacing the placeholder access-token provider.

10\. Testing steps for Start, Stop, timeout, server error, and OBS restart.

11\. A merge guide for applying the feature to future OBS versions.

12\. A Git patch or clearly separated commit containing only this feature.



Before writing code, inspect the latest checked-out OBS repository and adapt the implementation to its current public APIs and CMake conventions. Do not use outdated paths or APIs without verifying them in the repository.



\---



