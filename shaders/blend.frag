#version 450
#extension GL_GOOGLE_include_directive : require
// Frame intermedio para 120 Hz: mezcla el frame anterior y el siguiente (pc.blend = peso del siguiente).
// Sin vectores de movimiento: suaviza el paso entre frames, pero lo que se mueve rápido deja una estela.
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D uPrev;
layout(set = 0, binding = 1) uniform sampler2D uNext;
#include "aa_common.glsl"

void main() {
    vec3 a = pc.mode > 0.5 ? fxaa(uPrev, vUV, pc.rcp) : texture(uPrev, vUV).rgb;
    vec3 b = pc.mode > 0.5 ? fxaa(uNext, vUV, pc.rcp) : texture(uNext, vUV).rgb;
    outColor = vec4(screenFx(mix(a, b, pc.blend), vUV), 1.0);
}
