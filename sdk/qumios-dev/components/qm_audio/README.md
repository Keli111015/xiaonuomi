# qm_audio

This document defines the qm_audio layer model, module boundaries, media
contracts, and validation scope. Public headers are the interface baseline;
implementation and tests confirm behavior. Resolve documentation differences
through the applicable interface contract and implementation revision.

Chinese version: [README_CN.md](README_CN.md).

Architecture atlas: [ARCHITECTURE.md](ARCHITECTURE.md); [Chinese](ARCHITECTURE_CN.md).

| Item | Description |
| --- | --- |
| Scope | components/qm_audio and its generic codec/device adapter boundary |
| Framework model | One default audio instance, one physical output route, PCM or native encoded media |
| Primary evidence | include/, include/adapter/, src/, and tests/ |
| Not proof of | Host mock tests do not prove target hardware acoustics, board wiring, networking, or cloud protocol integration |

## Document-to-Implementation Mapping

Table 1 maps the major design items to their implementation locations.

| Current fact | Source anchor |
| --- | --- |
| Default instance creation and destruction | src/audio/qm_audio.c |
| Handle lifetime and subscription quiescence | include/qm_audio_control.h, include/qm_audio_capture.h, and src/control/qm_audio_control.c |
| Fixed profiles, codec/framing, and packet types | include/qm_audio_types.h |
| Control creation, playback, and PUSH input | include/qm_audio_control.h and src/control/qm_audio_control.c |
| Service owns playback sessions only | include/qm_audio_service.h |
| Fixed device capability validation | src/engine/qm_audio_engine.c |
| Independent native Capture | src/capture/qm_audio_capture.c and include/qm_audio_capture.h |
| Operation state, events, and release | include/qm_audio_control.h and src/control/qm_audio_control.c |
| Capacity and bounded-backpressure configuration | qm_kconfig and src/engine/qm_audio_engine.c |
| Device and codec adapter contracts | include/adapter/ and the matching platform implementation |
| Optional frontend wake and capture-feed boundary | include/adapter/qm_audio_frontend.h, include/adapter/qm_audio_device.h, and qm_audio.h |
| Optional cloud-adapter lifecycle | include/cloud/qm_audio_cloud.h and src/cloud/qm_audio_cloud.c |
| Automatically checked core contracts | components/qm_audio/tests and integration test suites |

## Layered Architecture

~~~mermaid
flowchart TB
    App["Product application / protocol adapter"]
    Source["Media input<br/>FILE / URI / MEMORY / CUSTOM / PUSH"]
    Consumer["Capture consumer<br/>QUEUE (pull) / CALLBACK"]

    subgraph Core["qm_audio"]
        direction TB
        subgraph L1["1. Business control layer"]
            Control["Control<br/>operation, event, snapshot, PUSH"]
        end
        subgraph L2["2. Session scheduling layer"]
            Service["Service<br/>admission, queue, priority, preemption, drain"]
        end
        subgraph L3["3. Media processing layer"]
            Pipeline["Pipeline<br/>source, route, passthrough, decode/encode"]
        end
        subgraph L4["4. Device routing layer"]
            Engine["Engine<br/>fixed profile, render, device lifecycle"]
            Capture["Independent Capture<br/>reader, subscription queues, callbacks"]
        end
    end

    subgraph Adapter["Platform adapter"]
        Device["device adapter<br/>device I/O"]
        Codec["decoder / encoder provider<br/>format processing"]
    end
    Hardware["Device / board implementation"]

    App --> Control
    Control -->|create / validate| Pipeline
    Control -->|submit session| Service
    Service -->|grant output| Pipeline
    Pipeline --> Engine
    Source --> Pipeline
    Pipeline -. codec provider .-> Codec
    Engine -->|playback| Device --> Hardware
    Device -->|native capture| Capture
    Device -. get_caps .-> Engine
    Engine -. owns lifetime .-> Capture
    Capture --> Consumer --> App
    Control -->|event / snapshot| App
~~~

Table 2 defines the framework layers and platform boundary. The default
qm_audio instance creates and shares the Service and Engine objects, the
instance event executor, and Source worker during initialization; Controls are
created per business domain.

| Layer | Core object | Responsibility | Excludes |
| --- | --- | --- | --- |
| Business control | Control | Accepts business commands; owns operations, events, snapshots, and PUSH ingress | Cross-session scheduling, codec processing, device I/O |
| Session scheduling | Service | Session admission, queueing, priority, preemption, cancellation, and drain completion | Source reading, media processing, device access |
| Media processing | Pipeline | Source management, route planning, PCM/byte/packet delivery, decode/encode | Device ownership, session scheduling, product policy |
| Device routing | Engine | Fixed-profile validation, device ownership, render I/O, and Capture object lifetime | Capture internals, URI reading, codecs, product protocol |
| Native capture | Capture | Device capture I/O, independent reader/dispatcher, bounded subscriptions | Engine/Service/Pipeline state, format conversion, network transport |
| Platform boundary | device adapter and codec provider | Publishes actual capabilities; implements device I/O and declared format processing | Control/Service state machines and product workflows |

Solid arrows show calls or media data. Dotted arrows show capability queries or lifetime ownership.
Control creates Pipeline and submits the session; Service grants Pipeline output
ownership after admission. The playback-media path is `Pipeline -> Engine ->
device adapter`. Capture independently reads the device and delivers to subscribers;
it does not depend on Engine or enter a Service playback session. Route combinations are specified in section 4; the
Capture lifecycle is specified in section 6.

### Execution Model and Concurrency Constraints

The framework uses one output route and one Source worker. There is no multi-worker option or standalone Pipeline task. These constraints define execution, synchronization and resource reclamation.

| State or resource | Owner | Cross-task rule |
| --- | --- | --- |
| Operation state | `audio_event` | Commands are serialized; external queries read protected snapshots and each terminal result is published once |
| Service admission, preemption, and action state | Shared Service action core | `audio_event` advances it at runtime; external synchronous calls drive the same core. The Service lock protects registration and state commit. |
| Source cursor, codec execution and run generation | `audio_source` | Pause retains context and yields the worker; cancellation unblocks I/O and the old execution exits before context reconstruction |
| Output authorization, queue accounting, media counters, and volume/mute | Engine / `audio_render` | Pipeline submits directly through a stable bound output lease; revoking authorization and freeing a lease are separate operations. Volume/mute is ordered by `audio_event` and reaches the device only on the render worker. |
| Capture subscription table and consumer queues | Independent Capture | The PULL reader captures and enqueues; private `audio_cap_cb` runs callbacks and QUEUE uses the consumer's task |
| Borrowed source/context and operation storage | Application and Control | Borrowing ends only after successful release; timeout retains objects for retry |

Each media block follows `Pipeline → Engine render queue → device adapter`, without acquiring Control/Service state locks. State transitions and handle borrowing still use short critical sections. PUSH/Capture rings and asynchronous codec output require synchronization with cancellation. These locks must not cover application callbacks, Source I/O, or worker-exit waits.

External `qm_audio_set_volume()` and `qm_audio_set_mute()` calls hold the
instance setting slot until their worker result is consumed. Calls made from an
audio callback acquire that slot without waiting and return `-QM_EBUSY` while
another caller owns the request or its unread result.

Cancellation has four distinct boundaries:

1. Request accepted: latch cancellation, revoke this run's write access, and wake data/space waiters.
2. Execution quiescent: Source I/O returns, codec output stops, and the Source worker leaves the run.
3. Route reusable: previous device writes and drain have ended, allowing another session to acquire output.
4. Storage reclaimable: event posts, API borrows and callbacks are quiescent before source, codec and operation storage are freed.

CUSTOM cancellation must be thread-safe, idempotent and nonblocking; it must not free the context. Open/read must return after cancellation. Cancellation cannot depend on the blocked worker consuming another command. HTTP uses interruption checks rather than concurrent cleanup of a live HTTP/TLS object. DNS, connection and TLS handshake bounds remain a transport-adapter contract. Products requiring a fixed preemption deadline must verify that boundary or feed audio through PUSH from their network task.

Only natural EOF flushes codecs and drains output. Cancellation and errors discard codec tails. Successful drain releases device authorization while the terminal operation retains its result until application release. Quiescence waits report execution exit separately from the media result, so a media timeout cannot be mistaken for an incomplete cleanup wait.

Pipeline wait confirms Source execution exit, not successful provider closure. If provider close fails, destroy/rewind retries it after execution exits. Borrowed contexts remain valid until destroy or application release succeeds.

