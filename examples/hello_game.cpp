#include <array>
#include <cstdio>

#include <um.h>
#include <ung.h>

#include "../pasta/pfx/pfx.hpp"

struct Game {
    ung_camera_id camera;
    ung_material_id material;
    ung_geometry_id geometry;
    ung_transform_id trafo;
    um_rad cam_yaw = { 0.0f };
    um_rad cam_pitch = { 0.0f };
    um_vec3 cam_pos = {};
    bool running = true;
    ung_geometry_id level;
    ung_transform_id level_trafo;
    ung_camera_id ui_camera;
    ung_material_id sprite_material;
    ung_font font;
    std::array<utxt_quad, 1024> quads;
    utxt_layout* layout;
    utxt_style style;
    ung_sound_source_id shoot_sound;
    ung_sound_source_id explode_sound;
    bool mouse_captured = true;

    pfx::Renderer pfx_renderer;
    pfx::Effect pfx_smoke;
    std::array<pfx::Particle, 4096> smoke_particles;
    float smoke_spawn_accum = 0.0f;
    bool update_particles = true;

    void init()
    {
        material = ung_material_load("examples/assets/hello_game.vert",
            "examples/assets/hello_game.frag",
            { .mugfx = { .cull_face = MUGFX_CULL_FACE_MODE_NONE } });

        const auto texture = ung_texture_load("examples/assets/checkerboard.png", false, {});
        ung_material_set_texture(material, 0, texture);

        geometry = ung_geometry_load("examples/assets/Wasp.obj");
        // geometry = ung_geometry_box(1.0f, 1.0f, 1.0f);

        trafo = ung_transform_create();

        level_trafo = ung_transform_create();
        ung_transform_set_scale_u(level_trafo, 0.1f);
        level = ung_geometry_load("examples/assets/level.obj");

        uint32_t win_w, win_h;
        ung_get_window_size(&win_w, &win_h);
        camera = ung_camera_create();
        ung_camera_set_perspective(camera, 45.0f, static_cast<float>(win_w) / win_h, 0.1f, 100.0f);

        ung_mouse_set_relative(mouse_captured);

        ui_camera = ung_camera_create();
        ung_camera_set_orthographic_fullscreen(ui_camera);

        sprite_material = ung_material_load("examples/assets/sprite.vert", "examples/assets/sprite.frag", {
            .mugfx = {
                  .depth_func = MUGFX_DEPTH_FUNC_ALWAYS,
                  .write_mask = MUGFX_WRITE_MASK_RGBA,
                  .cull_face = MUGFX_CULL_FACE_MODE_NONE,
            },
        });
        const auto sprite_texture = ung_texture_load("examples/assets/checkerboard.png", false, {});
        ung_material_set_texture(sprite_material, 0, sprite_texture);

        ung_font_load_ttf(&font,
            {
                .ttf_path = "examples/assets/NotoSans.ttf",
                .load_params = { .size = 50, .atlas_size = 1024 },
                .vert_path = "examples/assets/sprite.vert",
                .frag_path = "examples/assets/text.frag",
            });
        utxt_draw_text(quads.data(), quads.size(), font.font,
            UTXT_LITERAL("Hallo, Jana! Guck dir mal den Text an :)"), 20.0f, 40.0f);

        layout = utxt_layout_create(ung_get_utxt_alloc(), 1024);
        utxt_layout_reset(layout, 512.0f, UTXT_TEXT_ALIGN_LEFT);
        style = { font.font, nullptr };
        utxt_layout_add_text(layout, &style,
            UTXT_LITERAL(
                "Hey, guck dir mal diesen Text an, der mit aller Wahrscheinlichkeit mehrere Zeilen "
                "füllen wird. Es geht gar nicht anders! Er ist so lang, dass vermutlich"));
        utxt_layout_add_text(layout, &style,
            UTXT_LITERAL(
                " sogar mehr als zwei Zeilen nötig sein werden. Vielleicht sogar vier!\nDieser "
                "Teil sollte in einer eigenen Zeile sein."));
        utxt_layout_compute(layout);

        shoot_sound = ung_sound_source_load("examples/assets/shoot.wav", {});
        explode_sound = ung_sound_source_load("examples/assets/explode.wav", {});

        pfx_renderer.init(smoke_particles.size());

        const auto smoke
            = ung_texture_load("examples/assets/smoke.png", true, { .generate_mipmaps = true });

        pfx_smoke.buffer.particles = smoke_particles;
        pfx_smoke.draw_data.init(smoke_particles.size(), "pasta/pfx/pfx.vert", "pasta/pfx/pfx.frag",
            smoke, pfx::Sort::BackToFront);
        pfx_smoke.load("examples/assets/smoke.pfx");
    }

