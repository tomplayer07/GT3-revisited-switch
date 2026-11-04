#version 460
layout(location=0) in vec3 color;
layout(location=1) in vec2 uv;
layout(location=2) flat in int textured;
layout(binding=0) uniform sampler2D sourceTexture;
layout(location=0) out vec4 outputColor;
void main() {
    outputColor = textured == 1 ? texture(sourceTexture,uv) : vec4(color,1);
}