Validation covers cancellation during blocked open/read, PUSH backpressure, asynchronous output, pause yielding, late output from an older generation, a blocked event callback with durable completion state, natural completion handing over the route before operation release, and retryable cleanup timeout. Host tests cover concurrency and ownership. Adapter interruption latency, task stacks and audible boundaries require target validation.

## 1. Scope and Constraints

### 1.1 Application entry header

Applications should include `qm_audio.h` as the default entry header. It
re-exports the shared media types, Control API, and Capture API while keeping
their declarations in `qm_audio_control.h` and `qm_audio_capture.h`. The
top-level declarations cover default-instance lifecycle, fixed stream-format
queries, route capability queries through `qm_audio_get_caps()`, and shared
volume/mute controls.

`qm_audio_get_caps()` reports the opened route selected during
`qm_audio_init()`. Its flags describe capture/playback availability, shared
output controls, abort/drain/position behavior, and stop semantics. They do
not enumerate codec providers, imply full-duplex support, or permit runtime
format reconfiguration. Use Control for business playback and Capture for
capture delivery; include Engine, Service, Pipeline, adapter, frontend, or
cloud headers only when implementing those lower-level boundaries.

qm_audio is the reusable embedded-audio control, scheduling, media-processing,
and device-routing component. Product startup calls qm_audio_init() to create
the process-wide default instance. Each business domain creates its own Control
while sharing the same Service and Engine.

The following constraints apply:

- One Engine manages one physical device route. One Service is the only session
  admission and preemption authority for that route.
- A Service session represents playback only. Recording and duplex sessions are
  not Service APIs; capture uses an explicit independent Capture handle.
- Control owns business operations but never writes the device directly.
  Pipeline processes one media operation but never schedules across sessions.
  Engine is the only physical-device owner.
- Pipeline may select PCM, encoded byte-stream, or complete encoded-packet
  routes. It does not provide arbitrary media graphs, dynamic plugins, or a
  generic codec registry.
- qm_audio does not resample, change channel count or sample width, mix,
  mux/demux containers, or infer packet boundaries from arbitrary byte chunks.
- A platform adapter must explicitly reject unsupported profiles. It must not
  silently reinterpret encoded media as PCM.

## 2. Object Ownership and Startup

| Object | Implemented responsibility | Does not own |
| --- | --- | --- |
| qm_audio | Default-instance lifecycle, shared source worker, instance resources | Product resource rules and cloud business flow |
| Control | Business operations, events, snapshots, PUSH writes | Codec work, physical writes, cross-session arbitration |
| Service | Admission, priority, queueing, preemption, cancellation, drain, terminal state | Source I/O, codecs, device SDK |
| Pipeline | Sources, route planning, passthrough, decode, encode, media callbacks | Device ownership and preemption policy |
| Engine | Fixed device profile, rendering, device and Capture object lifetime | Capture internals, URI reads, format parsing, product policy |
| Capture | Native capture, bounded fan-out, reader and lazy callback task | Upper-layer state, codecs, transport |
| adapter | One MCU/device codec or I/O binding | Service, Control, and cloud state machines |

The required startup and shutdown order is:

1. Set fixed input_format and output_format profiles supported by the device.
2. Call qm_audio_init().
3. Create one Control for each business domain.
4. Submit qm_audio_control_play() and observe events, snapshots, or wait APIs.
5. Stop and release operations, unsubscribe Capture, shut down and destroy
   Controls, then call qm_audio_deinit().

qm_audio_init() succeeds repeatedly with the same configuration. A different
configuration returns -QM_EBUSY instead of reconfiguring a live device. Failed
shutdown never frees objects that a backend may still access. Retain handles and
retry cleanup.

Service shutdown succeeds only after active and queued sessions are quiescent.
It returns `-QM_EBUSY` while a concurrent session release owns required output
cleanup; admission remains closed. Wait for release to complete and retry.

When assembling Engine and Service directly, choose one output owner. Public
`qm_audio_engine_render_open()` is for an Engine with no Service attached.
Service creation rejects an existing render stream, and Service opens streams
through a private entry that checks its identity. Service retains the route
until it is destroyed: shut it down, release all sessions, and destroy it before
Engine shutdown. Engine queries use the complete native stream profile;
PCM-only convenience queries remain on the `qm_audio` application facade.

An Engine stream has separate media-completion and resource-release barriers:

| Engine stream path | Required calls |
| --- | --- |
| Normal completion | `render_drain()` -> `render_stop()` -> `render_close()` |
| Cancellation | `render_stop()` -> `render_close()` |
| Failed or timed-out open with a non-NULL output handle | Retain the handle, retry `render_stop()`, then `render_close()`; do not write media |

Drain confirms that accepted media reached the physical output boundary; it
does not retire the backend run. Stop completes that barrier, including after a
successful drain. Close only releases a stopped, quiescent stream: it neither
starts cancellation nor waits for it, and returns `-QM_EBUSY` if stop is still
required. A stop timeout keeps the route and pending action alive for retry.

`qm_audio_engine_shutdown(timeout_ms)` uses one remaining budget across its
Capture, render-action and worker-exit waits. Synchronous adapter operations
without a timeout follow their documented finite platform bounds in addition;
the parameter is not a strict wall-clock return deadline. Zero skips core waits
but may still call those synchronous operations. Only successful cleanup
permits destruction or physical route reuse.

### 2.1 Runtime Tasks and Execution Contexts

Tasks follow runtime ownership rather than a one-task-per-layer rule. The
`audio_*` names below identify the current implementation and are not public API
names. Control and Service do not create a task per Control, operation, or
session.

| Layer or boundary | Task or execution context | Creation and count | Work | End condition and restriction |
| --- | --- | --- | --- | --- |
| Business control | `audio_event` | One per audio instance; the default instance creates it during `qm_audio_init()` | Serial command execution and public Control event dispatch | Instance shutdown posts an EXIT barrier. Control callbacks must not synchronously run lifecycle APIs or block this task. |
| Session scheduling | No dedicated task | No per-Service or per-session worker | Runtime Service ticks advance the shared action core on `audio_event`; external synchronous callers wait for Engine progress and drive the same core, so cleanup does not depend on a live event task | Service priority is a scheduling policy, not an RTOS task priority. There is no separate Service action task or queue. |
| Media processing | `audio_source` | One resident worker per audio instance | Source I/O, codec calls, direct media submission to Engine and drain waits | Acknowledged pause retains context and yields execution. The old run exits before another executes. There is no standalone Pipeline task. Shutdown waits for the registration table to empty. |
| Device routing, render | `audio_render` | One per Engine when its output profile is valid | Executes backend start/write/pause/resume/stop, position queries, and volume/mute calls | Drain reserves exclusive device access in its caller; stop signals abort from its caller. Engine shutdown records a persistent exit request and waits for worker exit. The worker does not invoke application media callbacks. |
| Capture PULL input | `audio_capture` | Created on demand after synchronous device start succeeds; at most one per Capture; absent in PUSH mode | Reads native PCM or complete packets and copies into bounded subscriber queues | Last unsubscribe, Capture shutdown, or source error ends reading; the reader stops the device before exiting. No application callbacks execute here. |
| Capture callback delivery | `audio_cap_cb` | At most one per Capture, created lazily by the first CALLBACK or `state_cb` subscription; absent for QUEUE without `state_cb` | Rotates between ready subscriptions and runs serial state/media callbacks outside Capture locks | Shutdown waits for dispatch exit; unsubscribe waits for borrowed callbacks. Slow callbacks affect other callbacks, not the reader, QUEUE delivery or Control event loop. |
| Application/protocol adapter | `audio_cloud` (optional) | One per optional cloud-adapter handle when cloud support is enabled | Owns the protocol work queue and translates external work into Control calls | Not part of the four core layers or the audio I/O path. Network work stays in this context, outside audio locks and media callbacks. |
| Platform boundary | Adapter/provider-private task | Defined by the enabled device adapter or codec provider | Device, DMA, SDK-callback, or codec implementation work | qm_audio neither creates nor includes these tasks in its core budget. The adapter document must state stack, priority, cancellation, and shutdown behavior. |

