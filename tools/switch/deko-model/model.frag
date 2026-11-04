#version 460
layout(location=0) in vec2 uv;
layout(location=1) in vec4 color;
layout(location=2) in vec3 worldNormal;
layout(location=3) in vec3 materialLight;
layout(location=4) in vec3 worldPosition;
layout(std140,binding=2) uniform CourseFog { vec4 viewZ; vec4 fogColor; vec4 fogParams; } fog;
layout(location=5) noperspective in float fogAmount;
layout(std140,binding=0) uniform Object { mat4 mvp; mat4 world; vec4 params; vec4 matAmbient; vec4 matDiffuse; vec4 matSpecular; vec4 matEmission; vec4 eyeDirection; vec4 envC0; vec4 envC1; };
layout(binding=0) uniform sampler2D image;
layout(binding=1) uniform sampler2D envPX;
layout(binding=2) uniform sampler2D envNX;
layout(binding=3) uniform sampler2D envPY;
layout(binding=4) uniform sampler2D envNY;
layout(binding=5) uniform sampler2D envPZ;
layout(binding=6) uniform sampler2D envNZ;
vec3 sceneEnvironment(vec3 r) {
    vec3 a=abs(r); vec2 q;
    if (a.x>=a.y && a.x>=a.z) {
        q=vec2(r.z*(r.x>0 ? 1 : -1),-r.y)/max(a.x,0.00001)*0.5+0.5;
        return r.x>0 ? texture(envPX,q).rgb : texture(envNX,q).rgb;
    }
    if (a.y>=a.z) {
        q=vec2(r.x,-r.z*(r.y>0 ? 1 : -1))/max(a.y,0.00001)*0.5+0.5;
        return r.y>0 ? texture(envPY,q).rgb : texture(envNY,q).rgb;
    }
    q=vec2(-r.x*(r.z>0 ? 1 : -1),-r.y)/max(a.z,0.00001)*0.5+0.5;
    return r.z>0 ? texture(envPZ,q).rgb : texture(envNZ,q).rgb;
}
layout(location=0) out vec4 outputColor;
void main() {
    if (params.z>4.5) {
        // 9x9x9 colour lattice; large channel spacing survives screenshot JPEG.
        float id=params.y;
        vec3 digits=vec3(mod(id,9),mod(floor(id/9),9),mod(floor(id/81),9));
        outputColor=vec4((vec3(15)+28*digits)/255,1);
        return;
    }
    if (params.z>2.5) {
        float alpha=texture(image,uv).a;
        outputColor=params.z<3.5 ? vec4(vec3(alpha),1) : vec4(fract(uv),0,1);
        return;
    }
    if (params.z>1.5) {
        vec3 n=worldNormal/max(length(worldNormal),0.00001);
        if (!gl_FrontFacing) n=-n;
        vec3 offset=worldPosition-eyeDirection.xyz;
        vec3 view=offset/max(length(offset),0.00001);
        float vn=min(dot(view,n),0);
        vec3 weight=envC0.w>0.5 ? envC0.rgb+envC1.rgb*vn : vec3(1+0.8*vn);
        vec3 reflected=reflect(view,n);
        float t=clamp(reflected.y*0.5+0.5,0,1);
        vec3 ground=vec3(0.18,0.17,0.16), horizon=vec3(0.75,0.78,0.82), sky=vec3(0.35,0.55,0.85);
        vec3 environment=t<0.5 ? mix(ground,horizon,t*2) : mix(horizon,sky,t*2-1);
        if (eyeDirection.w>0.5) environment=sceneEnvironment(reflected);
        float alpha=color.a*(params.x>0.5 ? texture(image,uv).a : 1);
        if (matAmbient.w>2.5 && alpha>254.0/255.0) discard;
        outputColor=vec4(environment*clamp(color.rgb*weight,vec3(0),vec3(255.0/128.0))*envC1.w*(1-clamp(fogAmount,0,1)),alpha);
        return;
    }
    vec4 texel=params.x>0.5 ? texture(image,uv) : vec4(1);
    vec4 c=texel*color;
    if(params.y>=0 && c.a<=params.y) discard;
    float n=length(worldNormal);
    float diffuse=n>0.00001 ? max(dot(worldNormal/n,normalize(vec3(0.3,0.8,0.5))),0) : 1;
    float light=params.z>0.5 ? 1 : (0.4+0.6*diffuse);
    vec3 lit=(matAmbient.w>0.5 && params.z<0.5) ? materialLight : vec3(light);
    outputColor=vec4(mix(c.rgb*params.w*lit,fog.fogColor.rgb,clamp(fogAmount,0,1)),c.a);
}
