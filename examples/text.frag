layout(binding = 0) uniform sampler2D u_base;

in vec2 vs_out_texcoord;
in vec4 vs_out_color;

out vec4 frag_color;

void main() {
    frag_color = vs_out_color * texture(u_base, vs_out_texcoord).r;
}