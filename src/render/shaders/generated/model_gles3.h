#pragma once

namespace gt3r::render::shaders {

inline constexpr char model_vert_gles3[] = R"(#version 300 es
precision highp float;
precision highp int;

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec4 a_color;

layout(std140) uniform VertexUniforms {
    mat4 mvp;
    mat4 model;      // object -> world
    mat4 normal_mat; // inverse-transpose of model (upper 3x3 used)
    vec4 view_z;     // the view matrix's z row: eye-space z = dot(xyz, world) + w
    vec4 fog;        // GT3 fog (z = 1): x distance A, y 1 - minimum visibility b
} vu;

layout(std140) uniform VertexLighting {
    vec4 eye_dir;
    vec4 light_dir[4];
    vec4 light_diffuse[4];
    vec4 light_specular[4];
    vec4 light_ambient;
    vec4 mat_ambient;
    vec4 mat_diffuse;
    vec4 mat_specular;
    vec4 mat_emission;
    vec4 env_rows[3];
    vec4 env_c0;
    vec4 env_c1;
    vec4 env_pano;
} l;

out vec3 v_normal;
out vec2 v_uv;
out vec4 v_color;
out vec3 v_world;
out vec3 v_lit_w;
out float v_fog_w;

vec3 v_lit;
float v_fog;

void main() {
    gl_Position = vu.mvp * vec4(a_pos, 1.0);
    // Map depth from [0, 1] (reversed depth: 1 near, 0 far) to OpenGL NDC [-1, 1]
    gl_Position.z = gl_Position.z * 2.0 - gl_Position.w;
    v_world = (vu.model * vec4(a_pos, 1.0)).xyz;
    v_normal = mat3(vu.normal_mat) * a_normal;
    v_uv = a_uv;
    v_color = a_color;
    v_fog = 0.0;
    if (vu.fog.z > 0.5) {
        float W = max(-(dot(vu.view_z.xyz, v_world) + vu.view_z.w), 1e-4);
        float b = clamp(255.0 * (1.0 - vu.fog.y), 0.0, 255.0);
        float F = floor(b + 0.1 + (255.0 - b) * min(W, vu.fog.x) / W);
        v_fog = 1.0 - min(F, 255.0) / 255.0;
    }
    v_lit = vec3(1.0);
    if (l.eye_dir.w > 2.5) {
        vec3 n = v_normal;
        vec3 D = v_world - l.eye_dir.xyz;
        float p = 1.0 / max(length(D), 1e-6);
        vec3 R = D - 2.0 * dot(D, n) * n;
        if (l.eye_dir.w < 3.5) {
            vec3 e = vec3(dot(l.env_rows[0].xyz, R), dot(l.env_rows[1].xyz, R), dot(l.env_rows[2].xyz, R)) * p;
            float m = max(length(e + vec3(0.0, 0.0, 1.0)), 1e-6);
            v_uv = vec2(0.5 + 0.5 * e.x / m, 0.5 - 0.5 * e.y / m);
        } else {
            vec3 r = R.x * l.env_rows[0].xyz + R.y * l.env_rows[1].xyz + R.z * l.env_rows[2].xyz;
            float a = r.x - r.z, b = r.x + r.z, u_val;
            if (a >= 0.0 && b >= 0.0) u_val = r.z / r.x;
            else if (a < 0.0 && b >= 0.0) u_val = 2.0 - r.x / r.z;
            else if (a < 0.0) u_val = 4.0 + r.z / r.x;
            else u_val = 6.0 - r.x / r.z;
            float y = r.y * p;
            v_uv = vec2(l.env_pano.x - u_val / 8.0, l.env_pano.z - l.env_pano.y * (y * y * y + 2.0 * y));
        }
        v_color = a_color * (l.env_c0 + l.env_c1 * min(dot(D, n) * p, 0.0));
    } else if (l.eye_dir.w > 1.5) {
        vec3 n = v_normal;
        vec3 D = v_world - l.eye_dir.xyz;
        vec3 R = D - 2.0 * dot(D, n) * n;
        float c = max(dot(normalize(l.light_dir[0].xyz), R) / max(length(D), 1e-6), 0.0);
        vec3 byte_ = floor(l.light_specular[0].rgb * l.mat_specular.rgb * (128.0 * l.mat_diffuse.w) *
                           pow(c, l.mat_specular.w));
        v_lit = mod(byte_, 256.0) / 128.0 * l.mat_emission.w;
    } else if (l.eye_dir.w > 0.5) {
        vec3 n = normalize(v_normal);
        vec3 E = normalize(l.eye_dir.xyz);
        vec3 lit = l.mat_emission.rgb + l.light_ambient.rgb * l.mat_ambient.rgb;
        for (int i = 0; i < 4; ++i) {
            if (l.light_dir[i].w < 0.5) continue;
            vec3 L = normalize(l.light_dir[i].xyz);
            lit += l.light_diffuse[i].rgb * l.mat_diffuse.rgb * max(dot(n, L), 0.0);
            if (l.mat_specular.w > 0.0)
                lit += l.light_specular[i].rgb * l.mat_specular.rgb *
                       pow(max(dot(n, normalize(L + E)), 0.0), l.mat_specular.w);
        }
        v_lit = clamp(lit * l.mat_diffuse.w, 0.0, 255.0 / 128.0) * l.mat_emission.w;
    }
    v_lit_w = v_lit * gl_Position.w;
    v_fog_w = v_fog * gl_Position.w;
}
)";

