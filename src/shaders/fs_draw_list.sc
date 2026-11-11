
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
	vec4 color = texel * v_color0;
	float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
	gl_FragColor = vec4(color.rgb + (noise - 0.5) * (0.98 / 255.0), color.a);
}
