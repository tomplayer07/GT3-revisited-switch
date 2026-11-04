// Capture shader deliberately has no environment samplers.
#version 460
layout(location=0) in vec2 uv;
layout(location=1) in vec4 color;
layout(location=2) in vec3 worldNormal;
layout(location=3) in vec3 materialLight;
layout(location=4) in vec3 worldPosition;
layout(std140,binding=0) uniform Object { mat4 mvp; mat4 world; vec4 params; vec4 matAmbient; vec4 matDiffuse; vec4 matSpecular; vec4 matEmission; vec4 eyeDirection; vec4 envC0; vec4 envC1; };
layout(binding=0) uniform sampler2D image;
layout(location=0) out vec4 outputColor;
void main() {
    vec4 texel=params.x>0.5 ? texture(image,uv) : vec4(1);
    vec4 c=texel*color;
    if(params.y>=0 && c.a<=params.y) discard;
    float n=length(worldNormal);
    float diffuse=n>0.00001 ? max(dot(worldNormal/n,normalize(vec3(0.3,0.8,0.5))),0) : 1;
    float light=params.z>0.5 ? 1 : (0.4+0.6*diffuse);
    vec3 lit=(matAmbient.w>0.5 && params.z<0.5) ? materialLight : vec3(light);
    outputColor=vec4(c.rgb*params.w*lit,c.a);
}