inline constexpr char model_frag_gles3[] = R"(#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;
precision highp samplerCube;

in vec3 v_normal;
in vec2 v_uv;
in vec4 v_color;
in vec3 v_world;
in vec3 v_lit_w;
in float v_fog_w;
vec3 v_lit;
float v_fog;

uniform sampler2D u_tex;
uniform samplerCube u_env;
uniform sampler2D u_sphere;

layout(std140) uniform FragmentUniforms {
    vec4 light_dir;
    vec4 camera_pos;
    vec4 params;
    vec4 flat_color;
    vec4 fog_color;
    vec4 fog_params;
    vec4 tint;
    vec4 view_row0;
    vec4 view_row1;
    vec4 grade;
    vec4 light_ambient;
    vec4 light_diffuse;
    vec4 env_sky;
    vec4 env_horizon;
    vec4 env_ground;
    vec4 light_specular;
    vec4 mat_ambient;
    vec4 mat_diffuse;
    vec4 mat_specular;
    vec4 mat_emission;
    vec4 light2_dir;
    vec4 light2_diffuse;
    vec4 light2_specular;
    vec4 light3_dir;
    vec4 light3_diffuse;
    vec4 light3_specular;
    vec4 light4_dir;
    vec4 light4_diffuse;
    vec4 light4_specular;
    vec4 grade2;
    vec4 env_c0;
    vec4 env_c1;
    vec4 output_mode;
} fu;

vec3 env_weight(vec3 view, vec3 n) {
    float vn = min(dot(view, n), 0.0);
    if (fu.env_c0.w > 0.5) return fu.env_c0.rgb + fu.env_c1.rgb * vn;
    return vec3(1.0 + fu.env_sky.w * vn);
}

layout(location = 0) out vec4 o_color;

vec3 env_color(vec3 dir) {
    float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    return t < 0.5 ? mix(fu.env_ground.rgb, fu.env_horizon.rgb, t * 2.0)
                   : mix(fu.env_horizon.rgb, fu.env_sky.rgb, t * 2.0 - 1.0);
}

vec3 grade_table(float i, float inv) {
    float t = (i / 255.0 - fu.grade2.x) * inv;
    if (i > 0.5) t = clamp(t, 0.0, 1.0);
    vec3 g = clamp(fu.grade.rgb, 0.0, 1.0);
    return floor(clamp(255.0 * (t * t + 2.0 * g * t * (1.0 - t)), 0.0, 255.0));
}