Render commands share the worker's retained action state; synchronous callers
wait for the same result that Service's asynchronous path polls. Position
queries have a separate retained result slot and cannot overwrite an unread
Service control result. A new query returns `-QM_EBUSY` while another query
request or waiter exists, or while drain is active. An accepted query completes
before device drain begins. Device drain runs on the calling task after queued
writes and ordinary device actions have quiesced. While drain owns the device,
pause/resume requests return `-QM_EBUSY`, and queued shared settings wait for its
release. Stop signals abort from its caller so it can interrupt a worker blocked in write or a caller
blocked in drain; backend stop still runs on `audio_render`. A stopped stream
returns cached device-position observations; a drained stream can still query
the device through the worker.

For an audio instance with output enabled, the core live-task count is
three for `audio_event`, `audio_source`, and `audio_render`. Capture adds one
`audio_cap_cb` only when CALLBACK or `state_cb` is used and, in PULL mode,
one `audio_capture` while the source runs. QUEUE without `state_cb` adds no
callback task; PULL still needs its reader.
Optional protocol-adapter and
adapter-private tasks are additional.

Task stack and RTOS priority are build-time resources, not Service scheduling
priorities. The relevant options are:

| Task | Stack option | RTOS priority option | Count or queue bound |
| --- | --- | --- | --- |
| `audio_event` | `CONFIG_QM_AUDIO_CONTROL_EVENT_TASK_STACK_SIZE` | `CONFIG_QM_AUDIO_CONTROL_EVENT_TASK_PRIORITY` | One per audio instance; event queue and pending-fact limits also apply |
| `audio_source` | `CONFIG_QM_AUDIO_CONTROL_SOURCE_TASK_STACK_SIZE` | `CONFIG_QM_AUDIO_CONTROL_SOURCE_TASK_PRIORITY` | One per instance; session capacity does not change task count |
| `audio_render` | `CONFIG_QM_AUDIO_ENGINE_RENDER_TASK_STACK_SIZE` | `CONFIG_QM_AUDIO_ENGINE_RENDER_TASK_PRIORITY` | One per output-enabled Engine; render queue is bounded |
| `audio_capture` | `CONFIG_QM_AUDIO_CAPTURE_TASK_STACK_SIZE` | `CONFIG_QM_AUDIO_CAPTURE_TASK_PRIORITY` | Zero or one per PULL Capture while running |
| `audio_cap_cb` | `CONFIG_QM_AUDIO_CAPTURE_CALLBACK_TASK_STACK_SIZE` | `CONFIG_QM_AUDIO_CAPTURE_CALLBACK_TASK_PRIORITY` | One per Capture when CALLBACK or `state_cb` is used |
| `audio_cloud` | `CONFIG_QM_AUDIO_CLOUD_TASK_STACK_SIZE` | `CONFIG_QM_AUDIO_CLOUD_TASK_PRIORITY` | One per initialized cloud adapter; cloud queue and operation-map limits also apply |

The integration record must justify these values with stack watermark, queue
latency, and callback-duration measurements. Changing an RTOS priority does not
change Service priority or operation admission order.

### 2.2 Data and Context Ownership

| Object | Caller responsibility | Component behavior |
| --- | --- | --- |
| qm_audio_config_t | Valid for the call | Copied during initialization |
| FILE/URI path or URL | Valid for the call | String is copied by Pipeline |
| MEMORY source data | Valid until operation release | Borrowed and never freed by Pipeline |
| CUSTOM source context/callbacks | Valid until operation release | Borrowed and never freed by Pipeline |
| `qm_audio_data_t` | For producer writes and callbacks, valid during the documented call | Payload and descriptor are borrowed; copied before successful queue acceptance and never retained after callback return |
| Adapter backend context | Valid until Engine close succeeds | Exclusively used between device open and close |

### 2.3 Handle Concurrency and Destruction

Internal locks protect component state, but they do not assign product ownership
of handles. The application must serialize each handle's terminal transition and
exclude new calls after destruction starts. Calls that change one operation's
state should also be serialized by its business owner; the framework does not
define a winner for concurrent pause/seek/stop/release requests.

| Handle | Required application serialization | Final ownership barrier |
| --- | --- | --- |
| Control | Do not start shutdown or destroy while another business task can submit commands. Shut down the Control, release every terminal operation, and remove every event subscription before destroy. | `qm_audio_control_destroy()` succeeds only after shutdown, zero retained operations, zero event subscriptions, and quiescent service/event use. |
| Operation ID | Keep borrowed source/context data alive until `release()` succeeds. Do not start a new command for the same operation while its release is in progress. | A successful release invalidates the operation ID. A timeout or other failure retains the ID and cleanup responsibility. |
| Control event subscription | Unsubscribe from a business task, never from the shared event task. | `qm_audio_control_unsubscribe()` waits for an in-flight callback; its callback context may be freed only after success. |
| Capture subscription | Serialize `enable`, `flush`, and `unsubscribe` for one subscription. QUEUE delivery permits one `recv()` consumer at a time; CALLBACK delivery cannot use `recv()`. | Once unsubscribe begins, the owner excludes new handle access. Success is the callback/I/O quiescence barrier; failure retains the closing handle for retry. |
| Default instance and optional adapters | Stop cloud, frontend, and adapter-owned work before Controls and `qm_audio_deinit()`. | A failed stop, close, unsubscribe, shutdown, or release keeps its handle and borrowed context live until a later successful retry. |

## 3. Fixed Media Profiles and Three Representations

qm_audio_stream_format_t is the fixed device input or output media profile. For
PCM, pcm is the real sample geometry. For encoded media, it is the PCM geometry
that a decode/encode path must preserve. Opaque encoded passthrough may use an
all-zero pcm declaration, but both sides must explicitly use the same one.

| Representation | Profile condition | Pipeline/Engine delivery |
| --- | --- | --- |
| PCM | codec is QM_AUDIO_CODEC_PCM; packet fields are zero; framing is STREAM | `qm_audio_data_t` aligned to complete sample frames |
| Encoded byte stream | Non-PCM codec and max_packet_bytes is zero | write_bytes; chunks do not imply codec-frame boundaries |
| Complete encoded packet | Non-PCM codec and max_packet_bytes is nonzero | `qm_audio_data_t`; each call carries one complete packet |

packet_bytes is a fixed packet size; zero means variable size.
packet_duration_ms is fixed packet duration; zero means unknown or variable.
Neither is a wildcard. Native packet passthrough requires exact
packet_duration_ms. For packet_bytes, a fixed-size sink requires equality; a
variable-size sink accepts a fixed or variable source declaration that does not
exceed max_packet_bytes.

`qm_audio_data_t` contains data, size, sequence, timestamp_ms, duration_ms, and
flags. `QM_AUDIO_DATA_TIMESTAMP_VALID` and `QM_AUDIO_DATA_DURATION_VALID` control
time-field validity. `QM_AUDIO_DATA_DISCONTINUITY` reports missing or reset
predecessor media. An empty packet, or a
packet that violates a fixed packet size, is invalid media rather than EOS. A
variable-size profile accepts any nonzero payload up to max_packet_bytes.
Temporary network starvation never means EOS. Normal completion is expressed by
the finish/drain path.

### 3.1 Source Declarations

A play request uses qm_audio_codec_config_t to declare the source codec,
framing, optional PCM geometry, and packet fields. PUSH + PACKET is the
complete-packet entry point. Ordinary PUSH writes may split or combine PCM or
encoded bytes, but cannot define packet boundaries.

Pipeline validates the following framing rules at creation:

- Ogg, RAW_HEADER, and CBR are Opus-only framings.
- Non-Opus MP3, G.711, G.722, AUTO, and other processing inputs use STREAM
  with packet_duration_ms equal to zero.
- A nonzero packet_duration_ms for provider processing is currently limited to
  Opus RAW_HEADER/CBR. A complete native PACKET may declare a fixed duration
  for a product-private codec.
- Private codecs start at QM_AUDIO_CODEC_PRIVATE_BASE. A compatible profile can
  pass them through without a provider; qm_audio does not inspect their payload.

### 3.2 Source Capability Matrix

Source type controls lifetime, end-of-input, and seek behavior independently of
the codec route. The source matrix is part of route admission; it is not a
promise that every source is enabled in every build.

