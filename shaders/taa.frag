#version 450
#extension GL_GOOGLE_include_directive : require
// TAA ligero (sin vectores de movimiento): mezcla el frame actual con el historial,
// limitando el historial al rango de colores del vecindario actual para evitar estelas.
// Suaviza el parpadeo de bordes y el temblor de polígonos típico de PlayStation.
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D uCur;
layout(set = 0, binding = 1) uniform sampler2D uHist;
#include "aa_common.glsl"

vec3 cur(vec2 uv) { return texture(uCur, uv).rgb; }

void main() {
    vec3 c = pc.mode > 0.5 ? fxaa(uCur, vUV, pc.rcp) : cur(vUV);
    // Vecindario 3x3 (en cruz + diagonales) para el recorte del historial
    vec3 mn = c, mx = c;
    for (int y = -1; y <= 1; y++)
        for (int x = -1; x <= 1; x++) {
            vec3 s = cur(vUV + vec2(x, y) * pc.rcp);
            mn = min(mn, s); mx = max(mx, s);
        }
    vec3 h = clamp(texture(uHist, vUV).rgb, mn, mx);
    outColor = vec4(mix(h, c, pc.blend), 1.0);
}
