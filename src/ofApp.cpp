#include "ofApp.h"

#include <cctype>

#include "Pinopticon.hpp"
#include "Pinopticon_Http.hpp"

//using namespace cv;
//using namespace ofxCv;
using namespace Pinopticon;

// Walk a TzKT JSON value and collect strings that could be NAPLPS data.
// Tezos `bytes` values arrive hex-encoded; plain `string` values arrive as-is.
static void collectNaplpsStrings(const ofJson& j, std::vector<std::string>& out, int maxBytes) {
    if (j.is_string()) {
        std::string val = j.get<std::string>();
        if (val.size() < 10) return;

        bool allHex = (val.size() % 2 == 0);
        if (allHex) {
            for (char c : val) {
                if (!std::isxdigit(static_cast<unsigned char>(c))) { allHex = false; break; }
            }
        }

        if (allHex && (int)val.size() >= 20) {
            std::string decoded;
            decoded.reserve(val.size() / 2);
            for (std::size_t i = 0; i < val.size(); i += 2) {
                unsigned int byte = 0;
                std::sscanf(val.c_str() + i, "%02x", &byte);
                decoded.push_back(static_cast<char>(byte));
            }
            if ((int)decoded.size() <= maxBytes) out.push_back(std::move(decoded));
        } else if ((int)val.size() <= maxBytes) {
            out.push_back(val);
        }
    } else if (j.is_object()) {
        for (auto& el : j.items()) collectNaplpsStrings(el.value(), out, maxBytes);
    } else if (j.is_array()) {
        for (auto& el : j) collectNaplpsStrings(el, out, maxBytes);
    }
}