| Source | Build prerequisite | End-of-input | Seek/restart | Ownership and cancellation |
| --- | --- | --- | --- | --- |
| MEMORY | None | The declared byte range is exhausted | Processing routes can restart and decode from the beginning; passthrough remains non-seekable | Buffer is borrowed and must remain unchanged until operation release; stop closes the run without freeing it |
| FILE | `CONFIG_QM_AUDIO_SOURCE_FILE_SUPPORT` | File read returns zero bytes | Local file processing routes can reopen/replay; native byte/packet passthrough is non-seekable | Path is copied; file handle is owned by Pipeline and closed during cleanup, with retry on close failure |
| URI | `CONFIG_QM_AUDIO_SOURCE_URI_SUPPORT` | HTTP read returns zero | Processing routes require a finite Content-Length and strong ETag, then re-download and validate the prefix; native byte/packet passthrough is non-seekable | URL is copied; HTTP context remains live until cleanup succeeds; a timeout is not permission to free it |
| PUSH | None; Control creates the bounded ring | `qm_audio_control_stream_finish()` is the only normal EOS | Non-seekable; pause may retain buffered state, but restart/seek is not implied | The operation owns the ring and destroys it at release; producer buffers are borrowed only for each write and writes observe backpressure/cancellation |
| CUSTOM | None; open/read/cancel/close callbacks are required | Callback returns success with `read_size == 0` | Non-seekable because the contract has no seek callback | open(ctx, timeout_ms), read and close are serialized; concurrent cancel latches interruption and wakes I/O; the context remains borrowed until release |

Stop, cancellation, and a failed read close the current source/codec generation;
they do not flush a codec tail. Only natural EOF enters decoder flush, encoder
finish, and Engine drain.

## 4. Route Planning and Limited Transcoding

The file-local build_route() in Pipeline is the only playback route planner.
Control creates a Pipeline before Service admission. At that time it validates
the source declaration, fixed sink profile, and compiled provider capabilities;
it does not open the source or create codec instances. After Service admission,
Pipeline prepare allocates packet buffers and encoder state.

~~~mermaid
flowchart TB
    Source["Source codec / framing / packet contract"]
    Output["Fixed Engine output profile"]
    Capability["Compiled decoder / encoder capabilities"]
    Planner["build_route()"]
    DirectPcm["Direct PCM"]
    BytePass["Encoded-byte passthrough"]
    PacketPass["Native-packet passthrough"]
    DecodePcm["Decode to PCM"]
    PcmEncode["PCM encode"]
    Transcode["Decode to PCM, then encode"]
    Engine["Engine"]

    Source --> Planner
    Output --> Planner
    Capability --> Planner
    Planner --> DirectPcm --> Engine
    Planner --> BytePass --> Engine
    Planner --> PacketPass --> Engine
    Planner --> DecodePcm --> Engine
    Planner --> PcmEncode --> Engine
    Planner --> Transcode --> Engine
~~~

| Route | Selection condition | Required provider |
| --- | --- | --- |
| Direct PCM | Source and output are matching PCM | None |
| Encoded-byte passthrough | Matching codec, framing, and PCM declaration; packet metadata fields are zero | None |
| Native-packet passthrough | Compatible codec, PCM geometry, duration, packet size limit, and boundary contract | None |
| Decode to PCM | PCM output and decoder accepts source codec/framing and target PCM | Decoder |
| PCM encode | Packet output and encoder accepts the output profile | Encoder |
| Decode then encode | Both providers accept the same PCM geometry | Decoder and encoder |

Intermediate PCM must preserve sample_rate_hz, channels, and sample exactly.
Unsupported providers, geometry mismatch, unsupported container/framing changes,
or required packet reframing return a capability error instead of silently
converting the media.

Identical codecs do not automatically mean passthrough. MP3 bytes may pass to
an MP3 sink only when codec, framing, geometry, and representation contracts
all match. A byte stream is never implicitly packetized. MP3-to-Opus exists
only when the MP3 decoder, target PCM, and Opus encoder all support the fixed
profile.

Further route limits:

- Native byte/packet passthrough cannot seek or rewind.
- Rewind on a processing route closes source/decoder/encoder state. A previous
  generation must fully exit before a new generation uses the state.
- Cancellation never flushes a codec tail. Natural EOF is the only path that
  performs decoder flush, encoder finish, and Service/Engine drain.

## 5. Sessions, Concurrency, Events, and Progress

Service acceptance is not audible playback. STARTED means media processing has
begun. FINISHED is emitted only after Pipeline input completion and Engine
drain confirmation. CANCELLED, PREEMPTED, and ERROR are terminal states, and
every terminal operation still requires explicit release.

### 5.1 Operation State and Command Legality

The diagram uses the public QM_AUDIO_CONTROL_* state names. NEW is an internal
allocation state; qm_audio_control_play() exposes an operation only after its
request has entered the admission path.

~~~mermaid
stateDiagram-v2
    [*] --> WAITING: play accepted
    WAITING --> PLAYING: Service admission and STARTED
    WAITING --> CANCELLED: stop or admission timeout
    WAITING --> ERROR: admission failure
    PLAYING --> PAUSED: pause confirmed
    PAUSED --> PLAYING: resume after admission
    PLAYING --> DRAINING: Pipeline reaches natural EOF
    DRAINING --> FINISHED: Engine drain confirmed
    PLAYING --> CANCELLED: stop
    PAUSED --> CANCELLED: stop
    DRAINING --> CANCELLED: cancel
    PLAYING --> PREEMPTED: Service preemption
    PAUSED --> PREEMPTED: Service preemption
    PLAYING --> ERROR: source, provider, or device failure
    PAUSED --> ERROR: resume failure
    DRAINING --> ERROR: drain failure
    FINISHED --> [*]: release
    CANCELLED --> [*]: release
    PREEMPTED --> [*]: release
    ERROR --> [*]: release
~~~

| API | Required state or precondition | Successful result |
| --- | --- | --- |
| qm_audio_control_play() | Live Control and valid source/profile declaration | Returns an operation ID; admission then determines WAITING or PLAYING |
| qm_audio_control_stream_write() | Live PUSH operation; byte-stream or PCM contract | Copies accepted bytes subject to bounded backpressure |
| qm_audio_control_stream_write_packet() | Live PUSH + PACKET operation with matching native profile | Atomically copies one complete packet; an error retains no partial packet |
| qm_audio_control_stream_finish() | Live PUSH operation after the final media unit | Marks input EOF; FINISHED still waits for Pipeline and device drain |
| pause/resume/seek/stop | An operation and a route/source that supports the requested action | Emits a later state/event result; unsupported preservation or seek returns -QM_ENOSYS |
| wait()/get_snapshot() | Known operation ID | Observes state without changing ownership |
| release() | Terminal operation only | Quiesces and invalidates the operation ID |

### 5.2 Admission and Scheduling

| Request policy | Service decision |
| --- | --- |
| QUEUE | Starts immediately when the route is free; otherwise enters the bounded queue. A zero admission timeout rejects waiting when busy, QM_WAIT_FOREVER has no expiry, and a finite deadline becomes CANCELLED on expiry. |
| REJECT_IF_BUSY | Rejects immediately when another operation owns the output route. |
| PREEMPT_LOWER | Requests preemption only when the new operation has higher Service priority than the active operation. |
| PREEMPT_CURRENT | Requests preemption regardless of relative priority, subject to safe release of the active route. |
| Multiple queued operations | Selects the highest priority first; equal priorities retain first-in-first-out order. Expired requests are resolved before later admission. |

Preemption never writes new media until the previous output owner has confirmed
stopping. Service priority is a session-admission policy, not an RTOS task
priority. If safe preemption stop fails, Service reports ERROR to both affected
operations; each remains owned until its terminal cleanup succeeds.

Each preemption dependency belongs to a specific close action. A failed action
affects only requests depending on it. A new preemption admitted during error
notification belongs to a new action and does not inherit the previous failure.

Synchronous Service APIs and the runtime asynchronous path share one mechanism:
action registration, nonblocking advancement, and state commit. `audio_event`
advances it at runtime. External synchronous callers wait for per-session Engine
progress notifications, recheck state, and advance the same core; synchronous
cleanup can therefore finish after the event task exits. The notification means
recheck, not completion. This mechanism adds no task or action queue.

An action holds an internal session lifetime reference (action pin) until state
commit and delivery of its notice complete. `release_pending` rejects new
external actions while allowing private STOP cleanup to continue. A wait timeout
retains the session, pending action, and required references; callers must keep
the handle for retry. Terminal state alone does not prove notice delivery or
permit storage reclamation.

Public synchronous Service APIs remain available. Synchronous
`qm_audio_service_session_finish()` still confirms physical drain before
advancing FINISH; the Control path still submits FINISH after Source worker
drain. Execution failure of an admitted resume commits ERROR and publishes an
error notice in both paths; the synchronous call also returns the execution
error.

