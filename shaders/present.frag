#version 450
#extension GL_GOOGLE_include_directive : require
// Dibuja la imagen del juego en pantalla: copia directa o con FXAA
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D uSrc;
layout(set = 0, binding = 1) uniform sampler2D uUnused;
#include "aa_common.glsl"

void main() {
    vec3 c = pc.mode > 0.5 ? fxaa(uSrc, vUV, pc.rcp) : texture(uSrc, vUV).rgb;
    outColor = vec4(screenFx(c, vUV), 1.0);   // opaco aunque el núcleo deje alfa a 0
}
