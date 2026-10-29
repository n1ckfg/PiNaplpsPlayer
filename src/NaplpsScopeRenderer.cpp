#include "NaplpsScopeRenderer.h"

namespace {

// Cuts the segment a-b to the rectangle (Liang-Barsky). Returns false if none
// of it is inside, or sets t0 and t1 to where the inside part starts and ends.
bool clipSegment(const glm::vec2 & a, const glm::vec2 & b, const ofRectangle & r, float & t0, float & t1) {
    const glm::vec2 d = b - a;
    const float p[4] = { -d.x, d.x, -d.y, d.y };
    const float q[4] = { a.x - r.getLeft(), r.getRight() - a.x, a.y - r.getTop(), r.getBottom() - a.y };
    t0 = 0;
    t1 = 1;
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0) {
            // parallel to this edge, so all in or all out
            if (q[i] < 0) return false;
            continue;
        }
        const float t = q[i] / p[i];
        if (p[i] < 0) t0 = std::max(t0, t);
        else t1 = std::min(t1, t);
        if (t0 > t1) return false;
    }
    return true;
}

// The curved and rectangular shapes TelidonDrawCmd draws, built the same way,
// so that their outlines match what it would fill and stroke.

// drawRect(): from two corners
ofPath rectPath(const std::vector<glm::vec2> & pts, float w, float h) {
    ofPath path;
    path.rectangle(pts[0].x * w, pts[0].y * h, (pts[1].x - pts[0].x) * w, (pts[1].y - pts[0].y) * h);
    return path;
}

// drawArc(): a circle from two points, or a run of half-ellipse slices
ofPath arcPath(const std::vector<glm::vec2> & pts, float w, float h) {
    ofPath path;
    if (pts.size() == 2) {
        const float x1 = pts[0].x * w;
        const float y1 = pts[0].y * h;
        const float d = pts[1].x * w - x1;
        path.ellipse(x1 + d / 2, y1 + d / 2, d, d);
    } else {
        for (int i = 0; i < (int)pts.size() - 1; i++) {
            const float x1 = pts[i].x * w;
            const float y1 = pts[i].y * h;
            const float rx = (pts[i + 1].x * w - x1) / 2;
            const float ry = (pts[i + 1].y * h - y1) / 2;
            const float a1 = i * (180.0f / pts.size());
            const float a2 = (i + 1) * (180.0f / pts.size());
            path.arc(x1 + rx, y1 + ry, rx, ry, a1, a2);
        }
    }
    return path;
}

// ofMesh has no move constructor, so std::swap() on two of them copies both.
// Their vertices and normals are all OsciMesh adds to and draws.
void swapLines(ofMesh & a, ofMesh & b) {
    a.getVertices().swap(b.getVertices());
    a.getNormals().swap(b.getNormals());
}

// OsciMesh's beam, with its quads built on the GPU: the four corners of a
// segment's quad all hold its two ends, and gl_VertexID says which corner each
// one is. The index buffer makes them the two triangles OsciMesh::addLine()
// would add, sharing two corners, so each corner is shaded once.
const std::string SEGMENT_BEAM_VERT = R"GLSL(
    uniform mat4 modelViewProjectionMatrix;
    uniform float uSize;
    in vec4 position; // x0, y0, x1, y1
    in float color;   // 24-bit, or -1 for dark
    out vec3 vUvl;
    out vec3 vRgb;

    void main() {
        vUvl = vec3(0.0);
        vRgb = vec3(0.0);
        if (color < 0.0) {
            // dark from here to the next sample: all four corners on one
            // point, off screen, so nothing is drawn
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            return;
        }

        vec2 p0 = position.xy;
        vec2 p1 = position.zw;
        vec2 dir = p1 - p0;
        float len = length(dir);
        dir = len > 1E-6 ? dir / len : vec2(1.0, 0.0);
        dir *= uSize;
        vec2 norm = vec2(-dir.y, dir.x);

        // corners 2 and 3 are past p1, and 1 and 3 on the normal's side
        int corner = gl_VertexID % 4;
        bool end = corner >= 2;
        bool side = corner == 1 || corner == 3;
        vec2 p = (end ? p1 + dir : p0 - dir) + (side ? norm : -norm);
        vUvl = vec3(end ? len + uSize : -uSize, side ? uSize : -uSize, len);

        uint c = uint(color);
        vRgb = vec3(float((c >> 16u) & 255u), float((c >> 8u) & 255u), float(c & 255u)) / 255.0;
        gl_Position = modelViewProjectionMatrix * vec4(p, 0.0, 1.0);
    }
)GLSL";