### 5.3 Normal Completion Sequence

~~~mermaid
sequenceDiagram
    participant App as Application
    participant Control
    participant Pipeline
    participant Service
    participant Engine
    participant Adapter as Device adapter

    App->>Control: play(request)
    Control->>Pipeline: validate source and build_route()
    Control->>Service: submit session
    Service-->>Control: ACCEPTED
    Service->>Pipeline: output ownership granted
    Pipeline->>Engine: PCM or native media
    Engine->>Adapter: render/write
    Pipeline-->>Control: persist natural EOF
    Pipeline->>Engine: drain (Source worker waits)
    Engine-->>Pipeline: drain confirmed
    Pipeline-->>Control: persist output completion
    Control->>Service: register and advance FINISH (drain confirmed)
    Service->>Engine: stop / close and release output authorization
    Service-->>Control: FINISHED
    Control-->>App: event, snapshot, or wait result
~~~

Each Control has an isolated operation table and event subscription. Controls
share one Service. Service never writes a preempting session until the old
output confirms stopping. Pipeline and callbacks use operation/generation
boundaries so a stale terminal event cannot affect a newer run.

Preserving pause and cancellation are intentionally different:

- pause succeeds only when the device and source can retain context. Unsupported
  pause returns -QM_ENOSYS and must not pretend to be stop/replay.
- QM_AUDIO_PAUSE_YIELD_OUTPUT releases output ownership and discards submitted
  media. Seekable processing sources can rebuild on resume; non-seekable live
  sources can only continue forward.
- stop/cancel interrupts input and queued media. A device adapter may have a
  bounded tail or drain completion path; callers must not assume immediate
  silence unless that adapter explicitly guarantees it.

Check qm_audio_control_snapshot_t flags before using position or duration. PCM
and packets with known duration can accumulate media time. Opaque byte streams
or packets with unknown duration cannot fabricate a position. A device without
position capability returns -QM_ENOSYS.

### 5.4 Failure, Timeout, and Cleanup Rules

| Condition | Framework fact | Required application behavior |
| --- | --- | --- |
| Unsupported source, profile, route, or operation | The call fails explicitly; qm_audio does not silently convert or emulate it | Select a declared compatible route or surface the capability error |
| Admission deadline expires | The queued operation becomes CANCELLED with a timeout result | Release the terminal operation; distinguish this from a caller wait timeout |
| wait() expires | The wait ends but does not cancel or release the operation | Query the snapshot or wait again; decide cancellation separately |
| I/O, drain, unsubscribe, shutdown, or release times out | A timeout does not prove callbacks, DMA, or backend access have stopped | Retain the handle and its borrowed context, then retry the documented cleanup operation |
| Terminal event arrives late | Generation filtering prevents an older run from changing a newer run | Correlate business work by operation ID; do not treat a callback as ownership transfer |

For PUSH, finish is the only normal end-of-input signal. Empty packets, empty
writes, or temporary source starvation do not signal EOF. Event callbacks and
Capture callbacks borrow their payloads; copy data before queuing it to another
task, and avoid blocking lifecycle work in the callback.

### 5.5 Callback and Threading Restrictions

| Context | Safe short work | Must not do synchronously |
| --- | --- | --- |
| Control event callback | Copy event fields, record the operation ID, schedule application work; PUSH write uses a zero timeout | play, pause, resume, seek, stop, wait, release, shutdown, destroy, unsubscribe, or other blocking lifecycle work |
| Capture CALLBACK delivery | Copy media and schedule transport work | recv/wait, unsubscribe, shutdown, destroy, or blocking I/O |
| Custom source callback | Perform bounded open/read/close work on the shared source worker | Retain caller data after its documented lifetime or wait indefinitely |
| Device adapter I/O | Run in task context with bounded I/O and abort support | Run in an ISR or take a lock held by the blocked I/O that abort must wake |

### 5.6 Event Delivery and Backpressure

Commands, lifecycle facts, and Control state notifications enter
`audio_event` through the instance event queue. Its capacity is configured by
`CONFIG_QM_AUDIO_CONTROL_EVENT_QUEUE_LEN` (default 16). Producers perform one
nonblocking queue submission; they do not reserve a slot or retry a full queue.
Accepted synchronous commands retain caller storage until completion. The queue
carries control events; media bytes remain in bounded source, render, or Capture
rings and are never copied into the event queue.

The unified event boundary is limited to control-plane work:

| Uses the instance event queue | Remains in its owning worker/ring or wait primitive |
| --- | --- |
| Commands, lifecycle facts, Control state snapshots, and EXIT barriers | Device read/write, codec execution, media payloads, independent Capture delivery, cancellation that must interrupt blocked I/O, and callback/I/O quiescence waits |

Capture's pending state, callback pins and semaphore wakes belong to its private
dispatcher and unsubscribe barrier; they never enter this instance event queue.

Pipeline facts are stored in fixed slots inside the operation, one per event type, and consumed in run-sequence order. Repeated nonterminal facts may coalesce; EOF, output completion and failure remain distinct. Older generations are rejected. Queue items only wake the consumer. A full queue already guarantees another scan, so terminal facts persist without blocking the Source worker.

Service facts raised on the event task use a bounded FIFO and are projected before command completion. External cleanup uses the same Service ingress. Public Control lifecycle callbacks run on the event task. Public notifications from other tasks use the instance queue. Shutdown closes post admission, waits for accepted posts, and then consumes the EXIT barrier.

Capture state notifications use its private dispatcher. Intermediate nonterminal
states may coalesce, while terminal STOPPED or ERROR remains queryable. A slow
Capture callback delays other Capture callbacks; Control event dispatch remains
independent. Keep all application callbacks short.

### 5.7 Time Domains

| Value | Origin and validity | Rule |
| --- | --- | --- |
| qm_audio_event_t.timestamp_ms | Audio monotonic event clock | Correlates event order; it is not media PTS |
| `qm_audio_data_t` timestamp_ms/duration_ms | Valid only with the corresponding `QM_AUDIO_DATA_TIMESTAMP_VALID` or `QM_AUDIO_DATA_DURATION_VALID` flag; PCM and byte-stream paths normally leave them zero | Preserve producer semantics; capture timestamps use the backend capture clock and are not render presentation time |
| qm_audio_control_snapshot_t position/duration | Valid only when the corresponding progress flag is set; position is measured before DAC and may be estimated | Do not derive UI position from a zero value or assume audible output |

Never infer time validity from a numeric zero. Cross-domain synchronization,
capture/render clock alignment, and end-to-end latency require an explicit
product contract beyond the current framework.

## 6. Capture Contract

Capture is an independent component using only its public media types, QUMI
utilities and `qm_audio_device_capture_*`. Its caller supplies an already open
device, native input format and buffer limits. Capture never opens/closes the
shared device and never accesses Engine, Service, Pipeline or `audio_event`.
Engine owns an opaque Capture handle and shuts it down before closing the device.

The first enabled subscriber starts physical capture synchronously in the
calling task; subscribe/enable returns the actual start result. After successful
start, PULL creates one `audio_capture` reader on demand. On exit the reader stops
the device, clears its active flag and signals waiters under the lock, then never
accesses Capture again. PUSH uses the device producer's task and the singleton
`qm_audio_capture_feed(input, result)` ingress. In PUSH builds one Capture
reserves that ingress from create through destroy. Both modes copy native data
into independent bounded subscription queues. `audio_cap_cb` rotates between
ready subscriptions and serializes state/media callbacks outside locks. QUEUE
consumers run in their own tasks. Create starts neither reader nor callback tasks.

Subscribe, enable, shutdown and unsubscribe claim lifecycle ownership through a
flag protected by the existing data lock; conflicting calls return `-QM_EBUSY`.
Device calls and quiescence waits run outside that lock.

PUSH production starts through device `capture_start` after Capture creation.
Device `capture_stop`, including after a failed start, must wait for all
in-flight feeds and prevent further calls from the old producer before returning
success. Feed must not overlap
create/destroy. This keeps the ingress pointer stable while QMOS mutexes and
semaphores protect instance data and task completion; no compiler atomic
builtins or persistent global lock are needed.

Capture publishes stack metadata and the borrowed source payload as two
segments of one copied ring queue record; enqueue needs no temporary payload
allocation or concatenation buffer. Both ranges remain valid until the call
returns. QUEUE receive uses a temporary buffer under the data lock, while the
dispatcher keeps a separate buffer stable until a callback returns. Each
subscription owns only its independent bounded media queue.

