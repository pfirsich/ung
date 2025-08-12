layout (binding = 3, std140) uniform UngTransform {
    mat4 model;
    mat4 model_inv;
    mat4 model_view;
    mat4 model_view_projection;
};

layout (binding = 9, std140) uniform UngMaterialDynamic {
    mat4 joint_matrices[64];
};

layout (location = 0) in vec3 a_position;
layout (location = 1) in vec2 a_texcoord;
layout (location = 2) in vec3 a_normal;
layout (location = 3) in vec4 a_color;
layout (location = 4) in ivec4 a_joints;
layout (location = 5) in vec4 a_weights;

out vec2 vs_out_texcoord;
out vec4 vs_out_color;
out vec3 vs_out_normal; // view space
out vec3 vs_out_position; // view space

void main()
{
    mat4 skin_matrix = a_weights.x * joint_matrices[int(a_joints.x)]
                     + a_weights.y * joint_matrices[int(a_joints.y)]
                     + a_weights.z * joint_matrices[int(a_joints.z)]
                     + a_weights.w * joint_matrices[int(a_joints.w)];

    vs_out_texcoord = a_texcoord;
    vs_out_color = a_color;
    vs_out_normal = transpose(inverse(mat3(model_view))) * a_normal;
    vs_out_position = (model_view * vec4(a_position, 1.0)).xyz;
    gl_Position = model_view_projection * skin_matrix * vec4(a_position, 1.0);
}