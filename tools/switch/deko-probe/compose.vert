#version 460
layout(location=0) out vec2 uv;
layout(location=1) flat out int panel;
void main() {
    const vec2 corners[6] = vec2[6](vec2(0,0),vec2(0,1),vec2(1,0),vec2(1,0),vec2(0,1),vec2(1,1));
    vec2 corner = corners[gl_VertexID % 6];
    uv = corner;
    panel = gl_VertexID >= 6 ? 1 : 0;
    vec2 p;
    if (panel == 1) p = mix(vec2(-0.85,0.55),vec2(0.85,0.25),corner);
    else p = mix(vec2(-0.92,0.92),vec2(0.92,-0.92),corner);
    gl_Position = vec4(p,0.5,1);
}
