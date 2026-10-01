#version 450

// Lightweight in-headset debug overlay vertex shader (DIAG-03 partial).
//
// Draws a small solid-color NDC quad for a status/error indicator block. No MVP:
// the incoming vertex is a unit-square corner in [0,1]^2 and the push constant
// carries the destination rect (x0,y0,x1,y1) directly in Vulkan NDC (Y-down), so
// no projection/view and no Y-negate are applied. This keeps the overlay correct
// even when the scene uses identity matrices (headless render-validate harness),
// which is exactly why NDC-space (not clip-space) authoring is required here.

layout(push_constant) uniform OverlayPush {
    vec4 color;  // RGBA, straight alpha (read by the fragment shader)
    vec4 rect;   // (x0, y0, x1, y1) in Vulkan NDC, Y-down
} pc;

layout(location = 0) in vec2 inPos;  // unit-square corner in [0,1]^2

void main() {
    // Map the unit-square corner into the destination NDC rect.
    vec2 ndc = mix(pc.rect.xy, pc.rect.zw, inPos);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
