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
	float width_scale = 1.0;
	if(FLAG_IS_SET(flags_to_geom[0], VERTEX_FLAG_LINE_THINNER))
		width_scale = .25;
	else if(FLAG_IS_SET(flags_to_geom[0], VERTEX_FLAG_LINE_THIN))
		width_scale = .5;
	// Extend the segment by half its rendered width.  The line is emitted as
	// a rectangle with butt caps; without this screen-space cap extension,
	// adjacent angled edges leave a visible gap even when their 3D endpoints
	// are identical.
	vec2 cap = normalize(v) * (line_width * width_scale / 2.0);
	vec2 cap_ndc = (screen * vec3(cap, 0)).xy;
	p0x.xy -= cap_ndc;
	p1x.xy += cap_ndc;
	// Use a true perpendicular to the segment direction.  The previous
	// (-v.y, -v.x) vector becomes nearly parallel to diagonal segments,
	// collapsing their rendered width and making curved geometry fade out.
	vec2 o2 = vec2(-v.y, v.x);
	o2 /= length(o2);
	o2 *= line_width/2;
	if(FLAG_IS_SET(flags_to_geom[0], VERTEX_FLAG_LINE_THINNER))
		o2 *= .25;
	else if(FLAG_IS_SET(flags_to_geom[0], VERTEX_FLAG_LINE_THIN))
		o2 *= .5;
	
	vec4 o = vec4((screen*vec3(o2,0)).xy, 0, 0);
	
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