// OsciMesh's BEAM_FUNCTIONS, with the colour from the vertex shader
const std::string SEGMENT_BEAM_FRAG = R"GLSL(
    uniform float uSize;
    uniform float uIntensity;
    in vec3 vUvl;
    in vec3 vRgb;
    out vec4 fragColor;

    #define SQRT2 1.4142135623730951
    #define TAUR 2.5066282746310002

    float erfApprox(float x) {
        float s = sign(x), a = abs(x);
        x = 1.0 + (0.278393 + (0.230389 + 0.000972 * a + 0.078108 * a * a) * a) * a;
        x *= x;
        return s - s / (x * x);
    }

    void main() {
        float len = vUvl.z;
        vec2 xy = vUvl.xy;
        float sigma = uSize / 3.0;
        float b;
        if (len < 1E-6) {
            b = exp(-dot(xy, xy) / (2.0 * sigma * sigma));
        } else {
            b = erfApprox(xy.x / SQRT2 / sigma) - erfApprox((xy.x - len) / SQRT2 / sigma);
            b *= exp(-xy.y * xy.y / (2.0 * sigma * sigma)) * sigma * TAUR / (2.0 * len);
        }
        b *= uIntensity;
        // where the beam is brightest it burns towards white
        fragColor = vec4(vRgb * b + vec3(max(b - 1.0, 0.0) * 0.35), 1.0);
    }
)GLSL";

}

//--------------------------------------------------------------
void NaplpsScopeRenderer::setup(int _sampleRate) {
    sampleRate = _sampleRate;
    parameters.setName("scope");
    // example-latk's settings
    parameters.add(loopFreq.set("loop Hz", 5, 1, 100));
    parameters.add(beamSize.set("beam size", 3, 0.5, 12));
    parameters.add(beamIntensity.set("beam intensity", 1, 0, 4));
}

//--------------------------------------------------------------
void NaplpsScopeRenderer::update(const Telidon & telidon, const glm::vec2 & offset, float width, float height) {
    const uint64_t startMicros = ofGetElapsedTimeMicros();
    beamsDirty = true;
    strokesDirty = true;
    if (width < 1 || height < 1) return;

    // The loop is a whole number of samples, so XYscope plays it back one
    // table entry per sample.
    cycleFrames = std::max<size_t>(2, std::lround(sampleRate / std::max(0.1f, loopFreq.get())));
    freq = float(sampleRate) / cycleFrames;
    canvas.set(0, 0, width, height);

    collect(telidon, offset);
    encode();
    stats.ms = (ofGetElapsedTimeMicros() - startMicros) / 1000.0f;
}