//--------------------------------------------------------------
void ofApp::setup() {
    ofSetWindowTitle("PiNaplpsPlayer");
    ofSetFrameRate(60);
    //ofSetVerticalSync(true);
    //ofEnableAntiAliasing();
    //ofEnableAlphaBlending(); // Alpha disabled for performance
    ofBackground(0);
    ofHideCursor();

    settings.loadFile("settings.xml");
    slideTimeout = settings.getValue("settings:slide_timeout", 30000);
    slideInterval = settings.getValue("settings:slide_interval", 10000);

    fboWidth = settings.getValue("settings:fbo_width", 640);
    fboHeight = settings.getValue("settings:fbo_height", 480);

    // the sample files in bin/data, cycled through with the arrow keys and
    // drawn from at random by the dead man's switch
    scanSamples();
    sampleIndex = 0;

    debugView = (bool) settings.getValue("settings:debug_view", 0); 
    progressiveDraw = true;
    labelPoints = debugView;
    showInfo = debugView;
    bFboDirty = true;

    updateLayout();
    if (!samples.empty()) loadRandomNap(); //loadNap(samples[sampleIndex]);
    napSource = "random"; //file";

    // The websocket server starts listening the moment it's set up, so
    // everything a frame touches has to be ready first.
    hasIncoming = false;
    received = 0;
    connections = 0;
    hostName = Pinopticon::getHostName();

    // Nothing has arrived yet, so the clock starts now: an app that comes up
    // with no server on the other end falls back after one timeout.
    ofSeedRandom();
    lastMessageTime = ofGetElapsedTimeMillis();
    lastSlideTime = lastMessageTime;
    slideshowActive = false;

    Pinopticon::setupWsServer(this, wsServer, WS_PORT, MAX_NAP_BYTES);
	
    fbo.allocate(fboWidth, fboHeight, GL_RGB);

    shaderName = settings.getValue("settings:shader_name", "vhsc"); 

#ifdef TARGET_OPENGLES
    shader.load("shaders/" + shaderName + "_es3");
#else
    if (ofIsGLProgrammableRenderer()) {
        shader.load("shaders/" + shaderName + "_gl3");
    } else {
        shader.load("shaders/" + shaderName + "_gl2");
    }
#endif

    if (!shader.isLoaded()) {
        ofLogWarning("PiNaplpsPlayer") << "shader " << shaderName << " didn't load, drawing without it";
    }

    scope.setup(44100);
    view = BEAMS;
    soloIndex = -1;
    lastRevealed = 0;

    // The effect chain from ofxTwoscilloscope's example-transform, in order.
    // The low pass and the channel delay are set in settings.xml further down;
    // the rest start off, for the panel and the e key.
    auto & effects = scope.effects;
    auto lowPass = effects.add<XYLowPass>();
    auto delay = effects.add<XYChannelDelay>();
    effects.add<XYHighPass>();
    effects.add<XYEcho>();
    effects.add<XYRingMod>();
    effects.add<XYRotate>();
    effects.add<XYDrive>();
    effects.add<XYWavefold>();
    effects.add<XYBitCrush>();
    effects.add<XYSampleHold>();
    effects.add<XYNoise>();
    for (auto & effect : effects.effects) effect->enabled = false;

    // Every setting is on the panel. Whatever was saved from it with its disk
    // icon comes back on the next start, so a player can be tuned once.
    gui.setup(effects.parameters, "effects.xml");
    gui.add(scope.parameters);
    if (ofFile::doesFileExist("effects.xml")) gui.loadFromFile("effects.xml");

    // The two effects example-latk turns on, at its settings by default. They
    // come after effects.xml, so settings.xml has the last word on them, and
    // they're clamped to the panel's ranges: past them the delay line is too
    // short and the filter goes unstable. 0 turns either one off.
    const float cutoff = settings.getValue("settings:lowpass_cutoff", 1500.0);
    lowPass->enabled = cutoff > 0;
    lowPass->cutoff = ofClamp(cutoff, lowPass->cutoff.getMin(), lowPass->cutoff.getMax());
    lowPass->resonance = ofClamp(settings.getValue("settings:lowpass_resonance", 0.707),
                                 lowPass->resonance.getMin(), lowPass->resonance.getMax());

    const float delayX = settings.getValue("settings:channel_delay_x", 0.0);
    const float delayY = settings.getValue("settings:channel_delay_y", 0.6);
    delay->enabled = delayX > 0 || delayY > 0;
    delay->delayX = ofClamp(delayX, delay->delayX.getMin(), delay->delayX.getMax());
    delay->delayY = ofClamp(delayY, delay->delayY.getMin(), delay->delayY.getMax());

    for (auto & effect : effects.effects) {
        if (!effect->enabled) gui.getGroup(effect->getName()).minimize();
    }
    setShowGui(false);

    // The panel's group holds the effects and the scope settings added to it,
    // so any change on the panel makes a new loop.
    paramsListener = gui.getParameter().castGroup().parameterChangedE().newListener([this](ofAbstractParameter &) {
        bFboDirty = true;
    });

    // The altered loop plays out of the sound card, X left and Y right, so a
    // real scope in X-Y mode draws what's on screen. It's an unpleasant buzz
    // through ordinary speakers, so it's muted unless settings.xml says otherwise.
    player.setup(0, 0, 44100, 512);
    volume = ofClamp(settings.getValue("settings:volume", 0.0), 0, 1);
    if (volume > 0) openAudio();

    tezosContract = settings.getValue("settings:tezos_contract", "KT1DypSEV87pwiw6swdYqhDKWRBZ7xfqeS3c");
    tzktBase = settings.getValue("settings:tzkt_base", "https://api.shadownet.tzkt.io/v1");
    tezosPollSeconds = settings.getValue("settings:tezos_poll_seconds", 30);
    tezosMaxBytes = settings.getValue("settings:tezos_max_bytes", 30000);
    tezosDrawingIndex = 0;
    slideshowFromChain = false;

    if (!tezosContract.empty()) {
        // No bin/data/ssl/cacert.pem ships with the app, and ofSSLManager's fallback
        // context trusts nothing. Use the OS trust store instead.
        ofSSLManager::initializeClient(new Poco::Net::Context(
            //Poco::Net::Context::TLS_CLIENT_USE, "",
            Poco::Net::Context::CLIENT_USE, "",
            Poco::Net::Context::VERIFY_RELAXED, 9, true /* loadDefaultCAs */));

        tezosRunning = true;
        tezosThread = std::thread(&ofApp::tezosThreadFunc, this);
    }
}

