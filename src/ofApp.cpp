#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup() {
    ofSetWindowTitle("ofxNaplps");
    ofSetFrameRate(60);
    ofSetVerticalSync(true);
    ofEnableAntiAliasing();
    ofEnableAlphaBlending();
    ofBackground(0);

    // the other sample files in bin/data, cycled through with the arrow keys
    samples.push_back("shark.nap");
    samples.push_back("santa.nap");
    samples.push_back("beer.nap");
    samples.push_back("haunt.nap");
    samples.push_back("wast.nap");
    samples.push_back("email2.nap");
    sampleIndex = 0;

    progressiveDraw = true;
    labelPoints = false;
    showInfo = true;

    updateLayout();
    loadNap(samples[sampleIndex]);
}

//--------------------------------------------------------------
void ofApp::loadNap(const std::string & filePath) {
    // 1. decode the file
    //naplps.setVerbose(true); // uncomment to log every command and point
    if (!naplps.load(filePath)) return;

    // 2. hand the decoded commands to the renderer
    telidon.setup(naplps, drawSize, drawSize);
    telidon.setProgressiveDraw(progressiveDraw);
    telidon.setLabelPoints(labelPoints);
}

//--------------------------------------------------------------
void ofApp::updateLayout() {
    drawSize = MIN(ofGetWidth(), ofGetHeight());
    drawOffset = glm::vec2((ofGetWidth() - drawSize) / 2.0f, (ofGetHeight() - drawSize) / 2.0f);
}

//--------------------------------------------------------------
void ofApp::update() {
    telidon.update();
}

//--------------------------------------------------------------
void ofApp::draw() {
    ofBackground(0);

    ofPushMatrix();
    ofTranslate(drawOffset.x, drawOffset.y);
    telidon.draw();
    ofPopMatrix();

    if (showInfo) {
        std::string info = naplps.fileName + "\n";
        info += "Telidon " + ofToString(naplps.version) + ", " + ofToString(naplps.cmds.size()) + " commands\n";
        info += telidon.isFinished() ? "finished\n" : "drawing...\n";
        info += "\n";
        info += "arrows: next/prev file\n";
        info += "space:  redraw\n";
        info += "p:      progressive draw " + std::string(progressiveDraw ? "on" : "off") + "\n";
        info += "l:      label points " + std::string(labelPoints ? "on" : "off") + "\n";
        info += "i:      hide this\n";
        info += "(or drop a .nap file on the window)";
        ofDrawBitmapStringHighlight(info, 10, 20);
    }
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key) {
    switch (key) {
        case ' ':
            telidon.reset();
            break;
        case OF_KEY_RIGHT:
        case OF_KEY_DOWN:
            sampleIndex = (sampleIndex + 1) % (int)samples.size();
            loadNap(samples[sampleIndex]);
            break;
        case OF_KEY_LEFT:
        case OF_KEY_UP:
            sampleIndex = (sampleIndex + (int)samples.size() - 1) % (int)samples.size();
            loadNap(samples[sampleIndex]);
            break;
        case 'p':
            progressiveDraw = !progressiveDraw;
            telidon.setProgressiveDraw(progressiveDraw);
            break;
        case 'l':
            labelPoints = !labelPoints;
            telidon.setLabelPoints(labelPoints);
            break;
        case 'i':
            showInfo = !showInfo;
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
}
