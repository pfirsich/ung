layout (binding = 2, std140) uniform UngCamera {
    mat4 view;
    mat4 view_inv;
    mat4 projection;
    mat4 projection_inv;
    mat4 view_projection;
    mat4 view_projection_inv;
};

layout(location = 0) in vec2 a_position;  // [-0.5..0.5] quad in local billboard space
layout(location = 1) in vec2 a_texcoord;

// per instance
layout(location = 2) in vec3  i_position; // world-space center
layout(location = 3) in float i_size;
layout(location = 4) in float i_rot;
layout(location = 5) in vec4  i_color;

out vec2 vs_out_texcoord;
out vec4 vs_out_color;

void main()
{
    // Rotate and scale billboard plane
    float s = sin(i_rot);
    float c = cos(i_rot);
    vec2 p = i_size * vec2(c * a_position.x - s * a_position.y, s * a_position.x + c * a_position.y);

    // Transform to world space
    // The view matrix is the inverse of the camera world transform
    // I.e. we can get the camera world up/right vectors from view_inv.
    vec3 cam_right = view_inv[0].xyz;
    vec3 cam_up = view_inv[1].xyz;
    vec3 world_pos = i_position + cam_right * p.x + cam_up * p.y;

    vs_out_texcoord = a_texcoord;
    vs_out_color = i_color;

    gl_Position = view_projection * vec4(world_pos, 1.0);
}