//--------------------------------------------------------------
void ofApp::loadNap(const std::string & filePath) {
    // 1. decode the file
    //naplps.setVerbose(true); // uncomment to log every command and point
    if (!naplps.load(filePath)) return;

    // 2. hand the decoded commands to the renderer
    startDrawing();
}

//--------------------------------------------------------------
// Whatever .nap files are in bin/data, in name order -- reading the directory
// rather than a hardcoded list means a drawing dropped in there is picked up
// by both the arrow keys and the dead man's switch.
void ofApp::scanSamples() {
    samples.clear();

    ofDirectory dir(ofToDataPath("", true));
    dir.allowExt("nap");
    dir.sort();
    dir.listDir();

    for (std::size_t i = 0; i < dir.size(); i++) {
        samples.push_back(dir.getName(i));
    }

    if (samples.empty()) {
        ofLogWarning("PiNaplpsPlayer") << "no .nap files in bin/data";
    }
}

//--------------------------------------------------------------
// The same thing for a drawing that arrived over the network: the bytes are
// already in hand, so they go straight to the decoder without touching a file.
void ofApp::showNap(const std::string & napRaw, const std::string & label) {
    naplps.decode(napRaw);

    if (!naplps.isLoaded()) {
        ofLogWarning("PiNaplpsPlayer") << "nothing to draw in " << label;
        return;
    }

    // decode() doesn't set a file name, and the old one would be a lie.
    naplps.fileName = label;

    startDrawing();
}

//--------------------------------------------------------------
void ofApp::startDrawing() {
    telidon.setup(naplps, drawSize, drawSize);
    telidon.setProgressiveDraw(progressiveDraw);
    telidon.setLabelPoints(labelPoints);
    bFboDirty = true;
}

//--------------------------------------------------------------
void ofApp::updateLayout() {
	drawSize = fboWidth; //MIN(ofGetWidth(), ofGetHeight());
	drawOffset = glm::vec2(0, fboHeight - fboWidth); //glm::vec2((ofGetWidth() - drawSize) / 2.0f, (ofGetHeight() - drawSize) / 2.0f);
    bFboDirty = true;
}

//--------------------------------------------------------------
void ofApp::update() {
    // Collect whatever the websocket thread left for us. Only the newest
    // drawing is kept: a player shows one at a time, so an older frame that
    // arrived in the same window has already been superseded.
    NapFrame frame;
    bool gotOne = false;

    {
        std::lock_guard<std::mutex> lock(incomingMutex);
        if (hasIncoming) {
            frame = incoming;
            hasIncoming = false;
            gotOne = true;
        }
    }

    if (gotOne) {
        napSource = frame.source.empty() ? "network" : frame.source;
        showNap(frame.nap, "(" + napSource + ")");

        // The network is alive and back in charge of the screen.
        lastMessageTime = ofGetElapsedTimeMillis();
        slideshowActive = false;
    }

    checkDeadMansSwitch();

    telidon.update();

    // The drawing only changes when Telidon draws on another point, or when
    // a new drawing, a redraw or a setting marks it dirty. Only then does the
    // scope need a new loop, and the fbo a new picture.
    std::size_t revealed = 0;
    for (auto & drawCmd : telidon.drawCmds) revealed += drawCmd.points.size();

    if (bFboDirty || revealed != lastRevealed) {
        lastRevealed = revealed;
        bFboDirty = true;

        // the whole round trip: shapes -> audio -> effects -> shapes
        scope.update(telidon, drawOffset, fboWidth, fboHeight);

        // loop the altered audio, Z (blanking) included
        std::vector<float> x, y, z;
        scope.getLoop(x, y, z);
        player.freq(scope.getFreq());
        player.setWaveforms(x, y, z);

        if (showInfo) updateInfoText();
    }

    if (showInfo) {
        static std::string lastState = "";
        std::string currentState = ofToString(connections) + "_" + ofToString(received) + "_" + (telidon.isFinished() ? "1" : "0") + "_" + napSource + "_" + naplps.fileName + "_" + ofToString(progressiveDraw) + "_" + ofToString(labelPoints) + "_" + ofToString(slideshowActive) + "_" + ofToString(player.isAudioOutOpen());
        if (currentState != lastState) {
            updateInfoText();
            lastState = currentState;
        }
    }
}