//--------------------------------------------------------------
void NaplpsScopeRenderer::collect(const Telidon & telidon, const glm::vec2 & offset) {
    pieces.clear();
    labels.clear();
    const float w = telidon.w;
    const float h = telidon.h;
    const glm::vec2 scale(w, h);

    // TelidonDraw::draw() resets its render state to white, and a SET COLOR
    // command changes it for every shape after it.
    ofColor color(255);

    std::vector<glm::vec2> pts; // canvas px, reused from shape to shape
    for (const auto & drawCmd : telidon.drawCmds) {
        const NapCmd & cmd = drawCmd.cmd;
        // Most shapes are polygons through these points, and the rest are
        // the outlines of the ofPath Telidon would draw.
        const std::vector<glm::vec2> * polygon = nullptr;
        std::vector<ofPolyline> outlines;

        switch (cmd.opcode.opId) {
            case NAP_OP_SET_COLOR:
            case NAP_OP_SELECT_COLOR:
                color = cmd.col;
                continue;

            // Points, lines and polygons are drawn on a point at a time, from
            // the points Telidon has revealed so far.
            case NAP_OP_POINT_SET_ABS:
            case NAP_OP_POINT_SET_REL:
            case NAP_OP_POINT_ABS:
            case NAP_OP_POINT_REL:
            case NAP_OP_LINE_ABS:
            case NAP_OP_LINE_REL:
            case NAP_OP_SET_LINE_ABS:
            case NAP_OP_SET_LINE_REL:
            case NAP_OP_POLY_OUTLINED:
            case NAP_OP_POLY_FILLED:
            case NAP_OP_SET_POLY_OUTLINED:
            case NAP_OP_SET_POLY_FILLED:
                polygon = &drawCmd.points;
                break;

            // Arcs and rectangles appear whole, from all of the command's points.
            case NAP_OP_ARC_OUTLINED:
            case NAP_OP_ARC_FILLED:
            case NAP_OP_SET_ARC_OUTLINED:
            case NAP_OP_SET_ARC_FILLED:
                outlines = arcPath(cmd.points, w, h).getOutline();
                break;

            case NAP_OP_RECT_OUTLINED:
            case NAP_OP_RECT_FILLED:
            case NAP_OP_SET_RECT_OUTLINED:
            case NAP_OP_SET_RECT_FILLED:
                if (cmd.points.size() == 2) {
                    outlines = rectPath(cmd.points, w, h).getOutline();
                } else {
                    polygon = &cmd.points;
                }
                break;

            // Telidon places text below the bottom of its screen for now, and
            // the other commands draw nothing.
            default:
                continue;
        }

        // TelidonDrawCmd::drawPoints() labels every polygon's points.
        if (polygon) {
            for (auto & p : *polygon) labels.push_back(p * scale + offset);
        }

        // The beams add light, so a black shape would draw nothing. The
        // darkest gray in the NAPLPS palette keeps it faintly visible instead.
        const ofColor & shapeColor = color.getBrightness() == 0 ? nap::defaultColorMap()[1] : color;

        if (polygon) {
            // makeShape(): the points joined up and closed, like p5's endShape(CLOSE)
            pts.clear();
            for (auto & p : *polygon) pts.push_back(p * scale + offset);
            addPieces(pts, true, shapeColor);
        } else {
            for (auto & outline : outlines) {
                pts.clear();
                for (auto & v : outline.getVertices()) pts.push_back(glm::vec2(v) + offset);
                addPieces(pts, outline.isClosed(), shapeColor);
            }
        }
    }

    stats.pathLength = 0;
    for (auto & piece : pieces) stats.pathLength += piece.length;
}

//--------------------------------------------------------------
void NaplpsScopeRenderer::addPieces(std::vector<glm::vec2> & pts, bool closed, const ofColor & color) {
    // Closing goes back to the first point. Two points closed are just the
    // line between them.
    if (closed && pts.size() > 2) pts.push_back(pts.front());

    // The canvas is the scope's screen, and past its edges the audio would
    // clip, so cut the shape where it leaves the canvas.
    const ofRectangle bounds(0, 0, canvas.width, canvas.height);
    bool open = false; // whether the next segment continues the last piece
    for (std::size_t i = 1; i < pts.size(); i++) {
        float t0, t1;
        if (clipSegment(pts[i - 1], pts[i], bounds, t0, t1)) {
            const glm::vec2 a = glm::mix(pts[i - 1], pts[i], t0);
            const glm::vec2 b = glm::mix(pts[i - 1], pts[i], t1);
            if (!open || t0 > 0) {
                pieces.emplace_back();
                pieces.back().color = color;
                pieces.back().points.push_back(a);
            }
            pieces.back().points.push_back(b);
            pieces.back().length += glm::distance(a, b);
            open = t1 == 1;
        } else {
            open = false;
        }
    }
}

