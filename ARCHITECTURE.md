# PiNaplpsPlayer Architecture

PiNaplpsPlayer is an openFrameworks application designed to receive and render NAPLPS (North American Presentation Level Protocol Syntax) graphics. It operates in two primary modes: **Network Mode**, functioning as a WebSocket server to receive live streams of NAPLPS drawings, and **Slideshow Mode**, acting as a standalone file viewer and fallback when the network is idle.

## Core Components

### Application Lifecycle (`ofApp`)
- **`setup()`**: Initializes window settings, loads local `.nap` sample files, sets up an `ofFbo` for rendering, starts the WebSocket server listening on port 7112, and optionally starts a background thread for Tezos chain polling.
- **`update()`**: Safely pulls any new drawing frames off the incoming network queue (protected by a mutex) and pushes them to the decoder. Updates the `Telidon` renderer state.
- **`draw()`**: Caches the current state of the drawing by rendering the `Telidon` object to an `ofFbo`. The `ofFbo` is only updated while the drawing is in progress or when marked dirty, reducing GPU load. The cached `ofFbo` is then scaled and drawn to the screen through the image-effect shader (see Configuration), which runs every frame on the way out so the `ofFbo` itself stays a clean copy of the drawing. Also displays an overlay with current status, connection count, and hotkey information.

### NAPLPS Processing
The core graphics processing is handled by the `ofxNaplps` openFrameworks addon:
- **`Naplps` (Decoder)**: Parses raw NAPLPS byte streams or `.nap` files into an internal representation of drawing commands.
- **`Telidon` (Renderer)**: Takes the parsed commands from the decoder and performs the actual OpenGL drawing commands to render the graphics to the screen. It supports progressive drawing (animating the drawing process over time) and point labeling.

### Configuration (`bin/data/settings.xml`)
Read once in `setup()` via `ofxXmlSettings`, following the same convention as the other Pinopticon apps:
- **`slide_timeout`** (ms): how long the player waits without a Network Mode drawing before the Dead Man's Switch engages and activates Slideshow Mode. `0` disables the Slideshow Mode fallback.
- **`slide_interval`** (ms): how often Slideshow Mode swaps in a new random drawing.
- **`fbo_width`**: the width of the `ofFbo` used for caching the drawing.
- **`fbo_height`**: the height of the `ofFbo` used for caching the drawing.
- **`debug_view`**: if `1`, enables the debug overlay and point labels on startup.
- **`shader_name`**: the image effect applied when the `ofFbo` is drawn to the screen. `setup()` loads `bin/data/shaders/<name>_gl3` on the desktop GL 3.2 context (`_es3` under `TARGET_OPENGLES`, `_gl2` on the fixed pipeline). Fragment shaders read the drawing from `uniform sampler2DRect tex0`, in pixels. The `_es3` pairs are GLSL ES 1.00 instead (no `#version` line, `attribute`/`varying`, `gl_FragColor`), because `ofAppEGLWindow` only creates ES 2 contexts, and they read `uniform sampler2D tex0` in 0..1 coordinates, since ES has no rectangle textures. If the pair doesn't load, the drawing goes to the screen unprocessed.
- **`tezos_contract`**: the Tezos contract address to poll for on-chain NAPLPS drawings. Empty disables chain reads.
- **`tzkt_base`**: the TzKT indexer API base URL (e.g. `https://api.shadownet.tzkt.io/v1`).
- **`tezos_poll_seconds`**: how often the background thread polls TzKT (default `30`).
- **`tezos_max_bytes`**: maximum size of a single NAPLPS drawing from the chain (default `30000`).

### Dead Man's Switch (Slideshow Mode Fallback)
A player showing a stale drawing looks identical to a crashed one, so `ofApp::checkDeadMansSwitch()` runs every `update()` and watches the clock since the last Network Mode frame:
- After `slide_timeout` ms of silence, it enters Slideshow Mode (setting `slideshowActive`) and calls `loadRandomNap()`, which picks a random `.nap` from `bin/data` (never the one already on screen) and loads it.
- While Slideshow Mode is active, it reloads a new drawing every `slide_interval` ms, alternating between a random local `.nap` file and a drawing fetched from the Tezos chain cache (see below). If the chain cache is empty, all slides come from local files.
- The first Network Mode drawing to arrive clears `slideshowActive` and hands the screen back to Network Mode. Arrow keys and drag-and-drop do the same, so a deliberate choice gets a full timeout on screen before Slideshow Mode resumes.