No CaptureRoute is required. For PCM-to-Opus, a consumer reads a PCM QUEUE
subscription and owns `qm_audio_encoder_open/push/finish/close`. Resampling is
needed only when source and encoder formats differ; Capture never converts data.

Default-instance consumers call
`qm_audio_capture_subscribe(qm_audio_get_capture(), &config, &subscription)`.
The returned Capture handle is borrowed; serialize default init/deinit against
lookups and handle access. Independent callers use
`qm_audio_capture_create(&open_config, &capture)` after opening their device.

| Capture media | Adapter read API | Upper-layer delivery |
| --- | --- | --- |
| PCM | qm_audio_device_capture_read() | PCM block aligned to complete sample frames |
| Native encoded media | `qm_audio_device_capture_read()` with the fixed encoded input profile | One `qm_audio_data_t` carrying a complete packet |

QUEUE subscribers use `qm_audio_capture_recv(subscription, frame, timeout_ms)` for
both media types. Before the call, `frame->data` points to caller-owned writable
storage and `frame->size` is its capacity. On success `frame->size` becomes the
payload length and Capture fills the metadata fields. The Capture input format
determines how to interpret the data; insufficient capacity leaves the head record
queued, and encoded packets are never split.

Before allocating a caller buffer, use
`qm_audio_capture_peek_size(subscription, &size)` to inspect the current head payload
size without waiting. An empty queue returns `-QM_EEMPTY`. The call does not consume
or reserve the head record, so serialize it with the following `recv()` and still
handle a capacity error if queue overwrite changes the head between the two calls.
`size` excludes Capture's private metadata.

Queue corruption detected by enqueue, oldest-record discard, peek, recv or callback delivery clears and terminates
only the affected subscription with `ERROR(-QM_EIO)`. Insufficient caller capacity
still returns `-QM_EINVAL` and retains the head item. `data_ready` signals an empty
queue becoming nonempty or a state change; it does not count media records.

Capture does not support encoded byte-stream input. STOPPED or ERROR
makes a subscription terminal; unsubscribe and create a new one rather than
only calling enable. Callback payloads and packet data are borrowed, so an
asynchronous transport must copy them before returning.

Capture state and media callbacks are dispatched by the same private `audio_cap_cb` task
and are not concurrent with one another; either may run before
`capture_subscribe()` returns. Initialize and retain callback context before
subscribing, and move longer work to an application-owned task.

Source failure makes both RUNNING and DISABLED subscriptions ERROR. A new enabled
subscription can restart capture; existing terminal subscriptions retain their
first error, including through shutdown. Capture callbacks may query
state or flush, but cannot subscribe, enable, receive or perform lifecycle calls.
Shutdown and unsubscribe share a deadline across their waits; synchronous device
stop additionally follows the adapter's finite stop bound. Failed stop retains the
handle, device and pending cleanup for retry. Shutdown waits for in-flight callbacks
but need not deliver pending terminal callbacks; snapshots remain until unsubscribe.
It wakes blocked receivers without waiting for `recv()` to return. Unsubscribe
waits for that subscription's receiver and callbacks before releasing their storage.

### 6.1 Capture Subscription Control

| Operation | Scope and effect | Required caller rule |
| --- | --- | --- |
| `capture_enable(false)` | Disables only this subscription, clears its pending queue, and wakes a blocked `recv()` with `-QM_EBUSY`; it does not stop the physical reader or other subscriptions. | Keep callback context until unsubscribe; disabling is not a callback-quiescence barrier. |
| `capture_enable(true)` | Enables delivery and preserves queued media on repeated calls; when source start is needed, returns its synchronous result. STOPPED and ERROR are terminal until unsubscribe. | Inspect the returned error and snapshot; clean up and recreate terminal subscriptions. |
| `capture_flush()` | Clears only this subscription's queued blocks; physical Capture and an in-flight callback are unaffected. Sequence numbers are not renumbered. | Use it to discard backlog, not to reset the physical Capture clock or loss accounting. |
| `capture_peek_size()` | Reports the current head payload size without waiting or consuming it; an empty queue returns `-QM_EEMPTY`. | Serialize it with the following `recv()`; the query does not reserve the head against overwrite. |
| `capture_recv()` | Copies one complete native PCM block or encoded packet into `frame->data`; `frame->size` is capacity on entry and the actual length on success. Only one QUEUE consumer may receive at a time; insufficient capacity does not consume the head item. CALLBACK mode cannot use `recv()`. | Serialize receives and retain the destination buffer for the duration of the call. |
| `capture_unsubscribe()` | Stops this delivery, wakes waiters, and waits for receiver/callback quiescence. The final subscription also stops the physical reader. | Do not call from Capture's reader or dispatcher; retry the same handle after timeout. |

### 6.2 Capture Lifecycle

~~~mermaid
sequenceDiagram
    participant App as Application
    participant Capture
    participant Adapter as Device adapter
    participant Queue as Subscription queue
    participant Consumer as QUEUE or CALLBACK consumer

    App->>Capture: create(open device, native format)
    App->>Capture: subscribe(capture, config)
    Capture->>Adapter: synchronous start on first enabled subscriber
    Adapter-->>Capture: start result
    Capture-->>App: publish subscription or return start error
    Adapter-->>Capture: PULL read or PUSH feed
    Capture->>Queue: copy native block or complete packet
    Queue-->>Consumer: recv() or callback
    App->>Capture: unsubscribe(subscription)
    Capture->>Queue: stop delivery, wait for receiver/callback
    Capture->>Adapter: final unsubscribe aborts; PULL reader stops then exits, PUSH stop waits for feed
    Capture-->>App: subscription released
    App->>Capture: shutdown(capture), destroy(capture)
    App->>Adapter: close device after successful shutdown
~~~

### 6.3 Capacity and Backpressure Budget

qm_audio uses bounded resources; a product must select values from its memory,
latency, and concurrency budget rather than rely on defaults.

| Budget | Primary configuration | Product decision and acceptance condition |
| --- | --- | --- |
| Retained Service sessions | CONFIG_QM_AUDIO_SERVICE_MAX_SESSIONS | Maximum queued, active, paused, and terminal-but-unreleased operations |
| Source execution | One fixed worker | Session capacity bounds registered Pipelines; paused context does not add a task |
| Per-Control operations | CONFIG_QM_AUDIO_CONTROL_MAX_OPERATIONS | Maximum business operations before explicit terminal release is required |
| PUSH ingress | CONFIG_QM_AUDIO_CONTROL_PUSH_BUFFER_BYTES and write timeout | Producer behavior on bounded-backpressure timeout or interruption |
| Render queue | CONFIG_QM_AUDIO_ENGINE_RENDER_QUEUE_LEN, CONFIG_QM_AUDIO_ENGINE_RENDER_BLOCK_BYTES and write timeout | PCM/byte input accepts bounded chunks; atomic encoded packets use the profile's max_packet_bytes |
| Capture fan-out | CONFIG_QM_AUDIO_CAPTURE_QUEUE_LEN and CONFIG_QM_AUDIO_CAPTURE_MAX_SUBSCRIBERS | Per-subscriber latency; a full queue drops the oldest item and sequence gaps expose loss |
| Event delivery | CONFIG_QM_AUDIO_CONTROL_EVENT_QUEUE_LEN and QM_AUDIO_EVENT_PENDING_MAX | Callback return-time budget; load-test proof that application callbacks do not delay commands, admission, or event projection |
| Optional cloud adapter | CONFIG_QM_AUDIO_CLOUD_QUEUE_LEN and CONFIG_QM_AUDIO_CLOUD_MAX_OPERATIONS | Include cloud ingress queue, `audio_cloud` stack, and operation-map capacity only when the adapter is initialized |

All timeout, queue-length, task-priority, and stack-size choices need an
integration-owned rationale and a load test. A slow Capture subscriber must not
block physical capture; a slow PUSH producer must not assume an unbounded queue.
The task count in section 2.1 and the stack size of every included task belong
in the same memory budget; retained Service sessions do not themselves add a
worker task.

A conservative planning estimate, excluding allocator and adapter-private
overhead, is:

~~~text
capture record payload   = capture_frame_bytes + 4 * sizeof(uint32_t)
per Capture subscription ~= queue_len * (QM_RING_QUEUE_RECORD_HEADER_BYTES + capture record payload)
                           + subscription/OS objects
