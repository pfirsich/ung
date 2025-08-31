layout(binding = 0) uniform sampler2D u_base;

in vec2 vs_out_texcoord;
in vec4 vs_out_color;
in vec3 vs_out_normal; // view space
in vec3 vs_out_position; // view space

out vec4 frag_color;

void main() {
    vec3 light_dir = normalize(-vs_out_position); // camera is at origin
    vec3 normal = normalize(vs_out_normal);
    float ndotl = max(dot(light_dir, normal), 0.0);
    vec3 ambient = vs_out_color.rgb * vec3(0.5);
    vec3 diffuse = ndotl * vs_out_color.rgb * 0.3;
    frag_color = vec4(diffuse + ambient, 1.0);
    //frag_color = vec4(normal * 0.5 + 0.5, 1.0);
    //frag_color = vs_out_color;// * texture(u_base, vs_out_texcoord);
}