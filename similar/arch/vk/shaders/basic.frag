#version 450

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    float alpha_ref;
    float pad[3];
} pc;

layout(location = 0) in vec4 fragColor;
layout(location = 0) out vec4 outColor;

void main() {
    if (fragColor.a < pc.alpha_ref)
        discard;
    outColor = fragColor;
}