vec3 gt3_grade(vec3 c) {
    vec3 b = floor(clamp(c, 0.0, 1.0) * 255.0 + 0.5);
    float d = fu.grade2.y - fu.grade2.x;
    float inv = d == 0.0 ? 0.0 : 1.0 / d;
    vec3 lr = grade_table(b.r, inv), lg = grade_table(b.g, inv), lb = grade_table(b.b, inv);
    float sp = min(128.0 * fu.grade.w, 255.0);
    float m = max(128.0 - sp, 0.0);
    vec3 w = vec3(0.30, 0.59, 0.11);
    vec3 off = floor(m * w), diag = floor(clamp(sp + m * w, 0.0, 255.0));
    vec3 r = floor(lr * vec3(diag.r, off.r, off.r) / 128.0);
    vec3 gg = floor(lg * vec3(off.g, diag.g, off.g) / 128.0);
    vec3 bb = floor(lb * vec3(off.b, off.b, diag.b) / 128.0);
    return min(r + gg + bb, vec3(255.0)) / 255.0;
}

float fog_amount(float d) {
    if (fu.fog_params.w > 0.5) return v_fog;
    return clamp((d - fu.fog_params.x) * fu.fog_params.y, 0.0, 1.0) * fu.fog_params.z;
}

void shade() {
    if (fu.params.w > 4.5) {
        vec3 c = texture(u_tex, v_uv).rgb;
        if (fu.grade2.z > 0.5) c = gt3_grade(c);
        o_color = vec4(c, fu.tint.a);
        return;
    }
    if (fu.params.w > 0.5 && fu.params.w < 1.5) {
        o_color = fu.flat_color;
        return;
    }
    if (fu.params.w > 3.5) {
        vec3 n = normalize(v_normal);
        if (!gl_FrontFacing) n = -n;
        vec3 view = normalize(v_world - fu.camera_pos.xyz);
        vec3 rw = reflect(view, n);
        vec3 r = vec3(dot(fu.view_row0.xyz, rw), dot(fu.view_row1.xyz, rw),
                      dot(cross(fu.view_row0.xyz, fu.view_row1.xyz), rw));
        float m = max(length(r + vec3(0.0, 0.0, 1.0)), 1e-4);
        vec2 uv = vec2(0.5 + 0.5 * r.x / m, 0.5 - 0.5 * r.y / m);
        vec4 vc = vec4(min(v_color.rgb * env_weight(view, n), vec3(255.0 / 128.0)), v_color.a);
        vec4 c = texture(u_tex, uv) * vc * fu.tint;
        if (fu.params.z > 0.5) c.a = v_color.a * fu.tint.a;
        o_color = c;
        if (fu.grade2.w > 0.5) o_color.a = 1.0;
        return;
    }
    if (fu.params.w > 2.5) {
        vec3 n = normalize(v_normal);
        if (!gl_FrontFacing) n = -n;
        vec3 view = normalize(v_world - fu.camera_pos.xyz);
        vec3 w = env_weight(view, n);
        vec3 r = reflect(view, n);
        if (fu.env_ground.w > 1.5 && fu.env_ground.w < 2.5) {
            vec3 x = fu.view_row0.xyz, y = fu.view_row1.xyz, z = cross(x, y);
            vec3 e = vec3(dot(x, r), dot(y, r), dot(z, r));
            float th = acos(clamp(e.z, -1.0, 1.0)), ph = atan(e.y, e.x);
            float th2 = 2.0 * asin(0.9 * sin(0.5 * th));
            vec3 d = vec3(sin(th2) * cos(ph), sin(th2) * sin(ph), cos(th2));
            d = vec3(d.x, 0.966 * d.y + 0.259 * d.z, -0.259 * d.y + 0.966 * d.z);
            r = x * d.x + y * d.y + z * d.z;
        }
        vec3 env = fu.env_ground.w > 0.5 ? texture(u_env, r).rgb : env_color(r);
        if (fu.env_ground.w > 2.5) {
            vec3 x = fu.view_row0.xyz, y = fu.view_row1.xyz;
            vec3 e = vec3(dot(x, r), dot(y, r), dot(cross(x, y), r));
            float m = max(length(e + vec3(0.0, 0.0, 1.0)), 1e-4);
            env = texture(u_sphere, vec2(0.5 + 0.5 * e.x / m, 0.5 - 0.5 * e.y / m)).rgb;
        }
        vec3 rgb = env * min(v_color.rgb * w, vec3(255.0 / 128.0)) * fu.params.z;
        if (fu.fog_color.w > 0.0) {
            float d = length(v_world - fu.camera_pos.xyz);
            rgb *= 1.0 - fog_amount(d);
        }
        o_color = vec4(rgb, v_color.a * texture(u_tex, v_uv).a * fu.tint.a);
        return;
    }
    vec4 c = texture(u_tex, v_uv) * v_color;
    if (fu.params.w > 1.5) {
        c.rgb = texture(u_tex, v_uv).rgb * v_lit;
        if (fu.fog_color.w > 0.0) {
            float d = length(v_world - fu.camera_pos.xyz);
            c.rgb *= 1.0 - fog_amount(d);
        }
        o_color = c;
        return;
    }
    if (fu.params.x >= 0.0 && (fu.params.x < 2.0 ? c.a <= fu.params.x : c.a > fu.params.x - 2.0)) discard;
    if (fu.params.y < 0.5) {
        vec3 n = normalize(v_normal);
        if (!gl_FrontFacing) n = -n;
        vec3 L = normalize(fu.light_dir.xyz);
        float diffuse = max(dot(n, L), 0.0);
        if (fu.mat_ambient.w > 0.5) {
            c.rgb *= v_lit;
        } else if (fu.light_ambient.w > 0.5) {
            vec3 lit = fu.light_ambient.rgb + fu.light_diffuse.rgb * diffuse;
            if (fu.light2_dir.w > 0.5) lit += fu.light2_diffuse.rgb * max(dot(n, normalize(fu.light2_dir.xyz)), 0.0);
            if (fu.light3_dir.w > 0.5) lit += fu.light3_diffuse.rgb * max(dot(n, normalize(fu.light3_dir.xyz)), 0.0);
            if (fu.light4_dir.w > 0.5) lit += fu.light4_diffuse.rgb * max(dot(n, normalize(fu.light4_dir.xyz)), 0.0);
            c.rgb *= min(lit, vec3(2.0));
        }
        else
            c.rgb *= fu.light_dir.w + (1.0 - fu.light_dir.w) * diffuse;
        if (fu.params.z > 0.0) {
            vec3 view = normalize(v_world - fu.camera_pos.xyz);
            vec3 r = reflect(view, n);
            c.rgb = mix(c.rgb, env_color(r), fu.params.z);
        }
    }
    if (fu.fog_color.w > 0.0) {
        float d = length(v_world - fu.camera_pos.xyz);
        float f = fog_amount(d);
        c.rgb = mix(c.rgb, fu.fog_color.rgb, f);
    }
    o_color = c * fu.tint;
    if (fu.grade2.w > 0.5) o_color.a = 1.0;
}

void main() {
    v_lit = v_lit_w * gl_FragCoord.w;
    v_fog = v_fog_w * gl_FragCoord.w;
    shade();
    if (fu.output_mode.x > 0.5) o_color.rgb = floor(clamp(o_color.rgb, 0.0, 1.0) * (255.0 / 8.0) + 1e-4) * (8.0 / 255.0);
}
)";

} // namespace gt3r::render::shaders
