#version 100

precision mediump float;

varying vec2 fragTexCoord;
varying vec4 fragColor;

uniform sampler2D texture0;
uniform sampler2D texture1;

uniform vec4 colDiffuse;

void main()
{
    vec4 texelColor = texture2D(texture0, fragTexCoord);

    gl_FragColor = vec4(texelColor.g, texelColor.b, texelColor.a, 1.0);
}
