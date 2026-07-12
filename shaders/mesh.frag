#version 450

#extension GL_GOOGLE_include_directive : require
#include "input_structures.glsl"

layout(location = 0) in vec3 in_normal;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec4 out_frag_color;

void main()
{
    float light_value = max(dot(in_normal, scene_data.sunlight_direction.xyz), 0.1);

    vec3 color = in_color * texture(color_tex, in_uv).xyz;
    vec3 ambient = color * scene_data.ambient_color.xyz;

    vec3 final_color = (color * light_value * scene_data.sunlight_color.w) + ambient;
    out_frag_color = vec4(final_color, 1.0);
}