// GLSL ES 1.00, so no #version line: ofAppEGLWindow only makes ES 2 contexts,
// and a Pi 3's GPU can't go past ES 2 anyway.

// these are for the programmable pipeline system and are passed in
// by default from OpenFrameworks
uniform mat4 modelViewMatrix;
uniform mat4 projectionMatrix;
uniform mat4 textureMatrix;
uniform mat4 modelViewProjectionMatrix;

attribute vec4 position;
attribute vec4 color;
attribute vec4 normal;
attribute vec2 texcoord;
// this is the end of the default functionality

varying vec2 varyingtexcoord;

void main()
{
    varyingtexcoord = vec2(texcoord.x, texcoord.y);

    gl_Position = modelViewProjectionMatrix * position;
}
