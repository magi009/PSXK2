#version 450
// ScaleFX - pasada 4 (portado a PSX-K2 desde libretro/slang-shaders)
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
layout(set = 0, binding = 1) uniform sampler2D refpass;


// extract corners
vec4 loadCrn(vec4 x){
	return floor(mod(x*80 + 0.5, 9));
}

// extract mids
vec4 loadMid(vec4 x){
	return floor(mod(x*8.888888 + 0.055555, 9));
}


void main()
{

	/*	grid		corners		mids

		  B		x   y	  	  x
		D E F				w   y
		  H		w   z	  	  z
	*/


	// read data
	vec4 E = texture(Source, vUV);

	// extract data
	vec4 crn = loadCrn(E);
	vec4 mid = loadMid(E);

	// determine subpixel
	vec2 fp = floor(3.0 * fract(vUV / pc.rcp));
	float sp = fp.y == 0. ? (fp.x == 0. ? crn.x : fp.x == 1. ? mid.x : crn.y) : (fp.y == 1. ? (fp.x == 0. ? mid.w : fp.x == 1. ? 0. : mid.y) : (fp.x == 0. ? crn.w : fp.x == 1. ? mid.z : crn.z));

	// output coordinate - 0 = E, 1 = D, 2 = D0, 3 = F, 4 = F0, 5 = B, 6 = B0, 7 = H, 8 = H0
	vec2 res = sp == 0. ? vec2(0,0) : sp == 1. ? vec2(-1,0) : sp == 2. ? vec2(-2,0) : sp == 3. ? vec2(1,0) : sp == 4. ? vec2(2,0) : sp == 5. ? vec2(0,-1) : sp == 6. ? vec2(0,-2) : sp == 7. ? vec2(0,1) : vec2(0,2);

	// ouput
	FragColor = vec4(texture(refpass, vUV + res * pc.rcp).rgb, 1.0);   // opaco aunque el núcleo deje alfa a 0
}