    void update(float dt)
    {
        if (ung_key_pressed("escape")) {
            running = false;
        }

        if (ung_key_pressed("c")) {
            mouse_captured = !mouse_captured;
            ung_mouse_set_relative(mouse_captured);
        }

        if (ung_key_pressed("j")) {
            ung_sound_play(shoot_sound, {});
        }
        if (ung_key_pressed("k")) {
            ung_sound_play(explode_sound, {});
        }

        if (ung_key_pressed("p")) {
            update_particles = !update_particles;
        }

        const auto box_q = um_quat_from_axis_angle({ 0.0f, 1.0f, 0.0f }, um_rad { ung_get_time() });
        ung_transform_set_orientation(trafo, &box_q.x);

        const auto cam_trafo = ung_camera_get_transform(camera);

        if (mouse_captured) {
            // Camera Movement
            int mx, my, mdx, mdy;
            ung_mouse_get(&mx, &my, &mdx, &mdy);
            const auto sens = 1.0f;
            cam_yaw.v -= mdx * dt * sens;
            cam_pitch.v -= mdy * dt * sens;
            const auto yaw_q = um_quat_from_axis_angle({ 0.0f, 1.0f, 0.0f }, cam_yaw);
            const auto pitch_q = um_quat_from_axis_angle({ 1.0f, 0.0f, 0.0f }, cam_pitch);
            const auto cam_q = um_quat_mul(yaw_q, pitch_q);
            ung_transform_set_orientation(cam_trafo, &cam_q.x);
        }

        const auto move_speed = 10.0f;
        const auto move_x = ung_key_down("d") - ung_key_down("a");
        const auto move_z = ung_key_down("w") - ung_key_down("s");
        const auto move = um_vec3_normalized({ (float)move_x, 0.0f, -(float)move_z });
        um_quat q;
        ung_transform_get_orientation(cam_trafo, &q.x);
        const auto world_move = um_quat_mul_vec3(q, move);
        cam_pos = um_vec3_add(cam_pos, um_vec3_mul(world_move, move_speed * dt));
        ung_transform_set_position(cam_trafo, &cam_pos.x);

        if (update_particles) {
            smoke_spawn_accum += 200.f * dt;
            const auto spawn_n = (size_t)smoke_spawn_accum;
            smoke_spawn_accum -= spawn_n;
            pfx_smoke.spawn(spawn_n, { 0.f, 0.f, 0.f }, { 0.f, 1.f, 0.f });
            pfx_smoke.update(dt);
        }
    }

    void draw()
    {
        ung_begin_frame();

        ung_begin_pass(MUGFX_RENDER_TARGET_BACKBUFFER, camera);
        mugfx_clear(MUGFX_CLEAR_COLOR_DEPTH, MUGFX_CLEAR_DEFAULT);
        ung_draw(material, level, level_trafo);
        ung_draw(material, geometry, trafo);
        pfx_smoke.draw(pfx_renderer, camera);
        ung_end_pass();

        ung_begin_pass(MUGFX_RENDER_TARGET_BACKBUFFER, ui_camera);
        ung_sprite_add(sprite_material, { .x = 20.0f, .y = 160.0f }, UNG_REGION_FULL,
            { 0.4f, 0.4f, 0.4f, 1.0f });
        ung_font_draw_quads(&font, quads.data(), quads.size(), UNG_COLOR_WHITE);

        ung_sprite_set_material(font.material);
        size_t num_glyphs = 0;
        const auto glyphs = utxt_layout_get_glyphs(layout, &num_glyphs);
        utxt_quad q;
        for (size_t i = 0; i < num_glyphs; ++i) {
            utxt_layout_glyph_get_quad(&q, &glyphs[i], 20.0f, 200.0f);
            ung_font_draw_quad(&q, UNG_COLOR_WHITE);
        }

        ung_sprite_flush();
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
        .auto_reload = true,
    });
    auto game = new Game;
    game->init();
    ung_run_mainloop(game, mainloop_wrap); // This is optional, but helps with emscripten
    delete game;
    ung_shutdown();
    return 0;
}
