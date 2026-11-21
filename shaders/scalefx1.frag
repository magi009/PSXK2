#version 450
// ScaleFX - pasada 1 (portado a PSX-K2 desde libretro/slang-shaders)
// ScaleFX by Sp00kyFox, 2017-03-01
/*
Copyright (c) 2016 Sp00kyFox - ScaleFX@web.de

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
layout(push_constant) uniform PC { vec2 rcp; float mode; float blend; } pc;   // rcp = 1 / tamaño de origen
const float SFX_CLR = 0.50;   // umbral
const float SFX_SAA = 1.0;    // filtro AA
const float SFX_SCN = 1.0;    // filtrar esquinas

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 0) uniform sampler2D Source;
layout(set = 0, binding = 1) uniform sampler2D uUnused;


// corner strength
float str(float d, vec2 a, vec2 b){
	float diff = a.x - a.y;
	float wght1 = max(SFX_CLR - d, 0) / SFX_CLR;
	float wght2 = clamp((1-d) + (min(a.x, b.x) + a.x > min(a.y, b.y) + a.y ? diff : -diff), 0., 1.);
	return (SFX_SAA == 1. || 2.*d < a.x + a.y) ? (wght1 * wght2) * (a.x * a.y) : 0.;
}


void main()
{

	/*	grid		metric		pattern

		A B		x y z		x y
		D E F		  o w		w z
		G H I
	*/


#define TEX(x, y) textureOffset(Source, vUV, ivec2(x, y))

	// metric data
	vec4 A = TEX(-1,-1), B = TEX( 0,-1);
	vec4 D = TEX(-1, 0), E = TEX( 0, 0), F = TEX( 1, 0);
	vec4 G = TEX(-1, 1), H = TEX( 0, 1), I = TEX( 1, 1);

	// corner strength
	vec4 res;
	res.x = str(D.z, vec2(D.w, E.y), vec2(A.w, D.y));
	res.y = str(F.x, vec2(E.w, E.y), vec2(B.w, F.y));
	res.z = str(H.z, vec2(E.w, H.y), vec2(H.w, I.y));
	res.w = str(H.x, vec2(D.w, H.y), vec2(G.w, G.y));
		
	FragColor = res;
}
