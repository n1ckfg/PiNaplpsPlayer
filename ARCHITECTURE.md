# PiNaplpsPlayer Architecture

PiNaplpsPlayer is an openFrameworks application designed to receive and render NAPLPS (North American Presentation Level Protocol Syntax) graphics. It operates both as a standalone file viewer and as a WebSocket server that can receive live streams of NAPLPS drawings over the network.

## Core Components

### Application Lifecycle (`ofApp`)
- **`setup()`**: Initializes window settings, loads local `.nap` sample files, sets up an `ofFbo` for rendering, and starts the WebSocket server listening on port 7112.
- **`update()`**: Safely pulls any new drawing frames off the incoming network queue (protected by a mutex) and pushes them to the decoder. Updates the `Telidon` renderer state.
- **`draw()`**: Caches the current state of the drawing by rendering the `Telidon` object to an `ofFbo`. The `ofFbo` is only updated while the drawing is in progress or when marked dirty, reducing GPU load. The cached `ofFbo` is then scaled and drawn to the screen through the image-effect shader (see Configuration), which runs every frame on the way out so the `ofFbo` itself stays a clean copy of the drawing. Also displays an overlay with current status, connection count, and hotkey information.

### NAPLPS Processing
The core graphics processing is handled by the `ofxNaplps` openFrameworks addon:
- **`Naplps` (Decoder)**: Parses raw NAPLPS byte streams or `.nap` files into an internal representation of drawing commands.
- **`Telidon` (Renderer)**: Takes the parsed commands from the decoder and performs the actual OpenGL drawing commands to render the graphics to the screen. It supports progressive drawing (animating the drawing process over time) and point labeling.

### Configuration (`bin/data/settings.xml`)
Read once in `setup()` via `ofxXmlSettings`, following the same convention as the other Pinopticon apps:
- **`slide_timeout`** (ms): how long the player waits without a websocket drawing before the dead man's switch engages. `0` disables the fallback.
- **`slide_interval`** (ms): how often the fallback swaps in a new random drawing.
- **`shader_name`**: the image effect applied when the `ofFbo` is drawn to the screen. `setup()` loads `bin/data/shaders/<name>_gl3` on the desktop GL 3.2 context (`_es3` under `TARGET_OPENGLES`, `_gl2` on the fixed pipeline). Fragment shaders read the drawing from `uniform sampler2DRect tex0`. If the pair doesn't load, the drawing goes to the screen unprocessed.

### Dead Man's Switch
A player showing a stale drawing looks identical to a crashed one, so `ofApp::checkDeadMansSwitch()` runs every `update()` and watches the clock since the last network frame:
- After `slide_timeout` ms of silence it sets `slideshowActive` and calls `loadRandomNap()`, which picks a random `.nap` from `bin/data` (never the one already on screen) and loads it.
- While active it reloads a new random drawing every `slide_interval` ms.
- The first websocket drawing to arrive clears `slideshowActive` and hands the screen back to the network. Arrow keys and drag-and-drop do the same, so a deliberate choice gets a full timeout on screen before the slideshow resumes.

`scanSamples()` builds the sample list by listing `bin/data` for `.nap` files rather than from a hardcoded list, so both the arrow keys and the switch pick up anything dropped into that directory.

### Network Layer
- **WebSocket Server**: Uses `ofxHTTP` (via the `Pinopticon_Http.hpp` wrapper) to run a WebSocket server on a dedicated thread. 
- **Message Parsing**: Frames can arrive in JSON format (with either raw text or base64 encoded payloads) or as raw NAPLPS streams. The application parses the incoming frames, extracts the NAPLPS data, and places it into a thread-safe incoming queue (`incomingMutex`).
- **External Integration**: Designed to receive streams pushed by an external server (e.g., `nap-xtz-server`).

### Pinopticon Utilities
The `src/` directory includes several utility headers under the `Pinopticon` namespace, providing reusable network and utility wrappers:
- **`Pinopticon.hpp`**: General utilities (hostname resolution, timestamps, image/FBO to buffer conversion).
- **`Pinopticon_Http.hpp`**: Wrappers for setting up `ofxHTTP` MJPEG streams, POST servers, and WebSocket servers, as well as functions to broadcast data.
- **`Pinopticon_Osc.hpp`**: Wrappers for setting up and sending messages via `ofxOsc` (unused in this specific application).

## Data Flow
1. **Input**:
   - **Local File**: User drags and drops a `.nap` file or uses arrow keys to cycle through samples.
   - **Network**: WebSocket server receives a frame containing NAPLPS data.
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
