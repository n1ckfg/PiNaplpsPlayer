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

        // Encodes whatever telidon has drawn so far. Its unit screen is
        // scaled to telidon's size and moved by offset, the way ofApp::draw()
        // places it, onto a width x height canvas. The new loop takes over
        // from the old as advance() plays it.
        void update(const Telidon & telidon, const glm::vec2 & offset, float width, float height);

        // Plays the next `seconds` of the loop through the effects, carrying
        // on from where the last call left them, the way an effects unit
        // would hear it. Returns whether the altered loop changed: with
        // nothing that varies over time switched on, it settles and stops.
        bool advance(float seconds);

        // The altered audio drawn by the oscilloscope beam.
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
        // loops the effects run over when they start from scratch, so that
        // filters and echoes settle, as XYTransformer::settleCycles
        int settleCycles = 4;
        // XYscope's blanking levels, and how the strokes view decodes
        XYDecoderSettings decoder;

        ofParameterGroup parameters;
        ofParameter<float> loopFreq;      // Hz: lower gives the drawing more samples
        ofParameter<float> beamSize;      // beam radius, px
        ofParameter<float> beamIntensity; // brightness of a stroke drawn at an even speed

        struct Stats {
            size_t pieces = 0;
            size_t dropped = 0;     // pieces left out because the loop is too short
            size_t samples = 0;     // per loop
            float pathLength = 0;   // px
            float ms = 0;           // collecting and encoding
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
        // starts the effects from scratch on the current loop
        void prime();
        void buildBeams();
        // The beams built on the GPU, where GLSL 1.30 can number the
        // vertices. Returns false on the GLES and GL 2 renderers, which build
        // them into OsciMesh instead.
        bool setupSegmentBeams();
        // brings the segments the effects have rewritten up to date
        void updateSegments(const std::vector<float> & sx, float aspect);
        void drawSegmentBeams();
        // the samples from start, round the loop, have changed since the
        // beams were last built
        void markDirty(size_t start, size_t count);
        void decodeStrokes();

        int sampleRate = 44100;
        float freq = 5;
        size_t cycleFrames = 8820;
        ofRectangle canvas;

        std::vector<Piece> pieces;
        std::vector<glm::vec2> labels; // canvas px
        // One loop of the audio as encoded, X, Y and Z interleaved, and the
        // piece each sample belongs to: its tag (0 between pieces) and colour.
        std::vector<float> dry;
        std::vector<uint32_t> dryTags;
        std::vector<ofColor> dryColors;
        uint32_t nextTag = 1;

        // The same loop as it comes out of the effects, each sample written
        // over as the effects reach it, so until then a sample still belongs
        // to the piece it was encoded from.
        std::vector<float> x, y, z;
        std::vector<uint32_t> tags;
        std::vector<ofColor> colors;
        size_t playhead = 0;  // the next sample the effects will reach
        double pending = 0;   // samples due, short of a whole one

        struct Beam {
            ofFloatColor color;
            ofMesh mesh;
        };
        OsciMesh osci;
        std::vector<Beam> beams;
        float beamExposure = 1;
        bool beamsDirty = true;

        // The beams on the GPU: the four corners of each segment of the
        // altered loop, from one sample to the next, all holding its two ends
        // and colour, so the vertex shader can build OsciMesh's quad from
        // them, and a fixed index buffer joining them into two triangles.
        // They're kept in the loop's order, so when the effects rewrite a
        // stretch of samples, only that stretch's segments are uploaded.
        struct SegmentVertex {
            glm::vec4 ends;  // x0, y0, x1, y1 in scope units
            float rgb;       // the colour as a 24-bit integer, or -1 where the beam is dark
        };
        bool segmentBeamsTried = false;
        bool segmentBeams = false;
        ofShader segmentShader;
        ofBufferObject segmentBuffer;
        ofVbo segmentVbo;
        std::vector<SegmentVertex> segmentVertices;
        float segmentAspect = 0;
        size_t dirtyStart = 0;
        size_t dirtyCount = 0;

        std::vector<ofPolyline> strokes;
        std::vector<ofColor> strokeColors;
        bool strokesDirty = true;

        Stats stats;

};