//--------------------------------------------------------------
void NaplpsScopeRenderer::encode() {
    const size_t n = cycleFrames;
    stats.samples = n;
    stats.dropped = 0;

    // Every piece takes a blank sample that jumps the beam to its start, and at
    // least two lit ones, for its ends. If the loop is too short for that, the
    // shortest pieces are left out.
    const size_t maxPieces = n / 3;
    if (pieces.size() > maxPieces) {
        std::vector<size_t> order(pieces.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return pieces[a].length > pieces[b].length; });
        std::vector<bool> keep(pieces.size(), false);
        for (size_t i = 0; i < maxPieces; i++) keep[order[i]] = true;
        std::vector<Piece> kept;
        kept.reserve(maxPieces);
        for (size_t i = 0; i < pieces.size(); i++) {
            if (keep[i]) kept.push_back(std::move(pieces[i]));
        }
        stats.dropped = pieces.size() - kept.size();
        pieces.swap(kept);
    }
    stats.pieces = pieces.size();

    // The rest of the loop is shared out by length, so the beam moves at an
    // even speed, as it does in XYscope's waveforms.
    double totalLength = 0;
    for (auto & piece : pieces) totalLength += piece.length;
    const size_t spare = n - 3 * pieces.size();

    // one loop in XYscope's format: X, Y and Z interleaved, the canvas mapped
    // to -1..1 with +Y up, and Z blanking the beam between pieces
    const XYDecoderSettings & levels = decoder;
    dry.assign(n * 3, 0);
    dryTags.assign(n, 0);
    dryColors.assign(n, ofColor(0));
    size_t i = 0;
    uint32_t tag = 0;
    ofColor color;
    auto write = [&](const glm::vec2 & p, bool lit) {
        dry[i * 3] = p.x / canvas.width * 2 - 1;
        dry[i * 3 + 1] = 1 - p.y / canvas.height * 2;
        dry[i * 3 + 2] = lit ? levels.zMax : levels.zMin;
        if (lit) {
            dryTags[i] = tag;
            dryColors[i] = color;
        }
        i++;
    };

    double before = 0; // length of the pieces so far
    for (size_t k = 0; k < pieces.size(); k++) {
        auto & piece = pieces[k];
        // rounded from running totals, so the shares add up to exactly the spare samples
        const double after = before + piece.length;
        size_t share;
        if (totalLength > 0) {
            share = size_t(std::llround(spare * after / totalLength) - std::llround(spare * before / totalLength));
        } else {
            share = spare * (k + 1) / pieces.size() - spare * k / pieces.size();
        }
        before = after;

        piece.start = i;
        piece.lit = 2 + share;
        // every piece gets a new tag, even the same shape encoded again
        tag = nextTag++;
        if (nextTag == 0) nextTag = 1;
        color = piece.color;
        write(piece.points.front(), false);

        // lit samples at even steps along the piece, from its first point to its last
        size_t seg = 0;
        float segStart = 0; // length along the piece to points[seg]
        float segLength = glm::distance(piece.points[0], piece.points[1]);
        for (size_t j = 0; j < piece.lit; j++) {
            const float at = piece.length * j / (piece.lit - 1);
            while (seg + 2 < piece.points.size() && segStart + segLength < at) {
                segStart += segLength;
                seg++;
                segLength = glm::distance(piece.points[seg], piece.points[seg + 1]);
            }
            const float t = segLength > 0 ? ofClamp((at - segStart) / segLength, 0, 1) : 1;
            write(glm::mix(piece.points[seg], piece.points[seg + 1], t), true);
        }
    }
    // with nothing to draw, the beam rests blanked in the middle
    while (i < n) write(glm::vec2(canvas.width, canvas.height) * 0.5f, false);

    // Otherwise the effects carry on as they are, and play the new loop from
    // wherever they've got to.
    if (x.size() != n) prime();
}

//--------------------------------------------------------------
void NaplpsScopeRenderer::prime() {
    // The effects run over a few loops, so that filters and echoes settle,
    // and the last one is kept, just as XYTransformer::transform() does in
    // example-latk. XYTransformer itself also decodes the whole loop back into
    // shapes, which nothing here uses and which took most of the time.
    const size_t n = cycleFrames;
    const size_t loops = std::max(0, settleCycles) + 1;
    ofSoundBuffer audio;
    audio.allocate(n * loops, 3);
    audio.setSampleRate(sampleRate);
    for (size_t l = 0; l < loops; l++) {
        std::copy(dry.begin(), dry.end(), audio.getBuffer().begin() + l * dry.size());
    }
    effects.reset();
    effects.process(audio);

    const float * last = audio.getBuffer().data() + (loops - 1) * dry.size();
    x.resize(n);
    y.resize(n);
    z.resize(n);
    for (size_t j = 0; j < n; j++) {
        x[j] = last[j * 3];
        y[j] = last[j * 3 + 1];
        z[j] = last[j * 3 + 2];
    }
    tags = dryTags;
    colors = dryColors;
    playhead = 0;
    pending = 0;
    dirtyStart = 0;
    dirtyCount = n;
}

