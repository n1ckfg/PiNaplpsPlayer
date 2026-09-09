
// these are for the programmable pipeline system
uniform mat4 modelViewProjectionMatrix;
attribute vec4 position;
attribute vec2 texcoord;

uniform sampler2D tex0;

varying vec2 texCoordVarying;

void main()
{
    // get the texture coordinates
    texCoordVarying = texcoord;

    // this is the resulting vertex position
    gl_Position = modelViewProjectionMatrix * position;
}