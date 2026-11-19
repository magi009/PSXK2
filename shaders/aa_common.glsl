// Parámetros comunes de los pases de antialiasing
layout(push_constant) uniform PC {
    vec2 rcp;      // 1 / tamaño de la imagen de origen (en texels)
    float mode;    // 0 = copia, 1 = FXAA
    float blend;   // TAA: peso del frame actual (1 = sin historial)
} pc;

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
