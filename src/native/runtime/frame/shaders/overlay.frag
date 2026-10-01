#version 450

// Lightweight in-headset debug overlay fragment shader (DIAG-03 partial).
// Emits the solid push-constant color (straight alpha; blended over the scene
// by the overlay pipeline's SRC_ALPHA / ONE_MINUS_SRC_ALPHA color blend state).

layout(push_constant) uniform OverlayPush {
    vec4 color;
    vec4 rect;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = pc.color;
}
