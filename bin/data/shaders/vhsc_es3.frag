// GLSL ES 1.00, so no #version line: ofAppEGLWindow only makes ES 2 contexts,
// and a Pi 3's GPU can't go past ES 2 anyway.

precision highp float;

uniform sampler2D tex0;

varying vec2 varyingtexcoord;

const float gamma = 1.2;
// ES has no rectangle textures, so tex0 is sampled 0..1 rather than in pixels:
// this is the gl3 version's 8 pixel offset over the 720x540 fbo.
const vec2 texelSize = vec2(8.0 / 720.0, 8.0 / 540.0);
const float posterizeLevels = 16.0; //90.0;

// GLSL ES 1.00 has no array constructors
const float kernel0 = 0.10;
const float kernel1 = 0.20;
const float kernel2 = 0.40;
const float kernel3 = 0.20;
const float kernel4 = 0.10;

float getLuminance(vec3 col) {
    return dot(col, vec3(0.299, 0.587, 0.114));
}

float map(float s, float a1, float a2, float b1, float b2) {
    return b1 + (s - a1) * (b2 - b1) / (a2 - a1);
}

vec3 adjustGamma(vec3 color, float gamma) {
    return pow(color, vec3(1.0 / gamma));
}

void main() {
    vec2 uv = varyingtexcoord;

    vec3 centerColor = texture2D(tex0, uv).xyz;
    vec3 leftColor = texture2D(tex0, uv - vec2(texelSize.x, 0.0)).xyz;
    vec3 rightColor = texture2D(tex0, uv + vec2(texelSize.x, 0.0)).xyz;
    vec3 topColor = texture2D(tex0, uv + vec2(0.0, texelSize.y)).xyz;
    vec3 bottomColor = texture2D(tex0, uv - vec2(0.0, texelSize.y)).xyz;

    vec3 blurredColor = topColor * kernel0 + leftColor * kernel1 + centerColor * kernel2 + rightColor * kernel3 + bottomColor * kernel4;
    vec3 sharpenedColor = blurredColor * 5.0 - (leftColor + rightColor + topColor + bottomColor);
    vec3 posterizedColor = floor(sharpenedColor * posterizeLevels) / posterizeLevels;

    gl_FragColor = vec4(posterizedColor, 1.0);
}