//--------------------------------------------------------------
bool NaplpsScopeRenderer::advance(float seconds) {
    const size_t n = cycleFrames;
    if (x.size() != n || dry.size() != n * 3) return false;

    // the samples that would have played since the last frame, at most a loop
    pending = std::min(pending + double(seconds) * sampleRate, double(n));
    const size_t count = size_t(pending);
    if (count == 0) return false;
    pending -= count;

    ofSoundBuffer audio;
    audio.allocate(count, 3);
    audio.setSampleRate(sampleRate);
    std::vector<float> & samples = audio.getBuffer();
    for (size_t j = 0; j < count; j++) {
        const size_t i = (playhead + j) % n;
        std::copy(&dry[i * 3], &dry[i * 3] + 3, &samples[j * 3]);
    }
    effects.process(audio);

    // Once the effects settle on a loop that isn't changing, every sample
    // comes out as it did last time, and there's nothing new to draw.
    bool changed = false;
    for (size_t j = 0; j < count; j++) {
        const size_t i = (playhead + j) % n;
        const float * s = &samples[j * 3];
        if (tags[i] != dryTags[i] || std::abs(s[0] - x[i]) > 1e-5f || std::abs(s[1] - y[i]) > 1e-5f) changed = true;
        x[i] = s[0];
        y[i] = s[1];
        z[i] = s[2];
        tags[i] = dryTags[i];
        colors[i] = dryColors[i];
    }
    playhead = (playhead + count) % n;

    if (changed) {
        markDirty((playhead + n - count) % n, count);
        beamsDirty = true;
        strokesDirty = true;
    }
    return changed;
}

//--------------------------------------------------------------
void NaplpsScopeRenderer::markDirty(size_t start, size_t count) {
    // Each stretch the effects play starts where the last one ended, so
    // together they're one stretch, from the first one's start.
    const size_t n = x.size();
    if (dirtyCount == 0) {
        dirtyStart = start;
        dirtyCount = count;
    } else {
        dirtyCount = std::max(dirtyCount, (start + n - dirtyStart) % n + count);
    }
    dirtyCount = std::min(dirtyCount, n);
}

//--------------------------------------------------------------
void NaplpsScopeRenderer::getLoop(std::vector<float> & _x, std::vector<float> & _y, std::vector<float> & _z) const {
    _x = x;
    _y = y;
    _z = z;
}

//--------------------------------------------------------------
void NaplpsScopeRenderer::buildBeams() {
    beamsDirty = false;
    beams.clear();
    if (x.size() != cycleFrames || tags.size() != x.size()) return;
    if (!segmentBeamsTried) segmentBeams = setupSegmentBeams();
    const size_t n = x.size();

    // Scope units: -1..1 up the canvas, and as far across it as its shape
    // allows, so the beam stays round on any canvas.
    const float aspect = canvas.width / canvas.height;
    std::vector<float> sx(n);
    for (size_t i = 0; i < n; i++) sx[i] = x[i] * aspect;
    osci.uSize = beamSize / (canvas.height / 2);

    // The beam is lit from one sample to the next when both are from the
    // same piece, so each run of lit samples with one tag is one line.
    auto lit = [&](size_t i) { return tags[i] != 0 && i + 1 < n && tags[i + 1] == tags[i]; };

    // A beam leaves less light on a line the faster it moves, so a longer
    // drawing or a shorter loop comes out dimmer. Scale the light by the
    // average step, so a stroke peaks at about beamIntensity either way.
    double stepSum = 0;
    size_t steps = 0;
    for (size_t i = 0; i + 1 < n; i++) {
        if (!lit(i)) continue;
        stepSum += glm::distance(glm::vec2(sx[i], y[i]), glm::vec2(sx[i + 1], y[i + 1]));
        steps++;
    }
    const float sigma = osci.uSize / 3;
    beamExposure = steps > 0 ? float(stepSum / steps) / (sigma * std::sqrt(TWO_PI)) : 1;

    if (segmentBeams) {
        updateSegments(sx, aspect);
        return;
    }

    auto forEachRun = [&](auto fn) {
        for (size_t i = 0; i < n;) {
            size_t end = i + 1;
            while (end < n && tags[end] == tags[i]) end++;
            if (tags[i] != 0 && end - i >= 2) fn(i, end);
            i = end;
        }
    };

    // OsciMesh joins each run of samples to the end of the last one, lit as
    // the run's first sample. Keep that jump dark.
    std::vector<float> bright(n, 1);
    bright[0] = 0;

    // addLines() reserves exactly what it adds, so adding run after run would
    // copy the whole mesh every time. Reserve each colour's six vertices a
    // sample up front instead.
    std::map<int, size_t> verticesOfColor;
    forEachRun([&](size_t i, size_t end) { verticesOfColor[colors[i].getHex()] += 6 * (end - i); });

    // One mesh per colour: the beams add up, so the drawing order doesn't matter.
    std::map<int, size_t> beamOfColor;
    forEachRun([&](size_t i, size_t end) {
        auto found = beamOfColor.find(colors[i].getHex());
        if (found == beamOfColor.end()) {
            found = beamOfColor.emplace(colors[i].getHex(), beams.size()).first;
            osci.clear();
            beams.push_back({ colors[i], osci.mesh });
            beams.back().mesh.getVertices().reserve(verticesOfColor[colors[i].getHex()]);
            beams.back().mesh.getNormals().reserve(verticesOfColor[colors[i].getHex()]);
        }
        swapLines(beams[found->second].mesh, osci.mesh);
        osci.addLines(&sx[i], &y[i], bright.data(), int(end - i));
        swapLines(beams[found->second].mesh, osci.mesh);
    });
}

