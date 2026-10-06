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
        beamsDirty = true;
        strokesDirty = true;
    }
    return changed;
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

    // Scope units: -1..1 up the canvas, and as far across it as its shape
    // allows, so the beam stays round on any canvas.
    const float aspect = canvas.width / canvas.height;
    std::vector<float> sx(x.size());
    for (size_t i = 0; i < x.size(); i++) sx[i] = x[i] * aspect;
    osci.uSize = beamSize / (canvas.height / 2);

    // OsciMesh joins each run of samples to the end of the last one, lit as
    // the run's first sample. Keep that jump dark.
    std::vector<float> bright(x.size(), 1);
    bright[0] = 0;

    // Each run of lit samples from one piece is one line of the beam, and
    // there's one mesh per colour: the beams add up, so the drawing order
    // doesn't matter.
    std::map<int, size_t> beamOfColor;
    double stepSum = 0;
    size_t steps = 0;
    for (size_t i = 0; i < tags.size();) {
        size_t end = i + 1;
        while (end < tags.size() && tags[end] == tags[i]) end++;
        if (tags[i] != 0 && end - i >= 2) {
            auto found = beamOfColor.find(colors[i].getHex());
            if (found == beamOfColor.end()) {
                found = beamOfColor.emplace(colors[i].getHex(), beams.size()).first;
                osci.clear();
                beams.push_back({ colors[i], osci.mesh });
            }
            std::swap(beams[found->second].mesh, osci.mesh);
            osci.addLines(&sx[i], &y[i], bright.data(), int(end - i));
            std::swap(beams[found->second].mesh, osci.mesh);
            for (size_t j = i + 1; j < end; j++) {
                stepSum += glm::distance(glm::vec2(sx[j - 1], y[j - 1]), glm::vec2(sx[j], y[j]));
                steps++;
            }
        }
        i = end;
    }

    // A beam leaves less light on a line the faster it moves, so a longer
    // drawing or a shorter loop comes out dimmer. Scale the light by the
    // average step, so a stroke peaks at about beamIntensity either way.
    const float sigma = osci.uSize / 3;
    beamExposure = steps > 0 ? float(stepSum / steps) / (sigma * std::sqrt(TWO_PI)) : 1;
}

//--------------------------------------------------------------
void NaplpsScopeRenderer::drawBeams() {
    if (beamsDirty) buildBeams();

    ofPushMatrix();
    ofTranslate(canvas.getCenter());
    // scope +Y is up
    ofScale(canvas.height / 2, -canvas.height / 2);
    osci.uIntensity = beamIntensity * beamExposure;
    for (auto & beam : beams) {
        std::swap(osci.mesh, beam.mesh);
        osci.uRgb = glm::vec3(beam.color.r, beam.color.g, beam.color.b);
        osci.draw();
        std::swap(osci.mesh, beam.mesh);
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
