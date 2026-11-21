#version 450
// ScaleFX - pasada 0 (portado a PSX-K2 desde libretro/slang-shaders)
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


// Reference: http://www.compuphase.com/cmetric.htm
float dist(vec3 A, vec3 B)
{
	float r = 0.5 * (A.r + B.r);
	vec3 d = A - B;
	vec3 c = vec3(2 + r, 4, 3 - r);

	return sqrt(dot(c*d, d)) / 3;
}


void main()
{

	/*	grid		metric

		A B C		x y z
		  E F		  o w
	*/


#define TEX(x, y) textureOffset(Source, vUV, ivec2(x, y)).rgb

	// read texels
	vec3 A = TEX(-1,-1);
	vec3 B = TEX( 0,-1);
	vec3 C = TEX( 1,-1);
	vec3 E = TEX( 0, 0);
	vec3 F = TEX( 1, 0);

	// output
	FragColor = vec4(dist(E,A), dist(E,B), dist(E,C), dist(E,F));
}
