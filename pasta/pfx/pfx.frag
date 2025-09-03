layout(binding = 0) uniform sampler2D u_tex;

in vec2 vs_out_texcoord;
in vec4 vs_out_color;

out vec4 frag_color;

void main()
{
    vec4 texel = texture(u_tex, vs_out_texcoord);
    vec4 c = texel * vs_out_color;  // multiply-by-vertex-color (non-PMA art)
    if (c.a <= 0.001) discard;     // avoid fringes on fully transparent texels
    frag_color = c;
}