per Capture             += temporary QUEUE receive buffer + lazy callback buffer + Capture/OS objects
total instance budget   += PUSH rings + render/event queues + source worker
                           + Capture dispatcher and active PULL reader + decoder/encoder private state
                           + application copies
~~~

Calculate the target-memory budget from the platform allocator, stack
watermark, and maximum concurrent subscription count. This formula is a
planning lower bound; it does not define an allocation ABI.

Render media storage is bounded by `(queue_len + 1) × (item_header + payload_limit)`, including the item held by the writer. PCM/byte streams use `RENDER_BLOCK_BYTES`; atomic packets use `max_packet_bytes`. Include per-operation fact slots, output leases and task stacks separately. Retained revoked leases do not retain emptied media queues.

## 7. Platform Adapter Contract

This section defines the qm_audio-to-adapter interface contract. Chip, audio
IC, module firmware, transport-bus, and board-profile details belong in the
platform adapter documentation and its tests.

### 7.1 Device Adapter

A device adapter must:

- After open(), publish only the fixed input/output profile it can service
  through qm_audio_device_get_caps(). Engine compares it with the initialization
  configuration; a mismatch fails initialization and follows close/retry cleanup
  rather than changing route selection.
- Keep playback and capture directions explicit. Enabling both directions does not
  by itself guarantee duplex behavior.
- Accept `qm_audio_data_t` through the unified playback/capture device APIs;
  the fixed profile selects PCM blocks or complete encoded packets. Validate
  the declared profile and explicitly reject unsupported media.
- Declare or reject playback, capture, drain, pause/resume, volume, mute, and
  position operations according to the adapter's real capability.
- Preserve device-specific error and tail/drain behavior without reinterpreting
  encoded data as PCM.

### 7.2 Codec Provider

Decoder and encoder providers are optional platform implementations. Pipeline
selects one only when its declared input/output combination preserves the fixed
PCM geometry:

- A decoder accepts a declared codec/framing and produces the negotiated PCM
  profile.
- An encoder accepts that exact PCM profile and produces the configured encoded
  packet profile.
- A provider may reject every unsupported codec, framing, sample geometry,
  packet duration, or packet size. qm_audio does not supply a fallback
  resampler, channel converter, packet reframer, parser, or container handler.

The core calls the encoder synchronously for each stream. A decoder may emit
from a provider-owned worker, but that worker, DMA, or SDK callback is outside
the core task count; the provider contract must keep its context valid through
stop and close and document its quiescence barrier.

### 7.3 Platform-Specific Documentation and Validation

Each platform adapter owns its own profile matrix, Kconfig choices, external
firmware dependencies, bus/pin requirements, target build instructions, and
board-test evidence. Framework host tests validate the common contract only;
they do not prove a particular adapter's target-media path.

### 7.4 Adapter Admission Checklist

| Area | Required adapter evidence |
| --- | --- |
| Link boundary | Exactly one implementation of each enabled named adapter interface is linked; no vendor SDK type crosses the public adapter header |
| Open and capabilities | open() copies needed configuration, get_caps() reports the actual fixed profiles and flags, and Engine rejects a mismatch |
| Playback and Capture | Each enabled direction accepts only its declared media representation and preserves caller-owned buffer lifetime |
| Cancellation and drain | Abort/stop wakes bounded I/O safely; close/free succeeds only after backend access and callbacks are quiescent |
| Codec provider | check_format() rejects unsupported combinations before allocation; open/push/finish/close preserve the fixed PCM geometry |
| Negative tests | Unsupported profile, bad packet, queue pressure, I/O timeout, cancellation, and cleanup retry are covered |

### 7.5 Adapter Call Sequence

| Path | Required order |
| --- | --- |
| Playback normal completion | open -> get_caps -> playback_start -> playback_write -> playback_drain -> playback_stop -> close |
| Playback cancellation | open -> get_caps -> playback_start -> playback_write -> playback_abort -> playback_stop -> close |
| Capture | open -> get_caps -> capture_start -> capture_read -> capture_abort -> capture_stop -> close |
| Codec provider | check_format -> open -> push* -> finish -> close for natural EOF, or check_format -> open -> push* -> close for cancellation |

get_caps() is valid only after open(). The render worker serializes backend
start/write/pause/resume/stop, position queries and shared output settings.
Drain reserves exclusive playback access on its caller, while abort is issued
from the stop caller to wake the matching blocked write/drain. Abort must not
wait for a lock held by that I/O. Capture and playback may run together only
when the adapter explicitly supports that combination. Close succeeds only
after all backend access and callbacks are quiescent.

## 8. Application and Protocol Adapter Boundary

An application or protocol adapter queries native stream profiles from the
initialized `qm_audio` instance, borrows a business-owned Control, and writes
downlink audio into one PUSH operation. It does not create an Engine, Service,
or physical device. An encoded format cannot be assumed to be PCM: encoded byte
streams, complete packets, and PCM use different write APIs.

An optional protocol adapter parses external commands, keeps a bounded external
request-ID to operation-ID mapping, and calls Control. Network sends run in the
adapter's own context, never under an audio lock or media/device callback.
Protocol, authentication, resource catalogs, and business retries remain outside
the four core layers.

### 8.1 Optional Voice Frontend Boundary

The voice frontend is an application-managed platform boundary, not an
automatically started qm_audio layer. The application calls
`qm_audio_frontend_open()` and `qm_audio_frontend_close()` and changes only the
wakeword mode through `qm_audio_frontend_set_mode()`.

The processed microphone path is separate from the frontend control API. The
platform implementation may feed AEC-processed PCM through
`qm_audio_capture_feed()`; the application receives that data through
the independent Capture subscriptions. PUSH feed does not create a reader
task, and uplink gating remains the Capture consumer's decision.

When the frontend detects a wake word, the producer copies the metadata and
calls `qm_audio_report_wake(&wake)`. qm_audio copies the metadata and projects
`QM_AUDIO_EVENT_WAKE` on `audio_event`; applications receive it through
`qm_audio_wake_subscribe()`, and the report function cannot be called from the
event task. Frontend-owned workers, DMA, and vendor callbacks
are outside the core task count and must be quiescent before frontend close.
Stop the frontend feed before removing the dependent Capture subscriptions;
return to wakeword mode before close when the implementation requires it.

### 8.2 Optional Cloud Adapter

`CONFIG_QM_AUDIO_CLOUD_SUPPORT` only includes the module; it does not initialize
one. After Engine, Service, Control, and the underlying cloud handle are ready,
the product calls `qm_audio_cloud_init()`. The adapter borrows the supplied
Control and owns one `audio_cloud` task, a bounded command queue, and the
cloud-created operation mapping.

The protocol receive callback only copies the external payload and enqueues it.
Command decoding, authorization, resource resolution, Control calls, status
projection, and terminal-operation cleanup run on `audio_cloud`. Resolver strings
are borrowed for the callback duration; a product retaining them must copy them.
The external message ID is a reply key, not a Control operation ID.
The resolver-provided release callback runs on `audio_cloud` after the related
operation is quiescent and is called exactly once; it must not reenter that
operation's lifecycle APIs.

Shutdown order is fixed: stop/unregister cloud ingress, call
`qm_audio_cloud_shutdown(timeout_ms)`, retry the same handle after a timeout,
then call `qm_audio_cloud_destroy()`. Cloud shutdown releases cloud-owned
operations but never shuts down the borrowed Control; therefore it must complete
before Control shutdown and `qm_audio_deinit()`.

## 9. Configuration, Build, and Validation

The main Kconfig options are:

| Option | Meaning |
| --- | --- |
| CONFIG_QM_AUDIO_SUPPORT | Control, Service, Pipeline, and Engine |
| CONFIG_QM_AUDIO_DECODER_SUPPORT | Platform decoder provider |
| CONFIG_QM_AUDIO_ENCODER_SUPPORT | PCM-to-encoded-packet provider |
| CONFIG_QM_AUDIO_SOURCE_FILE_SUPPORT | Built-in FILE source |
| CONFIG_QM_AUDIO_SOURCE_URI_SUPPORT | Built-in HTTP/HTTPS URI source |
| CONFIG_QM_AUDIO_CAPTURE_SUPPORT | Independent native Capture and subscriptions |
| CONFIG_QM_AUDIO_ENGINE_RENDER_BLOCK_BYTES | Maximum payload of a PCM/byte-stream queue item; larger writes accept a frame-aligned prefix |
| CONFIG_QM_AUDIO_CLOUD_SUPPORT | Optional cloud adapter module; explicit `qm_audio_cloud_init()` is still required |

