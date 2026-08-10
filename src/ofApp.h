#pragma once

#include "ofMain.h"

#include "ofxNaplps.h"

class ofApp : public ofBaseApp {

    public:

        void setup();
        void update();
        void draw();

        void keyPressed(int key);
        void windowResized(int w, int h);
        void dragEvent(ofDragInfo dragInfo);

        void loadNap(const std::string & filePath);
        void updateLayout();

        Naplps naplps;   // the decoder,  ported from naplps.js
        Telidon telidon; // the renderer, ported from TelidonP5.js

        std::vector<std::string> samples;
        int sampleIndex;

        // The NAPLPS unit screen runs from (0,0) to (1,1), so it gets a square
        // of the window, centered.
        float drawSize;
        glm::vec2 drawOffset;

        bool progressiveDraw;
        bool labelPoints;
        bool showInfo;

};
