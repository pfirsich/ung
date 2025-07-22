layout (binding = 2, std140) uniform UngCamera {
    mat4 view;
    mat4 view_inv;
    mat4 projection;
    mat4 projection_inv;
    mat4 view_projection;
    mat4 view_projection_inv;
};

layout (location = 0) in vec3 a_position;
layout (location = 1) in vec2 a_texcoord;
layout (location = 2) in vec4 a_color;

out vec2 vs_out_texcoord;
out vec4 vs_out_color;
out vec3 vs_out_position; // view space

void main() {
    vs_out_texcoord = a_texcoord;
    vs_out_color = a_color;
    gl_Position = view_projection * vec4(a_position, 1.0);
}