#version 450

// Test-scene vertex shader (Vulkan port of the former HLSL kVertexShader).
//
// stereo_math.cpp uses an honest COLUMN-vector convention: makeViewMatrix
// returns the true world->eye view V = [R^T | -R^T p], makeProjectionMatrix
// returns the GL-style projection P (clip z in [-1, 1]), and the renderer
// uploads viewProjection = P * V (multiply(projection, view)). So we apply it
// directly as a column-vector transform: clip = viewProjection * vec4(pos,1).
// We then apply the GL->Vulkan clip-space correction in-shader:
//   * Vulkan clip space is Y-down  -> negate Y.
//   * Vulkan depth range is [0, 1] -> remap z' = (z + w) * 0.5.
// This keeps the test scene correct and validation clean; the same V and P are
// what the external render callback receives (standard column-vector matrices).

layout(set = 0, binding = 0) uniform SceneConstants {
    mat4 viewProjection;  // column-major P*V, as built by stereo_math
    vec4 tint;
} constants;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

void main() {
    // Column-vector transform: viewProjection = P * V (built by the renderer),
    // applied directly to the world-space position.
    vec4 clip = constants.viewProjection * vec4(inPosition, 1.0);

    // GL -> Vulkan clip-space correction.
    clip.y = -clip.y;
    clip.z = (clip.z + clip.w) * 0.5;

    gl_Position = clip;
    fragColor = inColor * constants.tint.rgb;
}
