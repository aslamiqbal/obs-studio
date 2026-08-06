# Egress Backend API — Contract & Rollout Plan

> **Revised after reading the backend.** Most of the failover design below
> already exists server-side as the **room renderer lease**
> (`RoomRendererLeaseController`), built for the Electron "iZMAsTM Studio"
> client. NVS is another desktop fallback renderer, so it should **reuse** those
> endpoints rather than add a parallel broadcaster registry. Sections D and E
> are superseded — see "Existing backend surface" immediately below.

## Existing backend surface (reuse, do not rebuild)

| Endpoint | Purpose |
| --- | --- |
| `GET  /api/rooms/{id}/renderer-lease` | Current ownership state |
| `POST /api/rooms/{id}/renderer-lease/acquire` | Claim desktop-fallback rendering |
| `POST /api/rooms/{id}/renderer-lease/heartbeat` | Keep the lease alive |
| `POST /api/rooms/{id}/renderer-lease/release` | Hand ownership back |
| `POST /api/rooms/{id}/renderer-lease/takeover` | Force takeover (`confirm_split_brain_risk`) |
| `POST /api/rooms/{id}/renderer-lease/renderer-session-capability` | Short-lived room **subscribe** token → the browser-source URL |
| `POST /api/rooms/{id}/renderer-lease/desktop-publish-manifest` | RTMP publish URLs + keys for the lease holder |

Key mechanics already implemented server-side:

- **Fence token** — monotonic ownership generation; every privileged call sends
  `{lease_id, session_id, fence_token}` as proof. This is the split-brain guard.
- **Lease expiry** — `RoomRendererLeaseWorker` reaps stale leases, so a dead NVS
  client releases automatically.
- **Credential separation** — the subscribe token (renderer page) and the RTMP
  manifest (trusted process only) are deliberately different endpoints.

**Implication for NVS:** on a failed heartbeat the client must stop publishing
itself. The API cannot fence a live RTMP push — that contract is already
documented in `RoomRendererLease` and NVS must honour it.

## Implemented in this pass

| Endpoint | Status |
| --- | --- |
| `POST /api/auth/native/start` | **new** — begins PKCE loopback sign-in |
| `POST /api/auth/native/approve` | **new** — web app binds the signed-in user |
| `POST /api/auth/native/token` | **new** — one-time code → tokens |
| `GET  /api/auth/native/status` | **new** — polling fallback |

Still required for the flow to work end to end: a `/desktop-login` page in the
web app. See "Remaining work".


Actors:

- **Backend** — orchestrator and single source of truth
- **VPS Egress** — server-side broadcaster (LiveKit Egress), normally live
- **OBS client(s)** — this plugin; controller, standby broadcaster, takeover target
- **YT / FB** — destinations; terminate a live after ~1–2 min without ingest data

Base rule: every request carries `Authorization: Bearer <access token>`; every
response is JSON; all timestamps ISO-8601 UTC.

---

## A. Authentication (reuse existing)

### A1. `POST /api/v1/auth/login`
Existing backend login. OBS uses it once, then persists the refresh token in the
plugin config dir (never in source, never logged).

```json
// request
{ "email": "operator@example.com", "password": "..." }
// response
{ "accessToken": "...", "refreshToken": "...", "expiresInSec": 900 }
```

### A2. `POST /api/v1/auth/refresh`
```json
{ "refreshToken": "..." }  →  { "accessToken": "...", "expiresInSec": 900 }
```

### A3. `GET /api/v1/auth/me`
Identity + role check (`operator`, `broadcaster`). Lets the console hide
controls the user is not allowed to use.

Plugin-side: implement `AccessTokenProvider` (already an interface in
`egress-config.hpp`) as a refreshing provider over A1/A2.

---

## B. Room

### B1. `GET /api/v1/rooms/active`
```json
{ "roomId": "rm_123", "name": "Sunday Service", "status": "live",
  "participantCount": 4, "startedAt": "2026-08-06T03:00:00Z" }
```

### B2. `POST /api/v1/rooms/{roomId}/broadcast-token`
Subscriber-only (hidden participant) token for rendering the room.

