#include "ofApp.h"

#include "Pinopticon.hpp"
#include "Pinopticon_Http.hpp"

//using namespace cv;
//using namespace ofxCv;
using namespace Pinopticon;

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

    progressiveDraw = true;
    labelPoints = false;
    showInfo = false;
    bFboDirty = true;

    updateLayout();
    if (!samples.empty()) loadNap(samples[sampleIndex]);
    napSource = "file";

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

    if (showInfo) {
        static std::string lastState = "";
        std::string currentState = ofToString(connections) + "_" + ofToString(received) + "_" + (telidon.isFinished() ? "1" : "0") + "_" + napSource + "_" + naplps.fileName + "_" + ofToString(progressiveDraw) + "_" + ofToString(labelPoints) + "_" + ofToString(slideshowActive);
        if (currentState != lastState) {
            updateInfoText();
            lastState = currentState;
        }
    }
}

//--------------------------------------------------------------
void ofApp::draw() {
    if (!telidon.isFinished() || bFboDirty) {
        fbo.begin();
        ofBackground(0);

        ofPushMatrix();
        ofTranslate(drawOffset.x, drawOffset.y);
        telidon.draw();
        ofPopMatrix();
        fbo.end();

        if (telidon.isFinished()) {
            bFboDirty = false;
        }
    }
	
    // The effect goes on as the cached drawing is copied to the screen, not
    // into the fbo itself, so the fbo stays a clean copy of the drawing. A
    // shader that didn't load just means the plain drawing.
    if (shader.isLoaded()) shader.begin();
	fbo.draw(0, 0, ofGetWidth(), ofGetHeight()); //720, 480);
    if (shader.isLoaded()) shader.end();

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
    if (slideTimeout <= 0) return; // fallback switched off in settings.xml
    if (samples.empty()) return;   // nothing to fall back to

    const uint64_t now = ofGetElapsedTimeMillis();

    if (!slideshowActive) {
        if (now - lastMessageTime < (uint64_t)slideTimeout) return;

        ofLogNotice("PiNaplpsPlayer") << "no drawing in " << slideTimeout
                                      << "ms, falling back to bin/data";
        slideshowActive = true;
        loadRandomNap();
        return;
    }

    if (slideInterval > 0 && now - lastSlideTime >= (uint64_t)slideInterval) {
        loadRandomNap();
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
    const NapFrame frame = parseNapFrame(evt.frame().toString());
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
        default:
            break;
    }
}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h) {
    updateLayout();
    telidon.setSize(drawSize, drawSize);
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
        infoText += "no signal: random every " + ofToString(slideInterval) + "ms\n";
    }
    infoText += "\n";
    infoText += "ws://" + hostName + ":" + ofToString(WS_PORT) + "\n";
    infoText += ofToString(connections) + " connected, " + ofToString(received) + " received\n";
    infoText += "\n";
    infoText += "arrows: next/prev file\n";
    infoText += "space:  redraw\n";
    infoText += "p:      progressive draw " + std::string(progressiveDraw ? "on" : "off") + "\n";
    infoText += "l:      label points " + std::string(labelPoints ? "on" : "off") + "\n";
    infoText += "i:      hide this\n";
    infoText += "(or drop a .nap file on the window)";
}