//--------------------------------------------------------------
void ofApp::draw() {
    // update() has already encoded whatever made the fbo dirty.
    if (bFboDirty) {
        fbo.begin();
        ofBackground(0);

        switch (view) {
            case BEAMS:
                scope.drawBeams();
                break;
            case STROKES:
                scope.drawStrokes();
                break;
            case ORIGINAL:
                // Telidon's own filled shapes, labels and all, for comparison
                ofPushMatrix();
                ofTranslate(drawOffset.x, drawOffset.y);
                telidon.draw();
                ofPopMatrix();
                break;
        }
        if (labelPoints && view != ORIGINAL) scope.drawLabels();

        fbo.end();
        bFboDirty = false;
    }
	
    // The effect goes on as the cached drawing is copied to the screen, not
    // into the fbo itself, so the fbo stays a clean copy of the drawing. A
    // shader that didn't load just means the plain drawing.
    if (shader.isLoaded()) shader.begin();
	fbo.draw(0, 0, ofGetWidth(), ofGetHeight()); //720, 480);
    if (shader.isLoaded()) shader.end();

    if (showGui) gui.draw();

    if (showInfo) {
        ofDrawBitmapStringHighlight(infoText, 10, 20);
    }
}

//--------------------------------------------------------------
// Called every frame. While the network is talking to us this does nothing;
// once it has been quiet for slideTimeout ms it starts the fallback slideshow
// and keeps it turning over every slideInterval ms. update() clears
// slideshowActive the moment a real drawing arrives, which ends it.
void ofApp::checkDeadMansSwitch() {
    if (slideTimeout <= 0) return;

    bool hasChain;
    {
        std::lock_guard<std::mutex> lock(tezosMutex);
        hasChain = !tezosDrawings.empty();
    }

    if (samples.empty() && !hasChain) return;

    const uint64_t now = ofGetElapsedTimeMillis();

    if (!slideshowActive) {
        if (now - lastMessageTime < (uint64_t)slideTimeout) return;

        ofLogNotice("PiNaplpsPlayer") << "no drawing in " << slideTimeout
                                      << "ms, falling back to slideshow";
        slideshowActive = true;
        slideshowFromChain = false;
        if (!samples.empty()) {
            loadRandomNap();
        } else {
            loadChainNap();
        }
        return;
    }

    if (slideInterval > 0 && now - lastSlideTime >= (uint64_t)slideInterval) {
        if (slideshowFromChain) {
            if (!loadChainNap()) {
                if (!samples.empty()) loadRandomNap();
            }
            slideshowFromChain = false;
        } else {
            if (!samples.empty()) {
                loadRandomNap();
            } else {
                loadChainNap();
            }
            slideshowFromChain = hasChain;
        }
    }
}

//--------------------------------------------------------------
// A random file from bin/data, never the one already on screen -- repeating a
// drawing reads as a frozen player, which is the thing the switch exists to
// avoid.
void ofApp::loadRandomNap() {
    const int count = (int)samples.size();
    int index = (int)ofRandom(count);
    if (index >= count) index = count - 1; // ofRandom's top end is inclusive

    if (count > 1 && index == sampleIndex) index = (index + 1) % count;

    sampleIndex = index;
    lastSlideTime = ofGetElapsedTimeMillis();

    loadNap(samples[sampleIndex]);
    napSource = "random";
}

//--------------------------------------------------------------
bool ofApp::loadChainNap() {
    std::string nap;
    {
        std::lock_guard<std::mutex> lock(tezosMutex);
        if (tezosDrawings.empty()) return false;

        int count = (int)tezosDrawings.size();
        int index = (int)ofRandom(count);
        if (index >= count) index = count - 1;
        if (count > 1 && index == tezosDrawingIndex) index = (index + 1) % count;
        tezosDrawingIndex = index;
        nap = tezosDrawings[index];
    }

    lastSlideTime = ofGetElapsedTimeMillis();
    showNap(nap, "(chain)");
    napSource = "chain";
    return true;
}

