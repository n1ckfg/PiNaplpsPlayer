#version 150

uniform sampler2DRect tex0;

in vec2 varyingtexcoord;
out vec4 outputColor;

const float gamma = 1.2;
const vec2 texelSize = vec2(1.0/120.0, 1.0/90.0);
const float posterizeLevels = 90;

const float sigma = 2.0;
const float kernel[5] = float[](0.10, 0.20, 0.40, 0.20, 0.10);

float getLuminance(vec3 col) {
    return dot(col, vec3(0.299, 0.587, 0.114));
}

// https://github.com/dmnsgn/glsl-tone-map
float tone_map_aces(float x) {
  const float a = 2.51;
  const float b = 0.03;
  const float c = 2.43;
  const float d = 0.59;
  const float e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

float map(float s, float a1, float a2, float b1, float b2) {
    return b1 + (s - a1) * (b2 - b1) / (a2 - a1);
}

vec3 adjustGamma(vec3 color, float gamma) {
    return pow(color, vec3(1.0 / gamma));
}

void main() {
    vec2 uv = varyingtexcoord;

    vec3 centerColor = texture(tex0, uv).xyz;   
    vec3 leftColor = texture(tex0, uv - vec2(texelSize.x, 0.0)).xyz;
    vec3 rightColor = texture(tex0, uv + vec2(texelSize.x, 0.0)).xyz;
    vec3 topColor = texture(tex0, uv + vec2(0.0, texelSize.y)).xyz;
    vec3 bottomColor = texture(tex0, uv - vec2(0.0, texelSize.y)).xyz;

    vec3 blurredColor = topColor * kernel[0] + leftColor * kernel[1] + centerColor * kernel[2] + rightColor * kernel[3] + bottomColor * kernel[4];
    vec3 sharpenedColor = blurredColor * 5.0 - (leftColor + rightColor + topColor + bottomColor);
    vec3 posterizedColor = floor(sharpenedColor * posterizeLevels) / posterizeLevels;

    float luminance0 = tone_map_aces(getLuminance(posterizedColor));
    float luminance1 = pow(map(luminance0, 0.05, 0.95, 0.0, 1.0), 1.0 / gamma);

    outputColor = vec4(luminance1, luminance1, luminance1, 1.0);
}
