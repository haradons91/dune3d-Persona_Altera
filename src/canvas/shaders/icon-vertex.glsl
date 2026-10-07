#version 330
in vec3 origin;
in vec2 shift;
in uint icon_x;
in uint icon_y;
in uint flags;
in vec3 vec;
in float scale;

out vec4 origin_to_geom;
out vec2 shift_to_geom;
out float scale_to_geom;
out vec2 vec_to_geom;
flat out uint flags_to_geom;
flat out ivec2 icon_to_geom;
uniform uint pick_base;
flat out uint pick_to_geom;


uniform mat4 view;
uniform mat4 proj;
uniform mat3 screen;

void main() {
	pick_to_geom = uint(gl_VertexID+int(pick_base));
	flags_to_geom = flags;
    origin_to_geom = (proj*view*vec4(origin, 1));
    vec3 t = screen*vec3(1,-1,0);
    if(vec.x == vec.x && !isnan(vec.x)) {  // isnan() is broken on some platforms, but nan == nan evaluates to true on some others
        vec4 v4 = proj*view*vec4(vec, 0);
        v4.y *= -1;
        vec_to_geom = normalize(v4.xy/t.xy);
        // This canonicalizes to one of two equivalent orientations, correct
        // for a symmetric icon (e.g. a horizontal/vertical constraint bar,
        // which looks identical either way) but wrong for a genuinely
        // directional one like an arrow, which must keep pointing the way
        // it was actually told to -- VERTEX_FLAG_ICON_EXACT_DIRECTION (bit
        // 10, see vertex_flags.hpp) opts out of it. Using the raw bit
        // instead of the usual ubo.glsl macros/FLAG_IS_SET to avoid pulling
        // that whole shared uniform block into this shader stage.
        if((flags & (1u << 10)) == 0u && vec_to_geom.x < vec_to_geom.y)
            vec_to_geom *= -1;
    }
    else {
        vec_to_geom = vec2(1,0);
    }
    shift_to_geom = shift;
    icon_to_geom = ivec2(icon_x, icon_y);
    scale_to_geom = scale;
}

