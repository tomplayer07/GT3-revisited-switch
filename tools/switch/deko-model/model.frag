#version 460
layout(location=0) in vec2 uv;
layout(location=1) in vec4 color;
layout(location=2) in vec3 worldNormal;
layout(location=3) in vec3 materialLight;
layout(location=4) in vec3 worldPosition;
layout(std140,binding=2) uniform CourseFog { vec4 viewZ; vec4 fogColor; vec4 fogParams; vec4 viewX; vec4 viewY; vec4 envSky; vec4 envHorizon; vec4 envGround; vec4 linearFog; vec4 outputParams; } fog;
layout(location=5) noperspective in float fogAmount;
layout(std140,binding=0) uniform Object { mat4 mvp; mat4 world; vec4 params; vec4 matAmbient; vec4 matDiffuse; vec4 matSpecular; vec4 matEmission; vec4 eyeDirection; vec4 envC0; vec4 envC1; };
layout(binding=0) uniform sampler2D image;
layout(binding=1) uniform sampler2D envPX;
layout(binding=2) uniform sampler2D envNX;
layout(binding=3) uniform sampler2D envPY;
layout(binding=4) uniform sampler2D envNY;
layout(binding=5) uniform sampler2D envPZ;
layout(binding=6) uniform sampler2D envNZ;
layout(binding=7) uniform sampler2D showroomSphere;
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
vec4 sampleImage(vec2 coords) {
    if(fog.outputParams.w<0.5) return texture(image,coords);
    ivec2 size=textureSize(image,0);
    vec2 t=coords*vec2(size)-0.5,i=floor(t),f=min(floor((t-i)*16+0.001),vec2(15));
    ivec2 p0=clamp(ivec2(i),ivec2(0),size-1),p1=clamp(ivec2(i)+1,ivec2(0),size-1);
    vec4 a=floor(texelFetch(image,p0,0)*255+0.5),b=floor(texelFetch(image,ivec2(p1.x,p0.y),0)*255+0.5);
    vec4 c=floor(texelFetch(image,ivec2(p0.x,p1.y),0)*255+0.5),d=floor(texelFetch(image,p1,0)*255+0.5);
    vec4 top=a+floor((b-a)*f.x/16),bottom=c+floor((d-c)*f.x/16);
    return (top+floor((bottom-top)*f.y/16))/255;
}
// Precomputed exact byte contributions; no filtered LUT sampling.
vec3 gt3_grade(vec3 c) {
    ivec3 bytes=ivec3(floor(clamp(c,0.0,1.0)*255.0+0.5));
    vec3 r=floor(texelFetch(showroomSphere,ivec2(bytes.r,0),0).rgb*255.0+0.5);
    vec3 g=floor(texelFetch(showroomSphere,ivec2(bytes.g,1),0).rgb*255.0+0.5);
    vec3 b=floor(texelFetch(showroomSphere,ivec2(bytes.b,2),0).rgb*255.0+0.5);
    return min(r+g+b,vec3(255.0))/255.0;
}