```json
// request
{ "clientId": "obs-...", "purpose": "broadcast-render" }
// response
{ "url": "https://view.example.com/room?token=...", "token": "...",
  "expiresAt": "2026-08-06T05:00:00Z" }
```

The OBS **browser source URL** embeds this. The plugin itself never touches
media; it only provisions the URL. Token is short-lived and renewable via the
same endpoint.

---

## C. Egress session (already implemented in the plugin)

### C1. `POST /api/v1/egress/start`
### C2. `POST /api/v1/egress/stop`
### C3. `GET /api/v1/egress/status` — **extend** the existing shape:

```json
{
  "success": true,
  "status": "live",
  "mode": "vps-egress",              // vps-egress | obs-broadcaster | none
  "serviceId": "svc_123",
  "egressId": "eg_456",
  "activeBroadcasterId": null,        // set when mode = obs-broadcaster
  "destinations": [
    { "platform": "youtube",  "health": "healthy",  "lastDataAt": "..." },
    { "platform": "facebook", "health": "degraded", "lastDataAt": "..." }
  ]
}
```

---

## D. Broadcaster registry, heartbeat & lease (failover core)

### D1. `POST /api/v1/broadcasters/register`
Called on OBS startup (after login).

```json
// request
{ "client": "obs-studio", "platform": "windows", "priority": 10,
  "capabilities": ["rtmp-push"], "label": "Studio PC 1" }
// response
{ "broadcasterId": "bc_789", "heartbeatIntervalSec": 5, "leaseTtlSec": 15 }
```

### D2. `POST /api/v1/broadcasters/{id}/heartbeat`
The workhorse. Doubles as lease renewal **and** command channel — the response
can carry directives, so failover works with zero extra infrastructure.

```json
// request
{ "state": "ready",                  // idle | ready | live | error
  "obsStreaming": false,
  "roomSourceLoaded": true,
  "metrics": { "kbps": 0, "droppedFramesPct": 0.0, "cpuPct": 12.5, "fps": 30.0 } }
// response
{ "leaseValid": true,
  "role": "standby",                 // standby | live
  "directives": [                    // usually empty
    { "type": "start-broadcast", "ingest": { "platform": "youtube",
        "rtmpUrl": "rtmp://a.rtmp.youtube.com/live2", "streamKey": "...",
        "expiresAt": "..." } }
  ] }
```

Directive types: `start-broadcast`, `stop-broadcast`, `refresh-room-token`,
`reload-config`.

### D3. `DELETE /api/v1/broadcasters/{id}` — clean deregister on OBS exit.

### D4. `POST /api/v1/broadcasters/{id}/claim`
Manual takeover from the Stream Console ("go live from here, now").
Backend either grants (returns ingest credentials immediately) or refuses with
the current holder's identity.

### D5. `POST /api/v1/broadcasters/{id}/release`
Voluntarily hand the live role back (backend then re-elects / resumes VPS).

**Lease semantics:** missing 3 heartbeats (~15 s) ⇒ lease expires ⇒ backend
promotes the highest-priority `ready` broadcaster, or falls back to VPS Egress.
The next heartbeat from the promoted client carries `start-broadcast`.

---

## E. Destinations & ingest credentials

### E1. `GET /api/v1/destinations`
```json
[ { "platform": "youtube",  "configured": true, "status": "ready",
    "title": "Sunday Live" },
  { "platform": "facebook", "configured": true, "status": "ready" } ]
```

### E2. `POST /api/v1/destinations/ingest-credentials`
Returns RTMP URL + stream key **only to the current lease holder**; anyone else
gets 403. Keys are short-lived where the platform allows re-issuing.
Normally unnecessary (D2 delivers credentials inside the directive); exists for
retry/refresh.

---

## F. Events (latency reduction — later phase)

### F1. `GET /api/v1/events/stream` (SSE) or `wss://…/api/v1/ws`
Events: `role.assigned`, `role.revoked`, `egress.status_changed`,
`destination.unhealthy`, `room.changed`.

Not required for correctness — the 5 s heartbeat already bounds reaction time —
but cuts takeover latency from ≤5 s to sub-second. Add only if the heartbeat
cadence proves insufficient.

---

## G. Ops