The current `components/qm_audio/tests/CMakeLists.txt` registers
`test_capture_push`, `test_capture_pull`, and `test_engine`. The Engine test
also links Service directly to cover ownership, action transitions, cleanup
retry, shutdown during concurrent release, and preemption dependency identity.
The `components/qm_agent/tests` target `test_agent_audio_integration` links the
real Control, Service, Pipeline, Engine, and Capture implementations. It covers
default-instance lifecycle, PCM PUSH playback, pause/yield/resume, multi-Control
preemption, and Agent terminal cleanup. Other Source/profile/provider combinations
and optional cloud runtime integration still need acceptance coverage. Platform
adapters own their separate contract and board tests.

Run these host tests from the `qumios-dev` repository root:

~~~sh
cmake -S components/qm_audio/tests -B /private/tmp/qm-audio-host-tests
cmake --build /private/tmp/qm-audio-host-tests
ctest --test-dir /private/tmp/qm-audio-host-tests --output-on-failure

cmake -S components/qm_agent/tests -B /private/tmp/qm-agent-host-tests
cmake --build /private/tmp/qm-agent-host-tests
ctest --test-dir /private/tmp/qm-agent-host-tests --output-on-failure
~~~

### 9.1 Acceptance Matrix

| Scope | Required evidence | Minimum cases |
| --- | --- | --- |
| Framework | Host unit/contract tests | Lifecycle, route selection, packet validation, preemption, cancellation, drain, and Capture isolation |
| Device and codec adapter | Adapter-local contract tests | Profile mismatch, unsupported operation, bounded I/O, abort, drain, close retry, and provider-format rejection |
| Product integration | Target firmware build plus configured route matrix | Every enabled source/profile/priority policy and all intended PUSH completion paths |
| Hardware | Board test record | Initialization, real media I/O, capture and render interaction, interruption latency, drain, and recovery after device error |
| Load and fault handling | Reproducible stress/fault report | Queue pressure, slow subscriber, slow producer, source timeout, callback delay, and repeated start/stop |

### 9.2 Requirement-to-Evidence Trace

| Invariant | Current host evidence | Still needed before product release |
| --- | --- | --- |
| Capture queue isolation and lifetime | `test_capture_push`, `test_capture_pull` | Target capture data, loss behavior, and callback load |
| Engine ownership, worker serialization, stop/drain, and cleanup retry | `test_engine` | Platform interruption bounds and real device drain/stop behavior |
| PCM prefix metadata and atomic packet submission | `test_engine` | Every enabled device profile and media path |
| Service-owned open, action execution, and session cleanup | `test_engine`: failed start, cleanup retry, concurrent release/shutdown, and preemption from notice callbacks | Scheduling under load and target interruption bounds |
| Default-instance and Control operation lifecycle, events, PUSH, and terminal release | `test_agent_audio_integration`: PCM PUSH, pause/yield/resume, multi-Control arbitration, and Agent cleanup | Other source/framing combinations, target timing, and device behavior |
| Pipeline routing, provider rejection, Source cancellation, and stale output rejection | PCM/PUSH integration in `test_agent_audio_integration`; other route cases still needed | Enabled profile/provider combinations, open/read interruption, session capacity, and memory pressure |
| Completion with blocked callbacks and route handover before release | Selected fault/notice cases in `test_engine` and blocked-observer cleanup in `test_agent_audio_integration` | Broader timeout coverage, target callback load, and repeated preemption |
| Optional cloud and protocol integration | Host acceptance tests still needed | Adapter ordering plus real protocol, network, and authentication evidence |

Recommended validation order:

1. Build and run ctest for components/qm_audio/tests.
2. Add and run the missing host acceptance tests above, plus the relevant qm_agent integration tests.
3. Build and run the enabled platform adapter contract tests.
4. Build the intended firmware target.
5. On hardware, verify device initialization, the negotiated media profile,
   I/O, drain, interruption, timing, and cloud media transport.

The first four steps validate compilation, host contracts, and firmware linkage.
Only the fifth validates the actual device, module firmware, wiring, and media
chain.

## 10. Delivery Readiness

Before a product declares an audio route usable, it must record the following
decisions beside its integration code. This is a delivery checklist, not a claim
that every possible route is enabled by qm_audio.

| Deliverable | Must be explicit |
| --- | --- |
| Media contract | Input/output profiles, source declaration, packet boundary mode, and intended passthrough or provider route |
| Scheduling policy | Priority, busy policy, admission deadline, pause/yield behavior, and terminal-event owner |
| Resource budget | Session/operation/worker/queue limits, timeouts, task priorities, and worst-case memory/latency budget |
| Ownership plan | Who retains source data, callback context, operation IDs, subscriptions, and cleanup retry handles |
| Recovery plan | Behavior for -QM_ENOSYS, timeout, queue pressure, preemption, source failure, and device failure |
| Verification record | Passing framework, adapter, target-build, hardware, and stress/fault evidence from the acceptance matrix |

### 10.1 Product Route Delivery Record

Each route enabled by a product must have a version-controlled record beside
its integration code. It is not a runtime configuration format. It is the
minimum release-review contract for what that route actually promises:

| Record field | Must state |
| --- | --- |
| route_id and version | Stable route identity and the contract version released with it |
| Input declaration | Source kind, codec, framing, PCM geometry, byte-stream or packet boundary, and EOF method |
| Fixed output profile | Engine input/output profiles and the capability actually published by the device adapter |
| Planning result | Passthrough, decode, encode, or decode-then-encode; required providers and the expected error when unavailable |
| Scheduling and backpressure | Priority, busy policy, admission deadline, PUSH/queue timeout, and the business owner of retries |
| Resource and latency | Session, operation, worker, queue, and stack limits plus acceptable queueing/media-latency budget |
| Completion, failure, and cleanup | Terminal behavior and handle owner/retry path for finish, drain, cancel, preemption, timeout, and device error |
| Observability | Business-consumed events, snapshot fields, sequence gaps, or product metrics; an unimplemented metric must not be claimed |
| Evidence and compatibility | Host/adapter/target-build/hardware evidence, and compatibility, rollback, or upgrade-rejection policy for a public-contract change |

### 10.2 Change Implementation Gates

Add or change a route in the following order. Do not enter a later gate without
explicit evidence for the preceding one:

1. **Contract gate**: define source, fixed sink, packet boundary, ownership, and unsupported-case errors. A public-contract change updates both design documents, header comments, and its compatibility policy together.
2. **Route gate**: implement or link the required providers, then cover successful selection and every rejection path. Do not make a test pass through implicit resampling, packet reframing, or format guessing.
3. **Lifecycle gate**: cover admission, preemption, cancellation, drain, timeout, stale events, and cleanup retry. Resource limits, callback budget, and load results must be reviewable.
4. **Product-release gate**: bind the route record to the actual Kconfig/link configuration. Open the route to the product only after target build, adapter-contract, hardware, and stress/fault acceptance pass.

## 11. Open Decisions and Unimplemented Extensions

The following capabilities are outside the current implementation scope. A
product requirement needs the corresponding interface and acceptance design:

| Item | Current boundary | Decision needed for a product requirement |
| --- | --- | --- |
| Realtime playback delivery | QM_AUDIO_DELIVERY_REALTIME exists as a type, but Control currently uses reliable delivery | Expose a product-facing policy and define loss/latency behavior, or keep realtime delivery unsupported |
| Capture observability | Capture sequence gaps reveal loss, but no public dropped-count, queue-depth, or high-watermark metric exists | Add a statistics/snapshot API and alarm thresholds if field operations need diagnosis |
| Event reason | event.reason has no shared framework namespace | Keep it product-private, or define a versioned common reason enum before multiple integrations branch on it |
| Audible-start evidence | STARTED means processing began, not that a speaker produced the first audible sample | Add an optional adapter event/capability and board-level latency acceptance test if a start-audio SLA is required |

## 12. Documentation Maintenance

- When a public type, lifecycle, route condition, or generic adapter contract
  changes, update README.md and README_CN.md together.
- README.md and README_CN.md are the bilingual technical plans. Keep their
  section structure and public contracts aligned.
- Keep chip, module, bus, board-profile, and target-validation details beside
  the matching platform adapter, not in this framework design.
- Mark unimplemented capability as proposal or unsupported. Kconfig presence or
  protocol declaration is never sufficient to call it verified.
- Keep historical proposals in Git history rather than beside the current
  component interfaces as competing design baselines.