vec4 finishColor(vec4 c) {
    if(fog.outputParams.x>0.5) c.rgb=floor(clamp(c.rgb,0,1)*255/8)*8/255;
    return c;
}
vec3 compressedEnvironmentDirection(vec3 e) {
    float z=clamp(e.z,-1.0,1.0),xy2=dot(e.xy,e.xy);
    if(xy2>1e-12) {
        float s=0.9*sqrt(max(0.0,(1.0-z)*0.5));
        float radial=2.0*s*sqrt(max(0.0,1.0-s*s));
        return vec3(e.xy*(radial*inversesqrt(xy2)),1.0-2.0*s*s);
    }
    // Keep the original pole/seam behavior, avoiding normalization of tiny xy.
    float theta=2.0*asin(0.9*sin(acos(z)*0.5)),phi=atan(e.y,e.x);
    return vec3(sin(theta)*cos(phi),sin(theta)*sin(phi),cos(theta));
}
void main() {
    if(fog.linearFog.w>0 && -dot(fog.viewZ,vec4(worldPosition,1))>fog.linearFog.w) discard;
    float objectAlpha=fog.fogParams.z>0.5 ? fog.viewY.w : 1;
#ifndef GT3R_COMMON_FRAGMENT
    if(params.z>7.5) {
        vec4 c=sampleImage(uv); outputColor=vec4(gt3_grade(c.rgb),c.a); return;
    }
    if(params.z>6.5) {
        vec4 c=sampleImage(uv)*color;
        if(params.y>=0 && c.a<=params.y) discard;
        outputColor=finishColor(vec4(mix(c.rgb*params.w,fog.fogColor.rgb,clamp(fogAmount,0,1)),c.a*objectAlpha));
        return;
    }
    if (params.z>5.5) {
        vec3 n=worldNormal/max(length(worldNormal),0.00001);
        if(!gl_FrontFacing) n=-n;
        vec3 offset=worldPosition-eyeDirection.xyz;
        vec3 viewer=offset/max(length(offset),0.00001);
        vec3 reflected=reflect(viewer,n);
        vec3 x=fog.viewX.xyz, y=fog.viewY.xyz;
        vec3 e=vec3(dot(x,reflected),dot(y,reflected),dot(cross(x,y),reflected));
        float m=max(length(e+vec3(0,0,1)),0.0001);
        vec4 texel=texture(image,vec2(0.5+0.5*e.x/m,0.5-0.5*e.y/m));
        vec3 weight=envC0.w>0.5 ? envC0.rgb+envC1.rgb*min(dot(viewer,n),0) : vec3(1);
        float alpha=color.a*(eyeDirection.w>0.5 ? 1 : texel.a)*objectAlpha;
        if(params.y>=0 && alpha<=params.y) discard;
        outputColor=finishColor(vec4(texel.rgb*clamp(color.rgb*weight,vec3(0),vec3(255.0/128.0))*params.w,alpha));
        return;
    }
    if (params.z>4.5) {
        // 9x9x9 colour lattice; large channel spacing survives screenshot JPEG.
        float id=params.y;
        vec3 digits=vec3(mod(id,9),mod(floor(id/9),9),mod(floor(id/81),9));
        outputColor=finishColor(vec4((vec3(15)+28*digits)/255,1));
        return;
    }
    if (params.z>2.5) {
        float alpha=sampleImage(uv).a;
        outputColor=finishColor(params.z<3.5 ? vec4(vec3(alpha),1) : vec4(fract(uv),0,1));
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
        if (eyeDirection.w>0.5) {
            vec3 lookup=reflected;
            if(fog.outputParams.y>0.5) {
                vec3 x=fog.viewX.xyz,y=fog.viewY.xyz,z=cross(x,y);
                vec3 e=vec3(dot(x,lookup),dot(y,lookup),dot(z,lookup));
                vec3 d=compressedEnvironmentDirection(e);
                d=vec3(d.x,0.966*d.y+0.259*d.z,-0.259*d.y+0.966*d.z);
                lookup=x*d.x+y*d.y+z*d.z;
            }
            environment=sceneEnvironment(lookup);
        }
        if (fog.fogParams.z>0.5) {
            if(eyeDirection.w<0.5) environment=t<0.5 ? mix(fog.envGround.rgb,fog.envHorizon.rgb,t*2) : mix(fog.envHorizon.rgb,fog.envSky.rgb,t*2-1);
            if (fog.fogParams.w>0.5) {
                vec3 x=fog.viewX.xyz, y=fog.viewY.xyz;
                vec3 e=vec3(dot(x,reflected),dot(y,reflected),dot(cross(x,y),reflected));
                float m=max(length(e+vec3(0,0,1)),0.0001);
                environment=texture(showroomSphere,vec2(0.5+0.5*e.x/m,0.5-0.5*e.y/m)).rgb;
            }
        }
        float alpha=color.a*(params.x>0.5 ? sampleImage(uv).a : 1);
        if (matAmbient.w>2.5 && alpha>254.0/255.0) discard;
        outputColor=finishColor(vec4(environment*clamp(color.rgb*weight,vec3(0),vec3(255.0/128.0))*envC1.w*(1-clamp(fogAmount,0,1)),alpha*objectAlpha));
        return;
    }
#endif
    vec4 texel=params.x>0.5 ? sampleImage(uv) : vec4(1);
    vec4 c=texel*color;
    if(params.y>=0 && c.a<=params.y) discard;
    float n=length(worldNormal);
    float diffuse=n>0.00001 ? max(dot(worldNormal/n,normalize(vec3(0.3,0.8,0.5))),0) : 1;
    float light=params.z>0.5 ? 1 : (0.4+0.6*diffuse);
    vec3 lit=(matAmbient.w>0.5 && params.z<0.5) ? materialLight : vec3(light);
    outputColor=finishColor(vec4(mix(c.rgb*params.w*lit,fog.fogColor.rgb,clamp(fogAmount,0,1)),c.a*objectAlpha));
}
