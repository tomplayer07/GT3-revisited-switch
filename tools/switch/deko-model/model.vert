#version 460
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 texcoord;
layout(location=3) in vec4 vertexColor;
layout(std140,binding=0) uniform Object { mat4 mvp; mat4 world; vec4 params; vec4 matAmbient; vec4 matDiffuse; vec4 matSpecular; vec4 matEmission; vec4 eyeDirection; vec4 envC0; vec4 envC1; };
layout(std140,binding=1) uniform CarLights { vec4 ambient; vec4 directions[4]; vec4 diffuse[4]; vec4 specular[4]; } lights;
layout(location=0) out vec2 uv;
layout(location=1) out vec4 color;
layout(location=2) out vec3 worldNormal;
layout(location=3) out vec3 materialLight;
layout(location=4) out vec3 worldPosition;
layout(std140,binding=2) uniform CourseFog { vec4 viewZ; vec4 fogColor; vec4 fogParams; vec4 viewX; vec4 viewY; vec4 envSky; vec4 envHorizon; vec4 envGround; vec4 linearFog; vec4 outputParams; } fog;
layout(location=5) noperspective out float fogAmount;
void main() {
    gl_Position=mvp*vec4(position,1);
    if (matAmbient.w<0) gl_Position.z=0; // camera-centred sky, reversed far depth
    worldNormal=mat3(world)*normal;
    worldPosition=(world*vec4(position,1)).xyz;
    fogAmount=0;
    if (fog.fogColor.w>0.5 && matAmbient.w>=0) {
        float W=max(-dot(fog.viewZ,vec4(worldPosition,1)),0.0001);
        if(fog.fogParams.z<0.5 || fog.outputParams.z>0.5) {
            float b=255*clamp(fog.fogParams.y,0,1);
            float F=floor(b+0.1+(255-b)*min(W,fog.fogParams.x)/W);
            fogAmount=1-min(F,255)/255;
        } else fogAmount=clamp((W-fog.linearFog.x)*fog.linearFog.y,0,1)*fog.linearFog.z;
    }
    uv=texcoord; color=vertexColor;
    materialLight=vec3(1);
    if(params.z>6.5 && params.z<7.5) {
        vec3 D=worldPosition-eyeDirection.xyz, n=worldNormal;
        float inv=1/max(length(D),0.000001);
        vec3 R=D-2*dot(D,n)*n;
        if(matAmbient.w<2.5) {
            vec3 x=fog.viewX.xyz,y=fog.viewY.xyz;
            vec3 e=vec3(dot(x,R),dot(y,R),dot(cross(x,y),R))*inv*matDiffuse.w;
            float m=max(length(e+vec3(0,0,1)),0.000001);
            uv=vec2(0.5+0.5*e.x/m,0.5-0.5*e.y/m);
        } else {
            float co=matAmbient.x, sn=matAmbient.y;
            vec3 r=vec3(co*R.x+sn*R.z,R.y,-sn*R.x+co*R.z);
            float a=r.x-r.z,b=r.x+r.z,u;
            if(a>=0 && b>=0) u=r.z/r.x;
            else if(a<0 && b>=0) u=2-r.x/r.z;
            else if(a<0) u=4+r.z/r.x;
            else u=6-r.x/r.z;
            float y=r.y*inv;
            uv=vec2(matDiffuse.x-u/8,matDiffuse.z-matDiffuse.y*(y*y*y+2*y));
        }
        color*=envC0+envC1*min(dot(D,n)*inv,0);
        return;
    }
    if(matAmbient.w>2.5 && params.z<0.5) {
        vec3 D=worldPosition-eyeDirection.xyz;
        vec3 R=D-2*dot(D,worldNormal)*worldNormal;
        float c=max(dot(normalize(lights.directions[0].xyz),R)/max(length(D),0.000001),0);
        vec3 bytes=floor(lights.specular[0].rgb*matSpecular.rgb*(128*matDiffuse.w)*pow(c,matSpecular.w));
        materialLight=mod(bytes,256)/128*matEmission.w;
        return;
    }
    if (matAmbient.w>0.5 && params.z<0.5) {
        float normalLength=length(worldNormal);
        vec3 n=normalLength>0.00001 ? worldNormal/normalLength : vec3(0,1,0);
        vec3 L=normalize(vec3(0.3,0.8,0.5));
        vec3 E=normalize(eyeDirection.xyz);
        vec3 lit;
        if (matAmbient.w>1.5) {
            lit=matEmission.rgb+lights.ambient.rgb*matAmbient.rgb;
            for (int i=0;i<4;++i) {
                if (lights.directions[i].w<0.5) continue;
                vec3 direction=lights.directions[i].xyz;
                float directionLength=length(direction);
                if (directionLength<0.00001) continue;
                vec3 Li=direction/directionLength;
                lit+=lights.diffuse[i].rgb*matDiffuse.rgb*max(dot(n,Li),0);
                if (matSpecular.w>0) {
                    vec3 h=Li+E; float hl=length(h);
                    float term=hl>0.00001 ? max(dot(n,h/hl),0) : 0;
                    lit+=lights.specular[i].rgb*matSpecular.rgb*pow(term,matSpecular.w);
                }
            }
        } else {
            lit=matEmission.rgb+vec3(0.4)*matAmbient.rgb;
            lit+=vec3(0.6)*matDiffuse.rgb*max(dot(n,L),0);
            if (matSpecular.w>0) {
                vec3 halfVector=L+E;
                float halfLength=length(halfVector);
                float term=halfLength>0.00001 ? max(dot(n,halfVector/halfLength),0) : 0;
                lit+=vec3(0.6)*matSpecular.rgb*pow(term,matSpecular.w);
            }
        }
        materialLight=clamp(lit*matDiffuse.w,vec3(0),vec3(255.0/128.0))*matEmission.w;
    }
}
