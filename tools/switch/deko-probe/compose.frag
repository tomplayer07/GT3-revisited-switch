#version 460
layout(location=0) in vec2 uv;
layout(location=1) flat in int panel;
layout(binding=0) uniform sampler2D scene;
layout(location=0) out vec4 outputColor;
void main() {
    outputColor = panel == 1 ? vec4(0.05,0.7,1.0,0.5) : texture(scene,uv);
}
