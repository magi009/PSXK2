// Parámetros comunes de los pases de antialiasing
layout(push_constant) uniform PC {
    vec2 rcp;      // 1 / tamaño de la imagen de origen (en texels)
    float mode;    // 0 = copia, 1 = FXAA  (escaladores de píxel: 2 = Scale2x, 3 = Scale3x, 4 = Eagle 2x)
    float blend;   // TAA: peso del frame actual (1 = sin historial)
    float effect;  // efecto de pantalla: 0 ninguno, 1 scanlines, 2 CRT rejilla de apertura, 3 LCD, 4 subpíxel LCD
    float pad0;
    vec2 fxSize;   // resolución original de la PS1 (para que las líneas coincidan con las suyas)
} pc;

// Efectos de pantalla (en la pasada final, a resolución de salida)
vec3 screenFx(vec3 c, vec2 uv) {
    int e = int(pc.effect + 0.5);
    if (e == 0 || pc.fxSize.x <= 0.0) return c;
    vec2 f = fract(uv * pc.fxSize);          // posición dentro del píxel original
    const float PI = 3.14159265;
    if (e == 1) {                            // scanlines: líneas oscuras entre las filas de la PS1
        return min(c * (0.50 + 0.50 * sin(PI * f.y)) * 1.15, 1.0);
    }
    if (e == 2) {                            // CRT con rejilla de apertura (franjas RGB verticales)
        vec3 m = vec3(0.70);
        m[int(mod(gl_FragCoord.x, 3.0))] = 1.0;
        return min(c * (0.58 + 0.42 * sin(PI * f.y)) * m * 1.35, 1.0);
    }
    if (e == 3) {                            // LCD: rejilla fina entre píxeles, como una portátil
        vec2 g = smoothstep(0.0, 0.14, f) * smoothstep(0.0, 0.14, 1.0 - f);
        return min(c * mix(0.62, 1.0, g.x * g.y) * 1.10, 1.0);
    }
    // 4: subpíxel LCD: cada píxel en tres columnas R, G y B
    vec3 m = vec3(0.28);
    m[min(int(f.x * 3.0), 2)] = 1.0;
    float gy = smoothstep(0.0, 0.12, f.y) * smoothstep(0.0, 0.12, 1.0 - f.y);
    return min(c * m * mix(0.65, 1.0, gy) * 1.55, 1.0);
}

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

// FXAA (versión de consola de Timothy Lottes, una sola pasada)
vec3 fxaa(sampler2D tex, vec2 uv, vec2 rcp) {
    const float REDUCE_MIN = 1.0 / 128.0;
    const float REDUCE_MUL = 1.0 / 8.0;
    const float SPAN_MAX = 8.0;
    vec3 rgbNW = texture(tex, uv + vec2(-1.0, -1.0) * rcp).rgb;
    vec3 rgbNE = texture(tex, uv + vec2( 1.0, -1.0) * rcp).rgb;
    vec3 rgbSW = texture(tex, uv + vec2(-1.0,  1.0) * rcp).rgb;
    vec3 rgbSE = texture(tex, uv + vec2( 1.0,  1.0) * rcp).rgb;
    vec3 rgbM  = texture(tex, uv).rgb;
    float lNW = luma(rgbNW), lNE = luma(rgbNE), lSW = luma(rgbSW), lSE = luma(rgbSE), lM = luma(rgbM);
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), ((lNW + lSW) - (lNE + lSE)));
    float reduce = max((lNW + lNE + lSW + lSE) * (0.25 * REDUCE_MUL), REDUCE_MIN);
    float rcpMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcpMin, vec2(-SPAN_MAX), vec2(SPAN_MAX)) * rcp;
    vec3 a = 0.5 * (texture(tex, uv + dir * (1.0 / 3.0 - 0.5)).rgb + texture(tex, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 b = a * 0.5 + 0.25 * (texture(tex, uv - dir * 0.5).rgb + texture(tex, uv + dir * 0.5).rgb);
    float lB = luma(b);
    return (lB < lMin || lB > lMax) ? a : b;
}
