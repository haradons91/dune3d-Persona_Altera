#version 330
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

uniform mat3 screen;
uniform float line_width;

in vec4 p1_to_geom[1];
in vec4 p2_to_geom[1];
in uint flags_to_geom[1];
flat in uint axis_color_to_geom[1];
flat in uint pick_to_geom[1];
flat out uint pick_to_frag;
flat out uint flags_to_frag;
flat out uint hover_only_to_frag;
flat out vec3 color_to_frag;
flat out float alpha_to_frag;
flat out float depth_shift_to_frag;
flat out float select_alpha_to_frag;

##ubo

void main() {
	if (axis_color_to_geom[0] == 1u)
		color_to_frag = vec3(0.1, 0.7, 0.15);
	else if (axis_color_to_geom[0] == 2u)
		color_to_frag = vec3(0.9, 0.1, 0.1);
	else if (axis_color_to_geom[0] == 3u)
		color_to_frag = vec3(0.1, 0.35, 0.95);
	else if (axis_color_to_geom[0] == 4u)
		color_to_frag = FLAG_IS_SET(flags_to_geom[0], VERTEX_FLAG_HOVER | VERTEX_FLAG_SELECTED)
		                         ? vec3(1.0, 1.0, 0.25)
		                         : vec3(1.0, 0.78, 0.05);
	else if (axis_color_to_geom[0] == 5u)
		color_to_frag = vec3(0.0, 0.0, 0.0);
	else if (axis_color_to_geom[0] == 6u)
		color_to_frag = vec3(1.0, 1.0, 0.25);
	else if (FLAG_IS_SET(flags_to_geom[0], VERTEX_FLAG_HOVER_ONLY))
		color_to_frag = vec3(0.22, 0.22, 0.22);
	else
		color_to_frag = get_color(flags_to_geom[0]);
	alpha_to_frag = (axis_color_to_geom[0] == 4u || axis_color_to_geom[0] == 5u || axis_color_to_geom[0] == 6u)
	                         ? 0.55
	                         : 1.0;
	if (FLAG_IS_SET(flags_to_geom[0], VERTEX_FLAG_HOVER_ONLY)
	    && !FLAG_IS_SET(flags_to_geom[0], VERTEX_FLAG_HOVER | VERTEX_FLAG_SELECTED))
		alpha_to_frag = 0.0;
	depth_shift_to_frag = get_depth_shift(flags_to_geom[0]);
	if (axis_color_to_geom[0] == 5u)
		depth_shift_to_frag = 0.001;
	select_alpha_to_frag = get_select_alpha(flags_to_geom[0]);
	if(test_peel(pick_to_geom[0]))
		return;
	
	
	vec4 p0x = p1_to_geom[0] / p1_to_geom[0].w;
	vec4 p1x = p2_to_geom[0] / p2_to_geom[0].w;
	
	vec2 v = p1x.xy-p0x.xy;
	// A zero-length segment (both endpoints coincide) is a deliberate "join
	// patch": two consecutive segments of a tessellated curve (circle, arc)
	// only ever meet at some angle, never perfectly collinear, so the cap
	// extension below -- which only fully closes the gap for near-collinear
	// joints -- leaves a visible gap at a sharp enough angle no matter how
	// finely the curve is tessellated. A join patch plugs that gap directly
	// with a line_width-sized square at the shared vertex. Pick an arbitrary
	// direction here so the same cap/perpendicular math below still produces
	// that square instead of propagating NaNs from normalize(vec2(0)).
	if (dot(v, v) < 1e-12)
		v = vec2(1.0, 0.0);
	float width_scale = 1.0;
	if(FLAG_IS_SET(flags_to_geom[0], VERTEX_FLAG_LINE_THINNER))
		width_scale = .25;
	else if(FLAG_IS_SET(flags_to_geom[0], VERTEX_FLAG_LINE_THIN))
		width_scale = .5;
	// screen maps pixel deltas to NDC deltas, and NDC is scaled differently
	// in x and y (viewport aspect ratio, plus zoom for x/y independently in
	// ortho mode). v is in NDC, so normalize(v)/its perpendicular are only
	// actually unit-length/perpendicular *on screen* when that scaling is
	// uniform. At other angles this understated the perpendicular width and
	// skewed it away from true-perpendicular, so do this part of the math in
	// real (isotropic) pixels instead, converting back to NDC only for the
	// final offset.
	mat3 screen_inv = inverse(screen);
	vec2 v_px = (screen_inv * vec3(v, 0)).xy;
	// Extend the segment by half its rendered width.  The line is emitted as
	// a rectangle with butt caps; without this screen-space cap extension,
	// adjacent angled edges leave a visible gap even when their 3D endpoints
	// are identical.
	vec2 cap_px = normalize(v_px) * (line_width * width_scale / 2.0);
	vec2 cap_ndc = (screen * vec3(cap_px, 0)).xy;
	p0x.xy -= cap_ndc;
	p1x.xy += cap_ndc;
	vec2 o2_px = normalize(vec2(-v_px.y, v_px.x)) * (line_width * width_scale / 2.0);

	vec4 o = vec4((screen*vec3(o2_px,0)).xy, 0, 0);
	
	pick_to_frag = pick_to_geom[0];
	flags_to_frag = flags_to_geom[0];
	hover_only_to_frag = FLAG_IS_SET(flags_to_geom[0], VERTEX_FLAG_HOVER_ONLY) ? 1u : 0u;
	gl_Position = p0x-o;
	EmitVertex();
	
	pick_to_frag = pick_to_geom[0];
	gl_Position = p0x+o;
	EmitVertex();

	pick_to_frag = pick_to_geom[0];
	gl_Position = p1x-o;
	EmitVertex();
	
	pick_to_frag = pick_to_geom[0];
	gl_Position = p1x+o;
	EmitVertex();
	
	EndPrimitive();
	
}
