#version 460
layout(location=0) out vec3 color;
layout(location=1) out vec2 uv;
layout(location=2) flat out int textured;
void main() {
    const vec2 pos[10] = vec2[10](
        vec2(-0.45,-0.3),vec2(0.45,-0.3),vec2(-0.10,0.5),
        vec2(-0.8,-0.65),vec2(0.8,-0.65),vec2(0.15,0.8),
        vec2(0.15,0.65),vec2(0.15,-0.65),vec2(0.9,0.65),vec2(0.9,-0.65));
    const vec3 rgb[3] = vec3[3](vec3(1,0.15,0.1),vec3(0.1,1,0.2),vec3(0.1,0.3,1));
    const vec2 coords[4] = vec2[4](vec2(0,0),vec2(0,1),vec2(1,0),vec2(1,1));
    bool nearTriangle = gl_VertexID < 3;
    textured = gl_VertexID >= 6 ? 1 : 0;
    vec2 p = pos[gl_VertexID];
    if (textured == 0) p.x = p.x * 0.5 - 0.5;
    gl_Position = vec4(p,textured == 1 ? 0.5 : (nearTriangle ? 0.8 : 0.2),1);
    color = nearTriangle ? rgb[gl_VertexID % 3] : vec3(1,0.8,0.05);
    uv = textured == 1 ? coords[gl_VertexID - 6] : vec2(0);
}
