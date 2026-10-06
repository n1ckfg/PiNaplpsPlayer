#pragma once

#include "ofMain.h"

#include "ofxNaplps.h"
#include "ofxTwoscilloscope.h"

// Draws a NAPLPS drawing through ofxTwoscilloscope, the way LatkTwoscilloscope
// draws a Latk animation: the shapes are encoded as one loop of XY audio, run
// through the effect chain, and drawn back from the altered audio by a
// simulated oscilloscope beam.
//
// Telidon still does the progressive drawing. This reads how far it has got
// with each command, so the beam draws the picture on at the same pace. A
// scope beam can't fill, so filled shapes come out as their outlines.
//
// The shapes are encoded here rather than by XYscope, so that every sample of
// the loop is known to belong to one shape. The effects pass Z through
// untouched, so that still holds after them, and each shape is drawn from its
// own samples in its own colour.
class NaplpsScopeRenderer {

    public:

        void setup(int sampleRate = 44100);

        // Encodes and transforms whatever telidon has drawn so far. Its unit
        // screen is scaled to telidon's size and moved by offset, the way
        // ofApp::draw() places it, onto a width x height canvas.
        void update(const Telidon & telidon, const glm::vec2 & offset, float width, float height);

        // The beam sweeping through the altered audio in real time, as a scope
        // shows it. Fades what's already there by the afterglow, then draws
        // the stretch of the loop the beam has covered in the last `seconds`,
        // so draw it into a buffer that's kept from one frame to the next.
        void drawLive(float seconds);
        // The whole loop of the altered audio drawn by the beam at once.
        void drawBeams();
        // The altered audio decoded back into strokes.
        void drawStrokes();
        // Telidon's debug circles, on the points it has drawn so far.
        void drawLabels();

        float getFreq() const { return freq; }
        // one loop of the altered audio, -1..1: for XYscope::setWaveforms()
        void getLoop(std::vector<float> & _x, std::vector<float> & _y, std::vector<float> & _z) const;

        // what the audio goes through
        XYEffectChain effects;
        // loops the effects run over before the one that's kept, so that
        // filters and echoes settle, as XYTransformer::settleCycles
        int settleCycles = 4;
        // XYscope's blanking levels, and how the strokes view decodes
        XYDecoderSettings decoder;

        ofParameterGroup parameters;
        ofParameter<float> loopFreq;      // Hz: lower gives the drawing more samples
        ofParameter<float> beamSize;      // beam radius, px
        ofParameter<float> beamIntensity; // brightness of a stroke drawn at an even speed
        ofParameter<float> afterglow;     // seconds for the live beam's trace to fade to half

        struct Stats {
            size_t pieces = 0;
            size_t dropped = 0;     // pieces left out because the loop is too short
            size_t samples = 0;     // per loop
            float pathLength = 0;   // px
            float ms = 0;           // collecting, encoding and running the effects
        };
        const Stats & getStats() const { return stats; }

    private:

        struct Piece {
            std::vector<glm::vec2> points; // canvas px
            ofColor color;
            float length = 0;
            size_t start = 0;         // first sample: blanked, on the first point
            size_t lit = 0;           // then this many lit samples, from end to end
        };

        void collect(const Telidon & telidon, const glm::vec2 & offset);
        // cuts a shape (canvas px) into the pieces that fit on the canvas
        void addPieces(std::vector<glm::vec2> & pts, bool closed, const ofColor & color);
        void encode();
        void groupBeams();
        void buildBeams();
        // light per sample, so a stroke peaks at about beamIntensity
        float exposure() const;
        // the transform from scope units to the canvas
        void pushScopeMatrix() const;
        void decodeStrokes();

        int sampleRate = 44100;
        float freq = 5;
        size_t cycleFrames = 8820;
        ofRectangle canvas;

        std::vector<Piece> pieces;
        std::vector<glm::vec2> labels; // canvas px
        std::vector<float> x, y, z;    // one loop of the altered audio

        // One beam per colour: the beams add up, so the drawing order doesn't matter.
        struct Beam {
            ofFloatColor color;
            std::vector<size_t> pieces;
            ofMesh mesh; // the whole loop, for drawBeams()
        };
        OsciMesh osci;
        std::vector<Beam> beams;
        std::vector<float> sx;       // x in scope units, which keep the beam round
        std::vector<float> bright;   // 0 for the jump OsciMesh makes to each run, then 1
        float stepAverage = 0;       // the beam's average step, in scope units
        bool beamsDirty = true;
        double playhead = 0;         // where the live beam has got to in the loop, in samples

        std::vector<ofPolyline> strokes;
        std::vector<size_t> strokePieces; // the piece each stroke was decoded from
        bool strokesDirty = true;

        Stats stats;

};
