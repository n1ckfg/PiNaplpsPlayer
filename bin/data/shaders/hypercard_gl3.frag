#version 150

uniform sampler2DRect tex0;

in vec2 varyingtexcoord;
out vec4 outputColor;

const vec3 white = vec3(0.937, 0.937, 0.937);
const vec3 lightGray = vec3(0.686, 0.686, 0.686);
const vec3 darkGray = vec3(0.376, 0.376, 0.376);
const vec3 black = vec3(0.066, 0.066, 0.066);

// https://stackoverflow.com/questions/12422407/monochrome-dithering-in-javascript-bayer-atkinson-floyd-steinberg
const mat4 ditherPattern = mat4(
    0.059, 0.529, 0.176, 0.647,
    0.765, 0.294, 0.882, 0.412,
    0.235, 0.706, 0.118, 0.588,
    0.941, 0.471, 0.824, 0.353
);

const int matrixSize = 4;
const float finalThreshold = 0.4;

float getLuminance(vec3 col) {
    return dot(col, vec3(0.299, 0.587, 0.114));
}

void main() {
    vec2 uv = varyingtexcoord;

    vec3 texColor = texture(tex0, uv).xyz;
    
    float texGray = getLuminance(texColor);
    int paletteIndex = int(texGray * 15.0);
    int x = int(uv.x) % matrixSize;
    int y = int(uv.y) % matrixSize;
    int ditherIndex = int(ditherPattern[x][y] * 15.0);
    paletteIndex = min(paletteIndex + ditherIndex, 15);
    vec3 ditheredColor = vec3(
        float((paletteIndex & 4) >> 2),
        float((paletteIndex & 2) >> 1),
        float(paletteIndex & 1)
    );

    float texGray2 = texGray - 0.15 * getLuminance(ditheredColor);

    vec3 color;
    if (texGray2 < finalThreshold) {
        color = black;
    } else {
        color = white;
    }

    outputColor = vec4(color, 1.0);
}


