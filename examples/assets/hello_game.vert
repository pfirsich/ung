#pragma ung-include UngPass
#pragma ung-include UngTransform

layout (location = 0) in vec3 a_position;
layout (location = 1) in vec2 a_texcoord;
layout (location = 2) in vec3 a_normal;
layout (location = 3) in vec4 a_color;

out vec2 vs_out_texcoord;
out vec4 vs_out_color;
out vec3 vs_out_normal; // view space
out vec3 vs_out_position; // view space

void main() {
    vs_out_texcoord = a_texcoord;
    vs_out_color = a_color;
    vs_out_normal = mat3(view) * mat3(normal_matrix) * a_normal;
    vs_out_position = (model_view * vec4(a_position, 1.0)).xyz;
    gl_Position = model_view_projection * vec4(a_position, 1.0);
}