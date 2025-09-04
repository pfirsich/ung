#include <cmath>
#include <cstring>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <um.h>
#include <ung.h>

#include "../pasta/cgltf/ung-cgltf.h"

#include <cgltf.h>

struct Primitive {
    ung_geometry_id geometry;
    ung_material_id material;
};

struct Mesh {
    ung_skeleton_id skel;
    std::unordered_map<std::string, ung_animation_id> animations;
    std::vector<Primitive> primitives;
};

struct Game {
    ung_camera_id camera;
    ung_material_id material;
    ung_transform_id trafo;
    um_rad cam_yaw = { 0.0f };
    um_rad cam_pitch = { 0.0f };
    um_vec3 cam_pos = {};
    bool running = true;

    std::vector<Mesh> meshes;

    void init()
    {
        uint32_t win_w, win_h;
        ung_get_window_size(&win_w, &win_h);
        camera = ung_camera_create();
        ung_camera_set_perspective(camera, 45.0f, static_cast<float>(win_w) / win_h, 0.1f, 300.0f);
        cam_pos = { 0.0f, 1.0f, 3.0f };

        material
            = ung_material_load("examples/assets/skinning.vert", "examples/assets/skinning.frag",
                {
                    .mugfx = { .cull_face = MUGFX_CULL_FACE_MODE_NONE },
                    .dynamic_data_size = 64 * 16 * sizeof(float),
                });
        const auto texture = ung_texture_load("examples/assets/checkerboard.png", false, {});
        ung_material_set_texture(material, 0, texture);

        trafo = ung_transform_create();

        const auto gltf_path = "examples/assets/Quaternius_Universal_Animation_Library.glb";
        // const auto gltf_path = "examples/assets/SimpleSkin.gltf";
        const cgltf_options options = {};
        cgltf_data* data = nullptr;
        const auto result = cgltf_parse_file(&options, gltf_path, &data);
        if (result != cgltf_result_success) {
            ung_panicf("Error loading glTF file: %d\n", result);
        }
        cgltf_load_buffers(&options, data, gltf_path);

        for (const auto& g_node : std::span<cgltf_node>(data->nodes, data->nodes_count)) {
            if (g_node.mesh) {
                Mesh mesh;
                for (const auto& g_prim : std::span<cgltf_primitive>(
                         g_node.mesh->primitives, g_node.mesh->primitives_count)) {
                    mesh.primitives.push_back({ get_geom_from_cgltf(&g_prim), material });
                }
                if (g_node.skin) {
                    std::array<ung_skeleton_joint, 64> joints;
                    const auto num_joints
                        = get_joints_from_cgltf(g_node.skin, joints.data(), joints.size());
                    mesh.skel = ung_skeleton_create({
                        .num_joints = num_joints,
                        .joints = joints.data(),
                    });
                    for (size_t a = 0; a < data->animations_count; ++a) {
                        const auto& g_anim = data->animations[a];
                        const std::string default_name = std::to_string(a);
                        const auto name = g_anim.name ? g_anim.name : default_name.c_str();
                        mesh.animations.emplace(name, get_anim_from_cgltf(&g_anim, g_node.skin));
                    }
                }
                meshes.push_back(mesh);
            }
        }

        cgltf_free(data);
    }

    void update(float dt)
    {
        if (ung_key_pressed("escape")) {
            running = false;
        }

        // Camera Movement
        int mx, my, mdx, mdy;
        ung_mouse_get(&mx, &my, &mdx, &mdy);
        const auto sens = 1.0f;
        cam_yaw.v -= mdx * dt * sens;
        cam_pitch.v -= mdy * dt * sens;
        const auto yaw_q = um_quat_from_axis_angle({ 0.0f, 1.0f, 0.0f }, cam_yaw);
        const auto pitch_q = um_quat_from_axis_angle({ 1.0f, 0.0f, 0.0f }, cam_pitch);
        const auto cam_q = um_quat_mul(yaw_q, pitch_q);
        const auto cam_trafo = ung_camera_get_transform(camera);
        ung_transform_set_orientation(cam_trafo, &cam_q.x);

        const auto move_speed = 20.0f;
        const auto move_x = ung_key_down("d") - ung_key_down("a");
        const auto move_z = ung_key_down("w") - ung_key_down("s");
        const auto move = um_vec3_normalized({ (float)move_x, 0.0f, -(float)move_z });
        const auto world_move = um_quat_mul_vec3(cam_q, move);
        cam_pos = um_vec3_add(cam_pos, um_vec3_mul(world_move, move_speed * dt));
        ung_transform_set_position(cam_trafo, &cam_pos.x);
    }

    void draw()
    {
        ung_begin_frame();

        const auto t = ung_get_time();

        ung_begin_pass(MUGFX_RENDER_TARGET_BACKBUFFER, camera);
        mugfx_clear(MUGFX_CLEAR_COLOR_DEPTH, MUGFX_CLEAR_DEFAULT);
        for (const auto& mesh : meshes) {
            uint16_t num_joints = 0;
            const auto joint_transforms = ung_skeleton_get_joint_transforms(mesh.skel, &num_joints);
            const auto anim = mesh.animations.at("Dance_Loop");
            // const auto anim = mesh.animations.at("0");
            const auto ta = fmodf(t, ung_animation_get_duration(anim));
            ung_animation_sample(anim, ta, joint_transforms, num_joints);
            const auto joint_matrices = ung_skeleton_update_joint_matrices(mesh.skel, nullptr);

            for (const auto& prim : mesh.primitives) {
                std::memcpy(ung_material_get_dynamic_data(prim.material), joint_matrices,
                    num_joints * 16 * sizeof(float));
                ung_material_update(prim.material);
                ung_draw(prim.material, prim.geometry, trafo);
            }
        }
        ung_end_pass();
        ung_end_frame();
    }

    bool mainloop(float dt)
    {
        update(dt);
        draw();
        return running;
    }
};

static bool mainloop_wrap(void* arg, float dt)
{
    return ((Game*)arg)->mainloop(dt);
}

int main(int, char**)
{
    ung_init({
        .title = "cgltf Test",
        .window_mode = { .width = 1600, .height = 900 },
    });
    Game game;
    game.init();
    ung_run_mainloop(&game, mainloop_wrap); // This is optional, but helps with emscripten
    ung_shutdown();
    return 0;
}