//--------------------------------------------------------------
void ofApp::tezosThreadFunc() {
    for (int i = 0; i < 5 && tezosRunning; i++) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    while (tezosRunning) {
        std::vector<std::string> found;

        try {
            ofxHTTP::Client client;

            {
                std::string url = tzktBase + "/contracts/" + tezosContract + "/bigmaps";
                ofxHTTP::GetRequest req(url);
                auto resp = client.execute(req);

                if (resp->isSuccess()) {
                    ofJson bigmaps = resp->json();
                    if (bigmaps.is_array()) {
                        for (auto& bm : bigmaps) {
                            if (!tezosRunning) break;
                            if (!bm.contains("ptr") || bm.value("activeKeys", 0) == 0) continue;

                            int ptr = bm["ptr"].get<int>();
                            std::string keysUrl = tzktBase + "/bigmaps/" + ofToString(ptr)
                                                  + "/keys?active=true&limit=100";
                            ofxHTTP::GetRequest keysReq(keysUrl);
                            auto keysResp = client.execute(keysReq);

                            if (keysResp->isSuccess()) {
                                ofJson keys = keysResp->json();
                                if (keys.is_array()) {
                                    for (auto& entry : keys) {
                                        if (entry.contains("value")) {
                                            collectNaplpsStrings(entry["value"], found, tezosMaxBytes);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (found.empty() && tezosRunning) {
                std::string url = tzktBase + "/contracts/" + tezosContract + "/storage";
                ofxHTTP::GetRequest req(url);
                auto resp = client.execute(req);

                if (resp->isSuccess()) {
                    collectNaplpsStrings(resp->json(), found, tezosMaxBytes);
                }
            }

        } catch (const Poco::Exception& e) {
            ofLogWarning("Tezos") << "poll failed: " << e.displayText();
        } catch (const std::exception& e) {
            ofLogWarning("Tezos") << "poll failed: " << e.what();
        } catch (...) {
            ofLogWarning("Tezos") << "poll failed";
        }

        if (!found.empty()) {
            std::lock_guard<std::mutex> lock(tezosMutex);
            tezosDrawings = std::move(found);
            ofLogNotice("Tezos") << "cached " << tezosDrawings.size() << " drawings from chain";
        }

        for (int i = 0; i < tezosPollSeconds && tezosRunning; i++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

//--------------------------------------------------------------
void ofApp::exit() {
    player.closeAudioOut();

    tezosRunning = false;
    if (tezosThread.joinable()) tezosThread.join();
}

//--------------------------------------------------------------
// Frames from nap-xtz-server arrive in one of three shapes, set by
// RPI_NAPLPS_FORMAT on that side:
//
//   json    {"type":"naplps","source":"slideshow","encoding":"text","naplps":"..."}
//   base64  the same envelope, with the stream base64'd
//   raw     the NAPLPS stream on its own, no envelope
//
// Anything else on this port -- a camera command meant for one of the other
// Pinopticon apps, a keepalive -- is not a drawing and is left alone.
ofApp::NapFrame ofApp::parseNapFrame(const std::string & text) const {
    NapFrame frame;

    if (text.empty()) return frame;

    // A .nap stream opens with a control byte, never with '{'.
    if (text[0] != '{') {
        if (text == "take_photo" || text == "stream_photo" || text == "keepalive") return frame;
        frame.nap = text;
        frame.source = "raw";
        return frame;
    }

    ofxJSONElement json;
    if (!json.parse(text)) {
        ofLogWarning("PiNaplpsPlayer") << "frame wasn't valid JSON";
        return frame;
    }

    if (json["type"].asString() != "naplps") return frame;

    frame.source = json["source"].asString();

    // NAPLPS is a 7-bit-safe format, so "text" carries the stream through JSON
    // intact -- the control bytes travel as \u00xx escapes and come back whole.
    // "base64" is there for a payload that uses the high half anyway.
    const std::string payload = json["naplps"].asString();
    frame.nap = (json["encoding"].asString() == "base64")
        ? ofxCrypto::base64_decode(payload)
        : payload;

    return frame;
}

//--------------------------------------------------------------
void ofApp::onWebSocketOpenEvent(ofxHTTP::WebSocketEventArgs & evt) {
    connections++;
    ofLogNotice("PiNaplpsPlayer") << "websocket opened: " << evt.connection().clientAddress().toString();
}

//--------------------------------------------------------------
void ofApp::onWebSocketCloseEvent(ofxHTTP::WebSocketCloseEventArgs & evt) {
    if (connections > 0) connections--;
    ofLogNotice("PiNaplpsPlayer") << "websocket closed: " << evt.connection().clientAddress().toString();
}

//--------------------------------------------------------------
void ofApp::onWebSocketFrameReceivedEvent(ofxHTTP::WebSocketFrameEventArgs & evt) {
    std::string payload;
    evt.frame().readBytes(payload);
    const NapFrame frame = parseNapFrame(payload);
    if (frame.nap.empty()) return;

    ofLogNotice("PiNaplpsPlayer") << "received " << frame.nap.size() << " bytes of NAPLPS"
                                  << (frame.source.empty() ? "" : " from " + frame.source);

    // Hand it to update(); this is a server thread, not the GL thread.
    std::lock_guard<std::mutex> lock(incomingMutex);
    incoming = frame;
    hasIncoming = true;
    received++;
}

//--------------------------------------------------------------
void ofApp::onWebSocketFrameSentEvent(ofxHTTP::WebSocketFrameEventArgs & evt) {
    // nothing to do -- the player only listens
}

//--------------------------------------------------------------
void ofApp::onWebSocketErrorEvent(ofxHTTP::WebSocketErrorEventArgs & evt) {
    ofLogWarning("PiNaplpsPlayer") << "websocket error: " << evt.connection().clientAddress().toString();
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key) {
    switch (key) {
        case ' ':
            telidon.reset();
            bFboDirty = true;
            break;
        case OF_KEY_RIGHT:
        case OF_KEY_DOWN:
            if (samples.empty()) break;
            sampleIndex = (sampleIndex + 1) % (int)samples.size();
            loadNap(samples[sampleIndex]);
            napSource = "file";
            // A hand on the keys outranks the switch: hold this drawing for a
            // full timeout before the slideshow takes over again.
            lastMessageTime = ofGetElapsedTimeMillis();
            slideshowActive = false;
            break;
        case OF_KEY_LEFT:
        case OF_KEY_UP:
            if (samples.empty()) break;
            sampleIndex = (sampleIndex + (int)samples.size() - 1) % (int)samples.size();
            loadNap(samples[sampleIndex]);
            napSource = "file";
            lastMessageTime = ofGetElapsedTimeMillis();
            slideshowActive = false;
            break;
        case 'p':
            progressiveDraw = !progressiveDraw;
            telidon.setProgressiveDraw(progressiveDraw);
            bFboDirty = true;
            break;
        case 'l':
            labelPoints = !labelPoints;
            telidon.setLabelPoints(labelPoints);
            bFboDirty = true;
            break;
        case 'i':
            showInfo = !showInfo;
            if (showInfo) updateInfoText();
            break;
        case 'f':
            ofToggleFullscreen();
            break;
        case 'v':
            view = View((view + 1) % 3);
            bFboDirty = true;
            break;
        case 'e':
            soloEffect((soloIndex + 1) % (int)scope.effects.effects.size());
            break;
        case 'n':
            soloEffect(-1);
            break;
        case 'g':
            setShowGui(!showGui);
            break;
        case 'm':
            if (player.isAudioOutOpen()) {
                player.closeAudioOut();
            } else if (volume > 0) {
                // At volume 0 the player stays silent, sound card closed.
                openAudio();
            }
            break;
        default:
            break;
    }
}

//--------------------------------------------------------------
// Turns on one effect at a time, to see what each one does. -1 turns them
// all off, leaving the round trip on its own.
void ofApp::soloEffect(int index) {
    soloIndex = index;
    auto & effects = scope.effects.effects;
    for (std::size_t i = 0; i < effects.size(); i++) {
        auto & effect = effects[i];
        effect->enabled = (int)i == index;
        if (effect->enabled) {
            gui.getGroup(effect->getName()).maximize();
        } else {
            gui.getGroup(effect->getName()).minimize();
        }
    }
}

//--------------------------------------------------------------
void ofApp::openAudio() {
    // X and Y only: Z is the beam's blanking, not sound.
    player.amp(volume, volume);
    if (!player.openAudioOut()) {
        ofLogWarning("PiNaplpsPlayer") << "no sound card found, running silently";
    }
}

//--------------------------------------------------------------
// The panel needs the mouse, which a player normally hides. While it's
// hidden it ignores the mouse too, so a stray click can't change a setting.
void ofApp::setShowGui(bool show) {
    showGui = show;
    if (showGui) {
        gui.setPosition(ofGetWidth() - gui.getWidth() - 10, 10);
        gui.registerMouseEvents();
        ofShowCursor();
    } else {
        gui.unregisterMouseEvents();
        ofHideCursor();
    }
}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h) {
    updateLayout();
    telidon.setSize(drawSize, drawSize);
    gui.setPosition(w - gui.getWidth() - 10, 10);
}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo) {
    if (dragInfo.files.size() < 1) return;

    loadNap(dragInfo.files[0]);
    napSource = "file";
    lastMessageTime = ofGetElapsedTimeMillis();
    slideshowActive = false;
}

//--------------------------------------------------------------
void ofApp::updateInfoText() {
    infoText = naplps.fileName + "\n";
    infoText += "Telidon " + ofToString(naplps.version) + ", " + ofToString(naplps.cmds.size()) + " commands\n";
    infoText += telidon.isFinished() ? "finished\n" : "drawing...\n";
    infoText += "source: " + napSource + "\n";
    if (slideshowActive) {
        infoText += "no signal: slideshow every " + ofToString(slideInterval) + "ms\n";
    }
    {
        std::lock_guard<std::mutex> lock(tezosMutex);
        infoText += "chain: " + ofToString(tezosDrawings.size()) + " cached";
        if (!tezosContract.empty()) infoText += " (polling)";
        infoText += "\n";
    }
    infoText += "\n";
    {
        static const char * viewNames[] = { "beams", "decoded strokes", "original" };
        const auto & stats = scope.getStats();
        infoText += "scope: " + std::string(viewNames[view]) + ", loop " + ofToString(scope.getFreq(), 1) + " Hz, "
            + ofToString(stats.pieces) + " strokes";
        if (stats.dropped > 0) infoText += " (" + ofToString(stats.dropped) + " too short to fit)";
        infoText += ", " + ofToString(stats.ms, 1) + " ms\n";

        std::string on;
        for (auto & effect : scope.effects.effects) {
            if (effect->enabled) on += (on.empty() ? "" : ", ") + effect->getName();
        }
        infoText += "effects: " + (on.empty() ? std::string("none") : on) + "\n";
        infoText += "audio: " + (player.isAudioOutOpen() ? "volume " + ofToString(volume, 2) : std::string("muted")) + "\n";
    }
    infoText += "\n";
    infoText += "ws://" + hostName + ":" + ofToString(WS_PORT) + "\n";
    infoText += ofToString(connections) + " connected, " + ofToString(received) + " received\n";
    infoText += "\n";
    infoText += "arrows: next/prev file\n";
    infoText += "space:  redraw\n";
    infoText += "p:      progressive draw " + std::string(progressiveDraw ? "on" : "off") + "\n";
    infoText += "l:      label points " + std::string(labelPoints ? "on" : "off") + "\n";
    infoText += "v:      view beams/strokes/original\n";
    infoText += "e:      solo next effect\n";
    infoText += "n:      no effects\n";
    infoText += "g:      effects panel\n";
    infoText += "m:      mute/unmute audio\n";
    infoText += "i:      hide this\n";
    infoText += "(or drop a .nap file on the window)";
}