//--------------------------------------------------------------
bool NaplpsScopeRenderer::setupSegmentBeams() {
    segmentBeamsTried = true;
#ifdef TARGET_OPENGLES
    return false;
#else
    if (!ofIsGLProgrammableRenderer()) return false;
    auto renderer = ofGetGLRenderer();
    const std::string glsl = ofGLSLVersionFromGL(renderer->getGLVersionMajor(), renderer->getGLVersionMinor());
    if (ofToInt(glsl) < 130) return false;

    const std::string version = "#version " + glsl + "\n";
    bool ok = segmentShader.setupShaderFromSource(GL_VERTEX_SHADER, version + SEGMENT_BEAM_VERT) &&
              segmentShader.setupShaderFromSource(GL_FRAGMENT_SHADER, version + SEGMENT_BEAM_FRAG);
    if (ok) {
        segmentShader.bindDefaults();
        ok = segmentShader.linkProgram();
    }
    if (!ok) {
        ofLogWarning("NaplpsScopeRenderer") << "couldn't compile the segment beam shader, building meshes instead";
        return false;
    }

    // the ends in the position attribute, and the colour in the color one
    segmentBuffer.allocate();
    segmentVbo.setAttributeBuffer(ofShader::POSITION_ATTRIBUTE, segmentBuffer, 4, sizeof(SegmentVertex), offsetof(SegmentVertex, ends));
    segmentVbo.setAttributeBuffer(ofShader::COLOR_ATTRIBUTE, segmentBuffer, 1, sizeof(SegmentVertex), offsetof(SegmentVertex, rgb));
    return true;
#endif
}

