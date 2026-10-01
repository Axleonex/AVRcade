#version 450

// Test-scene fragment shader (Vulkan port of the former HLSL kPixelShader).

layout(location = 0) in vec3 fragColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(fragColor, 1.0);
}
