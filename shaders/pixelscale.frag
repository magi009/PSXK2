#version 450
#extension GL_GOOGLE_include_directive : require
// Escaladores clásicos de pixel art (solo usan colores de la imagen original):
//   pc.mode 2 = Scale2x (AdvanceMAME / EPX), 3 = Scale3x, 4 = Eagle 2x.
// Se dibuja a 2x o 3x del tamaño de origen; cada píxel de salida decide su color por sus vecinos.
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D uSrc;
layout(set = 0, binding = 1) uniform sampler2D uUnused;
#include "aa_common.glsl"

#define T(x, y) textureOffset(uSrc, vUV, ivec2(x, y)).rgb
bool eq(vec3 a, vec3 b) { return all(lessThan(abs(a - b), vec3(2.0 / 255.0))); }

void main() {
    int mode = int(pc.mode + 0.5);
    vec3 A = T(-1,-1), B = T(0,-1), C = T(1,-1);
    vec3 D = T(-1, 0), E = T(0, 0), F = T(1, 0);
    vec3 G = T(-1, 1), H = T(0, 1), I = T(1, 1);
    vec2 sub = fract(vUV / pc.rcp);          // posición dentro del píxel de origen
    vec3 o = E;
    if (mode == 3) {
        ivec2 q = ivec2(min(floor(sub * 3.0), vec2(2.0)));
        int k = q.y * 3 + q.x;
        bool dB = eq(D, B) && !eq(B, F) && !eq(D, H);
        bool bF = eq(B, F) && !eq(B, D) && !eq(F, H);
        bool dH = eq(D, H) && !eq(D, B) && !eq(H, F);
        bool hF = eq(H, F) && !eq(D, H) && !eq(B, F);
        if (k == 0) o = dB ? D : E;
        else if (k == 1) o = (dB && !eq(E, C)) || (bF && !eq(E, A)) ? B : E;
        else if (k == 2) o = bF ? F : E;
        else if (k == 3) o = (dB && !eq(E, G)) || (dH && !eq(E, A)) ? D : E;
        else if (k == 5) o = (bF && !eq(E, I)) || (hF && !eq(E, C)) ? F : E;
        else if (k == 6) o = dH ? D : E;
        else if (k == 7) o = (dH && !eq(E, I)) || (hF && !eq(E, G)) ? H : E;
        else if (k == 8) o = hF ? F : E;
    } else {
        bool left = sub.x < 0.5, top = sub.y < 0.5;
        if (mode == 4) {                     // Eagle: la esquina toma el color si sus tres vecinos coinciden
            if (top && left)  o = eq(D, A) && eq(A, B) ? A : E;
            if (top && !left) o = eq(B, C) && eq(C, F) ? C : E;
            if (!top && left) o = eq(D, G) && eq(G, H) ? G : E;
            if (!top && !left) o = eq(F, I) && eq(I, H) ? I : E;
        } else {                             // Scale2x
            if (top && left)  o = eq(D, B) && !eq(B, F) && !eq(D, H) ? D : E;
            if (top && !left) o = eq(B, F) && !eq(B, D) && !eq(F, H) ? F : E;
            if (!top && left) o = eq(D, H) && !eq(D, B) && !eq(H, F) ? D : E;
            if (!top && !left) o = eq(H, F) && !eq(D, H) && !eq(B, F) ? F : E;
        }
    }
    outColor = vec4(o, 1.0);
}
