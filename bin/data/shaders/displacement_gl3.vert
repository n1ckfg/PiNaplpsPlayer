#version 150

uniform mat4 modelViewMatrix;
uniform mat4 projectionMatrix;
uniform mat4 textureMatrix;
uniform mat4 modelViewProjectionMatrix;

in vec4 position;
in vec2 texcoord;
in vec4 normal;
in vec4 color;

uniform sampler2DRect tex0;
out vec2 varyingtexcoord;

void main() {
    varyingtexcoord = texcoord;

    // get the position of the vertex relative to the modelViewProjectionMatrix
	vec4 modifiedPosition = modelViewProjectionMatrix * position;
    
    // we need to scale up the values we get from the texture
    float scale = 100.0;
    
    // here we get the red channel value from the texture
    // to use it as vertical displacement
    float displacementY = texture(tex0, varyingtexcoord).x;
		
    // use the displacement we created from the texture data
    // to modify the vertex position
    modifiedPosition.y += displacementY * scale;

    // this is the resulting vertex position
	gl_Position = modifiedPosition;
}
