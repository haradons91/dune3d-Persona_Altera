#version 330
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

uniform mat3 screen;

in vec4 origin_to_geom[1];
in vec2 shift_to_geom[1];
flat in uint flags_to_geom[1];
flat in uint bits_to_geom[1];
in float scale_to_geom[1];
in float angle_to_geom[1];
flat in uint pick_to_geom[1];
flat out uint pick_to_frag;
flat out vec3 color_to_frag;
flat out float select_alpha_to_frag;
smooth out vec2 texcoord_to_fragment;

##ubo

void main() {
	if(test_peel(pick_to_geom[0]))
		return;
	color_to_frag = get_color(flags_to_geom[0]);
	select_alpha_to_frag = get_select_alpha(flags_to_geom[0]);
	
	vec4 o = origin_to_geom[0];
    o /= o.w;
   
    uint bits = bits_to_geom[0];
    GlyphInfo glyph = unpack_glyph_info(bits);
    mat2 rotation = mat2(cos(angle_to_geom[0]), -sin(angle_to_geom[0]),
                         sin(angle_to_geom[0]), cos(angle_to_geom[0]));
    vec3 shift_screen = screen * vec3(shift_to_geom[0].x, shift_to_geom[0].y, 0.0);
    vec3 size_x_screen = screen * vec3(glyph.w * scale_to_geom[0], 0.0, 0.0);
    vec3 size_y_screen = screen * vec3(0.0, -glyph.h * scale_to_geom[0], 0.0);
    vec2 shift = rotation * vec2(shift_screen.x, shift_screen.y);
    vec2 size_x = rotation * vec2(size_x_screen.x, size_x_screen.y);
    vec2 size_y = rotation * vec2(size_y_screen.x, size_y_screen.y);
    
	pick_to_frag = pick_to_geom[0];
	gl_Position = o+vec4(shift.x, shift.y, 0.0, 0.0);
    texcoord_to_fragment = vec2(glyph.x,glyph.y)/1024;
	EmitVertex();
	
	pick_to_frag = pick_to_geom[0];
	gl_Position = o+vec4((shift + size_x).x, (shift + size_x).y, 0.0, 0.0);
    texcoord_to_fragment = vec2(glyph.x+glyph.w,glyph.y)/1024;
	EmitVertex();
	
	pick_to_frag = pick_to_geom[0];
	gl_Position = o+vec4((shift + size_y).x, (shift + size_y).y, 0.0, 0.0);
    texcoord_to_fragment = vec2(glyph.x,glyph.y+glyph.h)/1024;
	EmitVertex();
    
	pick_to_frag = pick_to_geom[0];
	gl_Position = o+vec4((shift + size_x + size_y).x, (shift + size_x + size_y).y, 0.0, 0.0);
    texcoord_to_fragment = vec2(glyph.x+glyph.w,glyph.y+glyph.h)/1024;
	EmitVertex();
	
	EndPrimitive();
	
}