//--------------------------------------------------------------
void NaplpsScopeRenderer::updateSegments(const std::vector<float> & sx, float aspect) {
    const size_t n = x.size();
    const size_t segments = n - 1;

    auto write = [&](size_t i) {
        const bool lit = tags[i] != 0 && tags[i + 1] == tags[i];
        const SegmentVertex vertex { glm::vec4(sx[i], y[i], sx[i + 1], y[i + 1]), lit ? float(colors[i].getHex()) : -1.0f };
        std::fill_n(&segmentVertices[i * 4], 4, vertex);
    };

    // A new loop length or canvas shape moves every segment, and the buffer
    // is sized for the new loop.
    if (segmentVertices.size() != segments * 4 || segmentAspect != aspect) {
        if (segmentVertices.size() != segments * 4) {
            // corners 0, 1, 2 and 1, 2, 3 of every quad, as OsciMesh::addLine()
            std::vector<ofIndexType> indices(segments * 6);
            for (size_t i = 0; i < segments; i++) {
                const ofIndexType corner = ofIndexType(i * 4);
                const ofIndexType quad[6] = { corner, corner + 1, corner + 2, corner + 1, corner + 2, corner + 3 };
                std::copy(quad, quad + 6, &indices[i * 6]);
            }
            segmentVbo.setIndexData(indices.data(), int(indices.size()), GL_STATIC_DRAW);
        }
        segmentAspect = aspect;
        segmentVertices.resize(segments * 4);
        for (size_t i = 0; i < segments; i++) write(i);
        segmentBuffer.setData(segmentVertices.size() * sizeof(SegmentVertex), segmentVertices.data(), GL_DYNAMIC_DRAW);
        dirtyCount = 0;
        return;
    }

    // segments [from, to)
    auto update = [&](size_t from, size_t to) {
        if (from >= to) return;
        for (size_t i = from; i < to; i++) write(i);
        segmentBuffer.updateData(from * 4 * sizeof(SegmentVertex), (to - from) * 4 * sizeof(SegmentVertex), &segmentVertices[from * 4]);
    };
    if (dirtyCount >= n) {
        update(0, segments);
    } else if (dirtyCount > 0) {
        // from the segment into the first sample that changed to the one out
        // of the last, round the loop, where there's no segment from the
        // last sample back to the first
        const size_t first = (dirtyStart + n - 1) % n;
        const size_t last = first + dirtyCount + 1;
        update(first, std::min(last, segments));
        if (last > n) update(0, last - n);
    }
    dirtyCount = 0;
}

//--------------------------------------------------------------
void NaplpsScopeRenderer::drawSegmentBeams() {
    if (segmentVertices.empty()) return;

    ofPushStyle();
    ofEnableBlendMode(OF_BLENDMODE_ADD);
    segmentShader.begin();
    segmentShader.setUniform1f("uSize", osci.uSize);
    segmentShader.setUniform1f("uIntensity", beamIntensity * beamExposure);
    segmentVbo.drawElements(GL_TRIANGLES, int(segmentVertices.size() / 4 * 6));
    segmentShader.end();
    ofPopStyle();
}

//--------------------------------------------------------------
void NaplpsScopeRenderer::drawBeams() {
    if (beamsDirty) buildBeams();

    ofPushMatrix();
    ofTranslate(canvas.getCenter());
    // scope +Y is up
    ofScale(canvas.height / 2, -canvas.height / 2);
    if (segmentBeams) {
        drawSegmentBeams();
    } else {
        osci.uIntensity = beamIntensity * beamExposure;
        for (auto & beam : beams) {
            swapLines(osci.mesh, beam.mesh);
            osci.uRgb = glm::vec3(beam.color.r, beam.color.g, beam.color.b);
            osci.draw();
            swapLines(osci.mesh, beam.mesh);
        }
    }
    ofPopMatrix();
}

//--------------------------------------------------------------
void NaplpsScopeRenderer::decodeStrokes() {
    strokesDirty = false;
    strokes.clear();
    strokeColors.clear();
    if (x.size() != cycleFrames || tags.size() != x.size()) return;

    XYDecoderSettings settings = decoder;
    settings.width = canvas.width;
    settings.height = canvas.height;
    settings.sampleRate = sampleRate;
    settings.freq = freq;
    for (size_t i = 0; i < tags.size();) {
        // Each piece's samples decoded on their own, so that whatever the
        // effects made of them keeps the piece's colour.
        size_t end = i + 1;
        while (end < tags.size() && tags[end] == tags[i]) end++;
        if (tags[i] != 0 && end - i >= 2) {
            for (auto & line : XYDecoder::decodeCycle(&x[i], &y[i], &z[i], end - i, settings)) {
                strokes.push_back(std::move(line));
                strokeColors.push_back(colors[i]);
            }
        }
        i = end;
    }
}

//--------------------------------------------------------------
void NaplpsScopeRenderer::drawStrokes() {
    if (strokesDirty) decodeStrokes();

    ofPushStyle();
    ofNoFill();
    for (size_t i = 0; i < strokes.size(); i++) {
        ofSetColor(strokeColors[i]);
        strokes[i].draw();
    }
    ofPopStyle();
}

//--------------------------------------------------------------
void NaplpsScopeRenderer::drawLabels() {
    // as TelidonDrawCmd::drawPoints() draws them, at its default thickness
    ofPushStyle();
    ofSetColor(255, 63);
    for (auto & p : labels) ofDrawCircle(p, 2);
    ofPopStyle();
}
