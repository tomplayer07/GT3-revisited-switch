#version 460
layout(location=0) in vec4 color;
layout(location=1) in vec2 uv;
layout(location=2) flat in int textured;
layout(binding=0) uniform sampler2D image;
layout(location=0) out vec4 outputColor;
void main() { outputColor = color * (textured == 1 ? texture(image,uv) : vec4(1)); }