### G1. `GET /api/v1/health`
Unauthenticated liveness probe; console uses it to distinguish "backend down"
from "not logged in".

### G2. `POST /api/v1/client-events` *(optional)*
Client incident reports (stream dropped, encoder overload). Never contains
tokens or keys.

---

## Failover flow

```
NORMAL          VPS Egress → YT/FB primary ingest
                OBS: registered, state=ready, heartbeat every 5 s
                     browser source already joined room (B2 token)

FAILURE         Backend sees egress webhook failure / ingest silence
                → lease to best ready broadcaster (priority, health)
                → next heartbeat response: role=live + start-broadcast + keys

TAKEOVER (OBS)  plugin sets rtmp_custom service from directive
                → obs_frontend_streaming_start()
                → heartbeat now state=live, kbps>0
                → backend confirms platform ingest healthy

RECOVERY        VPS healthy again → directive stop-broadcast after
                backend confirms VPS is pushing (or operator decides)
```

**Timing budget:** detect ≤15 s (3 missed heartbeats) + directive ≤5 s (next
heartbeat) + OBS start ~3–5 s ⇒ **~20–25 s worst case**, comfortably inside the
YT/FB grace window.

**Zero-gap option:** YouTube (and FB) provide a *backup ingest* URL that may be
fed simultaneously with the primary. Point VPS Egress at primary and the OBS
standby at backup; platform switches instantly with no key handover. Recommended
for the final phase — it converts failover from "resume" to "already there".

---

## Remaining work

### 1. Web app `/desktop-login` page — REQUIRED, blocks sign-in

The desktop flow cannot complete without it. The page must:

```ts
// /desktop-login?session=<uuid>
// 1. Ensure the user is signed in (existing Google flow); if not, sign in first
//    and come back to this URL.
// 2. Show what is being authorised: "Sign in to NVS on <device_label>?"
// 3. On confirm:
const res = await api.post('/api/auth/native/approve', { session_id });
window.location.href = res.data.redirect_url;   // http://127.0.0.1:<port>/nvs-auth/callback?...
```

Show an explicit confirm step rather than approving automatically — the click is
what stops a malicious page from silently minting desktop credentials via an
already-authenticated session.

### 2. Room discovery and auto-join (NVS side)

1. List rooms the operator may render — needs a "rooms I can broadcast" list
   endpoint; `RoomsController` currently exposes per-room reads.
2. `acquire` the renderer lease.
3. `renderer-session-capability` → set the browser source URL to the returned
   join URL.
4. Heartbeat on a timer; stop publishing on heartbeat failure.
5. `desktop-publish-manifest` → apply RTMP URL to the stream service, start
   streaming.

### 3. Multi-instance note

Native auth sessions live in `IMemoryCache`. That is correct for a single API
instance and for their 5-minute lifetime, but behind a load balancer the
`start`, `approve`, and `token` calls must reach the same instance. Move them to
the distributed cache or a small table before scaling out. This is the one
deliberate trade-off in the new code.

## Rollout phases

| Phase | Scope | Endpoints | Plugin work |
| --- | --- | --- | --- |
| 0 ✅ | Egress start/stop/status | C1–C3 | done |
| 1 | Real auth | A1–A3, G1 | refreshing `AccessTokenProvider`, login UI in console |
| 2 | Room provisioning | B1, B2 | auto-configure browser source URL, token refresh |
| 3 | Registry + heartbeat | D1–D3, C3 ext. | 5 s heartbeat timer, metrics from `obs_output_*`, directive handler |
| 4 | Takeover | D4, D5, E1, E2 | apply ingest credentials to service, auto start/stop streaming, console "Take over" button |
| 5 | Latency + zero-gap | F1 | WS/SSE client; backup-ingest dual-push mode |

Order matters: 3 before 4 (a directive needs a heartbeat to ride on), and 4
before 5 (WS only shaves latency off an already-working path).

## Security invariants

- Stream keys and tokens: delivered only to the lease holder, held in memory,
  never logged, never written to disk by the plugin.
- All non-loopback traffic HTTPS (already enforced in `EgressConfig`).
- Heartbeat auth = same bearer token; a stolen broadcasterId alone is useless.
- `claim` requires the `broadcaster` role; console hides it otherwise.
