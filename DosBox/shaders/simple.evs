#version 100

attribute mediump vec2 position;

varying mediump vec2 texcoord;

void main()
{
    gl_Position = vec4(position, 0.0, 1.0);
    texcoord = position * vec2(0.5,-0.5) + vec2(0.5);
}
