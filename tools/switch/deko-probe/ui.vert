#version 460
layout(std140,binding=0) uniform UiTriangle { vec4 positions[3]; vec4 colors[3]; vec4 params; };
layout(location=0) out vec4 color;
layout(location=1) out vec2 uv;
layout(location=2) flat out int textured;
void main() {
    gl_Position = vec4(positions[gl_VertexID].xy,0.5,1);
    color = colors[gl_VertexID];
    uv = positions[gl_VertexID].zw;
    textured = params.x > 0.5 ? 1 : 0;
}