`scanSamples()` builds the sample list by listing `bin/data` for `.nap` files rather than from a hardcoded list, so both the arrow keys and the switch pick up anything dropped into that directory.

### Network Layer
- **WebSocket Server**: Uses `ofxHTTP` (via the `Pinopticon_Http.hpp` wrapper) to run a WebSocket server on a dedicated thread. 
- **Message Parsing**: Frames can arrive in JSON format (with either raw text or base64 encoded payloads) or as raw NAPLPS streams. The application parses the incoming frames, extracts the NAPLPS data, and places it into a thread-safe incoming queue (`incomingMutex`).
- **External Integration**: Designed to receive streams pushed by an external server (e.g., `nap-xtz-server`).

### Tezos Chain Read
A background `std::thread` (`tezosThreadFunc`) polls the TzKT indexer for NAPLPS drawings stored on-chain in a Tezos smart contract:
- On each poll it queries the contract's big\_maps via `/contracts/{addr}/bigmaps`, then fetches the keys of every active big\_map. If no big\_maps yield data, it falls back to the contract's direct `/storage` endpoint.
- JSON values are walked recursively; substantial strings are collected as potential NAPLPS data. Hex-encoded strings (the Tezos `bytes` type) are decoded automatically.
- Successfully found drawings are stored in a thread-safe `tezosDrawings` cache (protected by `tezosMutex`), which the main thread's `loadChainNap()` picks from at random during Slideshow Mode.
- Poll failures (network errors, API errors, empty results) are logged once and silently skipped — the slideshow continues with local files. The thread sleeps in 1-second increments between polls so it can shut down promptly on `exit()`.

### Pinopticon Utilities
The `src/` directory includes several utility headers under the `Pinopticon` namespace, providing reusable network and utility wrappers:
- **`Pinopticon.hpp`**: General utilities (hostname resolution, timestamps, image/FBO to buffer conversion).
- **`Pinopticon_Http.hpp`**: Wrappers for setting up `ofxHTTP` MJPEG streams, POST servers, and WebSocket servers, as well as functions to broadcast data.
- **`Pinopticon_Osc.hpp`**: Wrappers for setting up and sending messages via `ofxOsc` (unused in this specific application).

## Data Flow
1. **Input**:
   - **Local File**: User drags and drops a `.nap` file or uses arrow keys to cycle through samples.
   - **Network**: WebSocket server receives a frame containing NAPLPS data.
   - **Tezos Chain**: Background thread fetches NAPLPS drawings from the Tezos blockchain.
2. **Decoding**: `naplps.decode()` or `naplps.load()` processes the byte stream into drawing commands.
3. **Rendering Prep**: `telidon.setup()` is initialized with the decoded commands.
4. **Drawing**: During `ofApp::draw()`, if the drawing is still in progress or marked dirty, `telidon.draw()` executes the OpenGL commands to update the cached `ofFbo`. The `ofFbo` is then presented to the window.

## Addons
The project relies on the following openFrameworks addons (as listed in `addons.make`):
- `ofxNaplps`
- `ofxHTTP`
- `ofxIO`
- `ofxMediaType`
- `ofxNetworkUtils`
- `ofxPoco`
- `ofxSSLManager`
- `ofxJSON`
- `ofxCrypto`
- `ofxXmlSettings`

## Building on 64-bit Raspberry Pi OS (linuxaarch64)

`ofxPoco` ships with openFrameworks but bundles no Poco binaries on Linux — it
links against the system Poco via `ADDON_LDFLAGS`. Its `addon_config.mk` has
sections for `linux64`, `linuxarmv6l` and `linuxarmv7l` but **not**
`linuxaarch64`, so on 64-bit Pi OS no `-lPoco*` flags are emitted and the link
step fails with hundreds of undefined `Poco::` references (raised through
`ofxIO`, which uses Poco heavily). `setup.sh` installs `libpoco-dev` and adds
the missing `linuxaarch64:` section to `addons/ofxPoco/addon_config.mk`; the
patch is idempotent, and it must be re-applied after a fresh openFrameworks
checkout since that file lives outside this repo.
