#include <array>
#include <cstdio>

#include <um.h>
#include <ung.h>

struct Game {
    ung_camera_id camera;
    ung_material_id material;
    mugfx_geometry_id geometry;
    ung_transform_id trafo;
    float cam_yaw = 0.0f;
    float cam_pitch = 0.0f;
    um_vec3 cam_pos = {};
    bool running = true;
    mugfx_geometry_id level;
    ung_transform_id level_trafo;

    void init()
    {
        material = ung_material_load("examples/hello_game.vert", "examples/hello_game.frag",
            { .mugfx_params = { .cull_face = MUGFX_CULL_FACE_MODE_NONE } });

        const auto texture = ung_texture_load("examples/assets/checkerboard.png", {});
        ung_material_set_texture(material, 0, texture);

        geometry = ung_geometry_load("examples/assets/Wasp.obj");
        // geometry = ung_geometry_box(1.0f, 1.0f, 1.0f);

        trafo = ung_transform_create();
        ung_transform_set_position(trafo, 0.0f, 0.0f, 0.0f);

        level_trafo = ung_transform_create();
        ung_transform_set_scale(level_trafo, 0.1f, 0.1f, 0.1f);
        level = ung_geometry_load("examples/assets/level.obj");

        uint32_t win_w, win_h;
        ung_get_window_size(&win_w, &win_h);
        camera = ung_camera_create();
        ung_camera_set_perspective(camera, 45.0f, static_cast<float>(win_w) / win_h, 0.1f, 100.0f);

        ung_mouse_set_relative(true);
    }

    void update(float dt)
    {
        if (ung_key_pressed("escape")) {
            running = false;
        }

        const auto box_q = um_quat_from_axis_angle({ 0.0f, 1.0f, 0.0f }, ung_get_time());
        ung_transform_set_orientation(trafo, box_q.w, box_q.x, box_q.y, box_q.z);

        // Camera Movement
        int mx, my, mdx, mdy;
        ung_mouse_get(&mx, &my, &mdx, &mdy);
        const auto sens = 1.0f;
        cam_yaw -= mdx * dt * sens;
        cam_pitch -= mdy * dt * sens;
        const auto yaw_q = um_quat_from_axis_angle({ 0.0f, 1.0f, 0.0f }, cam_yaw);
        const auto pitch_q = um_quat_from_axis_angle({ 1.0f, 0.0f, 0.0f }, cam_pitch);
        const auto cam_q = um_quat_mul(yaw_q, pitch_q);
        const auto cam_trafo = ung_camera_get_transform(camera);
        ung_transform_set_orientation(cam_trafo, cam_q.w, cam_q.x, cam_q.y, cam_q.z);

        const auto move_speed = 10.0f;
        const auto move_x = ung_key_down("d") - ung_key_down("a");
        const auto move_z = ung_key_down("w") - ung_key_down("s");
        const auto move = um_vec3_normalized({ (float)move_x, 0.0f, -(float)move_z });
        const auto world_move = um_quat_mul_vec3(cam_q, move);
        cam_pos = um_vec3_add(cam_pos, um_vec3_mul(world_move, move_speed * dt));
        ung_transform_set_position(cam_trafo, cam_pos.x, cam_pos.y, cam_pos.z);
    }

    void draw()
    {
        ung_begin_frame();
        ung_begin_pass(MUGFX_RENDER_TARGET_BACKBUFFER, camera);
        mugfx_clear(MUGFX_CLEAR_COLOR_DEPTH, MUGFX_CLEAR_DEFAULT);
        ung_draw(material, level, level_trafo);
        ung_draw(material, geometry, trafo);
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
        .title = "Hello Game",
        .window_mode = { .width = 1600, .height = 900 },
    });
    Game game;
    game.init();
    ung_run_mainloop(&game, mainloop_wrap); // This is optional, but helps with emscripten
    ung_shutdown();
    return 0;
}
