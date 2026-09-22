#version 330

layout(location = 0) out vec4 outputColor;
layout(location = 1) out uint pick;
layout(location = 2) out vec4 select;
flat in uint pick_to_frag;
flat in vec3 color_to_frag;
flat in float select_alpha_to_frag;
flat in float depth_shift_to_frag;
flat in uint flags_to_frag;
uniform sampler2D tex;
smooth in vec2 texcoord_to_fragment;
uniform float texture_size;

##ubo

void main() {
    if(test_peel(pick_to_frag))
		discard;

  vec3 color = color_to_frag;
  
  float sample = texture(tex, texcoord_to_fragment).r;
  vec4 colora = vec4(color, sample);
  bool hidden_hover_only = FLAG_IS_SET(flags_to_frag, VERTEX_FLAG_HOVER_ONLY)
      && !FLAG_IS_SET(flags_to_frag, VERTEX_FLAG_HOVER | VERTEX_FLAG_SELECTED);
  if (hidden_hover_only)
    colora.a = 0.0;
  // A hidden hover-only marker still needs to reach the pick buffer (so
  // hovering it works), so it isn't discarded. But it must not write its
  // true depth, or it would occlude real geometry behind it while
  // invisible. Push it to the far plane instead.
  gl_FragDepth = hidden_hover_only ? 1.0 : gl_FragCoord.z *(1-0.001 + depth_shift_to_frag);
  if(colora.a < 0.1 && !FLAG_IS_SET(flags_to_frag, VERTEX_FLAG_HOVER_ONLY))
      discard;

  outputColor = vec4(colora.rgb, colora.a);
  select = outputColor*select_alpha_to_frag;
  pick = pick_to_frag;
}
