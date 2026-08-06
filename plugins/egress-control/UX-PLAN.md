# NVS — Operator UX Plan

The product in one sentence: **NVS renders a nadavox room and relays it to live
platforms. It is a forwarder, not a capture tool.**

## Architecture decision: NVS encodes and pushes

Decided 2026-08-07. NVS is the encoder, not a remote control — it renders the
room in a browser source, encodes locally, and pushes RTMP itself. This is the
"replaces a metered cloud egress" path the backend's `ObsEgressUrlService`
already describes.

The consequence that shaped the build: **OBS has exactly one streaming output**
(`OBSOutputAutoRelease streamOutput`, singular). Stock OBS cannot reach more
than one destination, so the multi-destination requirement is not reachable
through OBS's own Start Streaming at all.

`nvs-multi-rtmp.*` solves that: one shared x264 + AAC encoder pair feeding N
`rtmp_output` objects. The frame is encoded **once** and the compressed packets
are fanned out, so adding a destination costs bandwidth and not CPU.

Implications, recorded so they are not rediscovered later:

- Server-side stream targets are now a **record** of destinations, not the live
  path. "Save to Server" keeps the dashboard and reward reporting meaningful;
  it does not start anything.
- OBS's own Stream section (Service / Server / Stream Key + Start Streaming) is
  a **second, conflicting live path**. Using both at once would encode twice and
  push the same room from two places. See "Open question" below.

## The flow

```
1. Download installer pack
2. Setup (install, first run)
3. Sign in            — Google via system browser, JWT stored encrypted
4. Start with Windows — optional checkbox (default OFF for now; revisit)
5. Room               — pick a room, or paste nadavox URL + token manually
6. Broadcast setup    — add destinations:
                          YouTube  : live URL + token   (MULTIPLE allowed)
                          Facebook : live URL + token   (MULTIPLE allowed)
7. Go live            — Start forwards the room's video AND AUDIO to every
                        enabled destination
```

## Audio policy (explicit, because it is easy to get wrong)

**NVS does not capture audio.** No microphone, no desktop audio. The room's own
audio arrives through the browser source that renders the room, is mixed by
OBS, and is forwarded to the destinations. That is the only audio path.

Consequences for the implementation:

- The NVS profile must have **Desktop Audio and Mic/Aux global devices
  disabled**. Left at OBS defaults they capture the operator's machine, which
  both leaks the operator's environment and double-mixes room audio the moment
  the operator monitors the stream.
- The room browser source keeps its audio routed through OBS (not
  "control via OBS" muted, not monitor-only), because its audio IS the
  broadcast audio.
- First run should enforce this; the operator should not have to know OBS
  audio settings exist.

## Status vs. this plan

| Step | State | Notes |
| --- | --- | --- |
| 1. Installer pack | **Not started** | `cmake/windows/cpackconfig.cmake` exists upstream; add a `-Package` mode to `build-nvs.ps1` later |
| 2. Setup / first run | **Partial** | App runs; no first-run experience; audio policy not yet enforced |
| 3. Google sign-in, JWT stored | **Built** | PKCE loopback + DPAPI storage; blocked on `/desktop-login` page deployment |
| 4. Start with Windows | **Built** | Checkbox in console, off by default — matches "do later" |
| 5. Room URL + token, manual or fetched | **Built** | Room group: selector + Get, or paste both by hand |
| 6. Multiple destinations per platform | **MISMATCH** | Current UI is one fixed row per platform with token only. Spec wants a LIST of destinations, each with URL + token |
| 7. Forward room audio, capture nothing | **MISMATCH** | Nothing disables the global audio devices yet |

## The two fixes

### Fix A — Destinations become a list

The current `☐ Youtube [token]` / `☐ Facebook [token]` rows are replaced by a
table:

```
Destinations                                    [+ YouTube] [+ Facebook]
┌─────┬──────────┬──────────────────────────────┬───────────┬────┐
│ On  │ Platform │ Live URL                     │ Token     │  ✕ │
├─────┼──────────┼──────────────────────────────┼───────────┼────┤
│ ☑  │ YouTube  │ rtmp://a.rtmp.youtube.com/…  │ ••••••••  │  ✕ │
│ ☑  │ YouTube  │ rtmp://b.rtmp.youtube.com/…  │ ••••••••  │  ✕ │
│ ☐  │ Facebook │ rtmps://live-api-s.facebook… │ ••••••••  │  ✕ │
└─────┴──────────┴──────────────────────────────┴───────────┴────┘
                                              [Save to Server]
```

The backend already supports this: a room has N `StreamTarget` rows, each with
`platform_type`, `rtmp_url`, `stream_key_encrypted`, `is_enabled`. The
one-row-per-platform limit was only ever a UI assumption. This also means
`rtmp_url` now comes from the operator (it was previously sent empty), which
maps to the entity exactly.

Start Service starts every enabled target; Stop stops all. Keys are write-only
as before: blank token on save = keep the stored key.

### Fix B — Enforce the audio policy

On console startup (per profile, once):

- Set Desktop Audio and Mic/Aux global devices to Disabled in the profile.
- Log one line saying so, so an operator who *wants* local audio can see what
  turned it off and override in OBS settings if they insist.

## Open question

The console now shows two ways to go live:

1. **Start Service** — the NVS multi-RTMP engine, N destinations
2. **Start Streaming** — OBS's own single output, using the Stream section

Only (1) satisfies the multi-destination requirement. Leaving both visible
invites an operator to start both and push the same room twice. Options:

- Remove the Stream section and Start Streaming entirely (cleanest for a
  single-purpose appliance)
- Keep them, clearly labelled as a single-destination fallback, and refuse to
  start one while the other is live

## Deferred (agreed)

- Installer pack (step 1) and a first-run wizard (step 2)
- Auto-add to startup during setup (step 4 stays manual)
- Per-room credential memory (today the last room's address/token is stored;
  keying the store by room id is a small later upgrade)
