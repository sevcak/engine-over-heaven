#version 450

#extension GL_GOOGLE_include_directive : require
#include "input_structures.glsl"

layout(location = 0) in vec3 in_normal;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec3 in_world_pos;

layout(location = 0) out vec4 out_frag_color;

const float PI = 3.14159265359;

float trowbridge_reitz_ggx(float NdotH, float alpha)
{
    return alpha * alpha
        / (PI * pow(pow(NdotH, 2.0) * (pow(alpha, 2.0) - 1.0) + 1.0, 2));
}

float G1_ggx(float NdotV, float k)
{
    return NdotV / (NdotV * (1.0 - k) + k);
}

float smith_ggx(float NdotV, float NdotL, float k)
{
    return G1_ggx(NdotV, k) * G1_ggx(NdotL, k);
}

void main()
{
    vec3 L = normalize(scene_data.sunlight_direction.xyz);
    vec3 V = normalize(scene_data.camera_position.xyz - in_world_pos);
    vec3 H = normalize(V + L);
    vec3 N = normalize(in_normal);
    float NdotV = clamp(dot(N, V), 0.0001, 1.0);
    float NdotL = clamp(dot(N, L), 0.0001, 1.0);
    float NdotH = clamp(dot(N, H), 0.0001, 1.0);
    float VdotH = clamp(dot(V, H), 0.0001, 1.0);

    vec3 albedo = in_color * texture(color_tex, in_uv).xyz;
    vec4 metal_rough_sample = texture(metal_rough_tex, in_uv);
    float roughness =
        clamp(metal_rough_sample.g * material_data.metal_rough_factors.y, 0.04, 1.0);
    float metallic =
        clamp(metal_rough_sample.b * material_data.metal_rough_factors.x, 0.0, 1.0);

    float alpha = roughness * roughness;
    float D = trowbridge_reitz_ggx(NdotH, alpha);
    float G = smith_ggx(NdotV, NdotL, alpha / 2.0);
    // Fresnel-Schlick.
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = f0 + (1.0 - f0) * pow(1.0 - VdotH, 5.0);

    vec3 k_d = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = k_d * (albedo / PI);

    vec3 specular = (D * F * G) / (4.0 * NdotV * NdotL);

    vec3 direct_radiance = (diffuse + specular) * NdotL * scene_data.sunlight_color.rgb * scene_data.sunlight_color.w;
    vec3 ambient = scene_data.ambient_color.rgb * albedo;

    vec3 final_color = direct_radiance + ambient;

    out_frag_color = vec4(final_color, 1.0);
}