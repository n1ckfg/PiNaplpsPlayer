# PiNaplpsPlayer Architecture

PiNaplpsPlayer is an openFrameworks application designed to receive and render NAPLPS (North American Presentation Level Protocol Syntax) graphics. It operates in two primary modes: **Network Mode**, functioning as a WebSocket server to receive live streams of NAPLPS drawings, and **Slideshow Mode**, acting as a standalone file viewer and fallback when the network is idle.

Drawings are shown the way [LatkTwoscilloscope](https://github.com/n1ckfg/LatkBrushIntegrations) shows a Latk animation, through [ofxTwoscilloscope](https://github.com/n1ckfg/ofxTwoscilloscope): the shapes are encoded as one loop of XY oscilloscope audio, run through a chain of audio effects, and drawn back from the altered audio by a simulated oscilloscope beam, in their own colours. The altered loop can also play out of the sound card (muted by default), so it can drive a real scope in X-Y mode. Telidon's progressive drawing still decides how much of each shape is on screen, so the beam draws the picture on at the same pace the filled shapes used to.

## Core Components

### Application Lifecycle (`ofApp`)
- **`setup()`**: Initializes window settings, loads local `.nap` sample files, sets up an `ofFbo` for rendering, sets up the oscilloscope renderer, its effect chain and settings panel, opens the audio output if `volume` is above 0, starts the WebSocket server listening on port 7112, and optionally starts a background thread for Tezos chain polling.
- **`update()`**: Safely pulls any new drawing frames off the incoming network queue (protected by a mutex) and pushes them to the decoder. Advances `Telidon`'s progressive drawing. When the drawing has changed (Telidon has revealed another point, or a new drawing, redraw, view or panel setting has marked it dirty), it runs the oscilloscope round trip again and hands the altered loop to the audio output.
- **`draw()`**: Caches the drawing in an `ofFbo`, redrawn only when `update()` found it changed, reducing GPU load. The `ofFbo` holds one of three views, cycled with `v`: the oscilloscope **beams** (the default), the altered audio **decoded** back into coloured strokes, or Telidon's **original** filled shapes. The cached `ofFbo` is then scaled and drawn to the screen through the image-effect shader (see Configuration), which runs every frame on the way out so the `ofFbo` itself stays a clean copy of the drawing. Also displays the effects panel (`g`) and an overlay with current status, connection count, and hotkey information.

### NAPLPS Processing
The core graphics processing is handled by the `ofxNaplps` openFrameworks addon:
- **`Naplps` (Decoder)**: Parses raw NAPLPS byte streams or `.nap` files into an internal representation of drawing commands.
- **`Telidon` (Renderer)**: Takes the parsed commands from the decoder and runs the progressive drawing (animating the drawing process over time): every point, line and polygon command reveals one more of its points every 66 ms. The oscilloscope renderer reads that state; Telidon's own OpenGL drawing is only used for the original view. It also supports point labeling.

### Oscilloscope Rendering (`NaplpsScopeRenderer`)
A port of LatkTwoscilloscope's `LatkScopeRenderer`, with the 3D projection replaced by reading Telidon. `update()` runs the whole round trip in a few milliseconds:

1. **Collect:** each command's shape is outlined the way `TelidonDrawCmd` draws it: points, lines and polygons as a closed polygon through the points Telidon has revealed so far, and arcs and rectangles whole, from the outline of the same `ofPath` Telidon fills. Colours carry from one SET COLOR command to the next shapes as they do in Telidon. A scope beam can't fill, so filled shapes become their outlines. The beams add light, so black would draw nothing; black shapes are drawn in the darkest gray of the NAPLPS palette instead, so they stay faintly visible. Text is skipped because Telidon places it below its screen. The outlines are mapped onto the `ofFbo` the way `draw()` places the Telidon screen, and cut exactly at its edges, since the canvas is the scope's screen and anything past it would clip the audio. Each visible run becomes a *piece* with its shape's colour.
2. **Encode:** the pieces are written as one loop of XYscope-format audio: X, Y and Z, with the canvas mapped to -1..1 and +Y up. The loop is a whole number of samples (8,820 at the default 5 Hz). Each piece gets a blanked sample that jumps the beam to its start, then lit samples spaced evenly along it, shared out by length so the beam moves at an even speed. Every piece gets at least two; if the loop is too short for that, the shortest pieces are left out.
3. **Transform:** the effect chain runs over five repeats of the loop, so filters and echoes settle, and the last one is kept, just as `XYTransformer` does in example-latk. `XYTransformer` itself isn't used, because it also decodes the whole loop back into shapes; nothing here needs that, and skipping it more than halved the time.
4. **Draw:** for the beams view, each piece's lit samples become an `OsciMesh` (a gaussian beam sweeping along each pair of samples, drawn additively), grouped into one mesh per colour, with brightness scaled by the average step so strokes come out at about `beam intensity` however long the drawing is. For the decoded view, each piece's samples are decoded back into polylines with `XYDecoder::decodeCycle()` and drawn in the piece's colour.

The effects pass Z through untouched and keep samples in place, so whatever an effect does to a piece's samples, the piece keeps its colour.

The effect chain is the one from ofxTwoscilloscope's `example-transform`. As in its `example-latk`, the low pass (1500 Hz) and the channel delay (0.6 ms on Y) are on by default, set from `settings.xml` (see Configuration); the rest start off. The loop runs at 5 Hz with a 3 px beam by default (`loop_hz`, `beam_size`), also as in `example-latk`. A Latk drawing has tens of long strokes, though, and a NAPLPS drawing can have thousands of short shapes: at 5 Hz most of them get two or three samples, past about 2,900 shapes the shortest are left out, and the low pass and channel delay smear the beam's jumps between shapes across the picture. Dense drawings read best with a lower loop frequency (1 Hz gives five times the samples) and those two effects turned down or off. `e` solos the next effect, `n` turns them all off, and the panel turns on any combination. An `XYscope` loops the altered audio (X left, Y right) on the default sound card at the `volume` setting, and gets the new loop each time the drawing changes. `m` mutes and unmutes it.

### Configuration (`bin/data/settings.xml`)
Read once in `setup()` via `ofxXmlSettings`, following the same convention as the other Pinopticon apps:
- **`slide_timeout`** (ms): how long the player waits without a Network Mode drawing before the Dead Man's Switch engages and activates Slideshow Mode. `0` disables the Slideshow Mode fallback.
- **`slide_interval`** (ms): how often Slideshow Mode swaps in a new random drawing.
- **`fbo_width`**: the width of the `ofFbo` used for caching the drawing.
- **`fbo_height`**: the height of the `ofFbo` used for caching the drawing.
- **`debug_view`**: if `1`, enables the debug overlay and point labels on startup.
- **`lowpass_cutoff`** (Hz, 20 to 20000) and **`lowpass_resonance`** (0.3 to 10, 0.707 flat): the low pass filter. A cutoff of `0` turns it off.
- **`channel_delay_x`** and **`channel_delay_y`** (ms, 0 to 20): the channel delay. Both at `0` turns it off.
- **`loop_hz`** (1 to 100, default `5`): how many times a second the beam draws the whole drawing. Lower gives every shape more samples; `1` keeps even dense drawings readable.
- **`beam_size`** (`fbo` pixels, 0.5 to 12, default `3`) and **`beam_intensity`** (0 to 4, default `1`): the beam's radius and brightness.
- **`volume`**: 0 to 1, the volume of the altered loop on the default sound card. The default, `0`, mutes it and leaves the sound card closed, and `m` can't unmute it; above `0`, `m` mutes and unmutes it.
- **`shader_name`**: the image effect applied when the `ofFbo` is drawn to the screen. `setup()` loads `bin/data/shaders/<name>_gl3` on the desktop GL 3.2 context (`_es3` under `TARGET_OPENGLES`, `_gl2` on the fixed pipeline). Fragment shaders read the drawing from `uniform sampler2DRect tex0`, in pixels. The `_es3` pairs are GLSL ES 1.00 instead (no `#version` line, `attribute`/`varying`, `gl_FragColor`), because `ofAppEGLWindow` only creates ES 2 contexts, and they read `uniform sampler2D tex0` in 0..1 coordinates, since ES has no rectangle textures. If the pair doesn't load, the drawing goes to the screen unprocessed.
- **`tezos_contract`**: the Tezos contract address to poll for on-chain NAPLPS drawings. Empty disables chain reads.
- **`tzkt_base`**: the TzKT indexer API base URL (e.g. `https://api.shadownet.tzkt.io/v1`).
- **`tezos_poll_seconds`**: how often the background thread polls TzKT (default `30`).
- **`tezos_max_bytes`**: maximum size of a single NAPLPS drawing from the chain (default `30000`).

The oscilloscope settings (every effect's parameters, the loop frequency, beam size and beam intensity) are on the `ofxGui` panel, toggled with `g`, which shows the cursor while it's open and ignores the mouse while it's hidden. Saving with the panel's disk icon writes `bin/data/effects.xml`, which `setup()` loads if it exists, so a player can be tuned once and keep its settings. The effect and scope settings in `settings.xml` are applied after it, so they override the same values saved there, and out-of-range values are clamped to the panel's ranges.

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
4. **Progressive Drawing**: `telidon.update()` reveals more of each shape over time.
5. **Oscilloscope**: whenever the revealed drawing changes, `NaplpsScopeRenderer` collects its outlines, encodes them as one loop of XY audio and runs it through the effect chain. The altered loop goes to the sound card.
6. **Drawing**: During `ofApp::draw()`, if the drawing changed, the altered loop is drawn into the cached `ofFbo` as beams (or decoded strokes, or Telidon's original shapes). The `ofFbo` is then presented to the window through the shader.

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
- `ofxGui`
- `ofxTwoscilloscope`

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
