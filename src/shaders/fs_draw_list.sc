
$input v_color0, v_texcoord0

/*
 * Copyright bgfx contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_tex, 0);

void main()
{
	vec4 texel = texture2D(s_tex, v_texcoord0);
	gl_FragColor = texel * v_color0; 
}
