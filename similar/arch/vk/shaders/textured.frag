#version 450

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    float alpha_ref;
    float pad[3];
} pc;

layout(set = 0, binding = 0) uniform sampler2D texSampler;

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texel = texture(texSampler, fragTexCoord);
    vec4 color = texel * fragColor;
    if (color.a < pc.alpha_ref)
        discard;
    outColor = color;
}
