# ung (micro-engine)

This is an attempt to make the smallest game engine I could make that enables me to make games.
I am never quite sure how exactly I want to structure games or which abstractions I like and sometimes not even which programming language I like to use, so this is the minimal core I need that I could (potentially) build other stuff on top.

It's intended for me alone and for the games I would like to make (small-ish to medium indie games, mostly 3D with fairly low-complexity graphics).

The whole API is in a single C99 header file [ung.h](include/ung.h). Every resource is identified by a uint64 so the IDs can be represented exactly by a double (64 bit float). This enables easy binding to scripting languages like Lua or JavaScript.

The engine is also built with game jam games in mind, which now often require a web version. Therefore it is built to work with [Emscripten](https://emscripten.org/).

# Features
* Basic rendering abstractions (shader, texture, material, transform, geometry, camera)
* Basic input (including gamepad)
* Auto Reloading of resources
* Sound
* Random Numbers
* Sprite Renderer
* Text Rendering
* Skeletal Animation

# Notes
This engine is single-threaded everywhere. And it is intended to be used from single-threaded applications.

# Pasta
There are many systems that are well-known for being super simple to make an ad-hoc version of yourself. Like an abstract input system (often just a single struct and a single function) or a particle system (often a ring buffer and an update function). But those systems are only easy to make yourself, because they are easy to specialize. Input systems are simple, but they be pretty crazy, once you introduce double taps, modifier keys, different response curves for analog inputs, VR controls, etc. And making a system that can handle EVERYTHING is insanely complicated and you end up with a HUGE system that only 20% is needed of 80% of the time, but you still have to understand the whole thing!
It is systems like these, where it is less work to do the 10% of it you actually need yourself than understanding an existing thing and the dozen abstractions it had to introduce and using 10% of that. I think systems like these make up much of my frustration about game engines, where I want to do something very simple that I know exactly how to do without a game engine, but I have to squeeze and cut to fit that simple thing into existing, complicated abstractions that are made for literally everything. And in the end a general purpose game engine is FULL OF THIS STUFF. In fact it is mostly overcomplicated abstractions that barely anyone is using. And if anyone is using it, they are not using all of them at the same time.

For for some systems my peronal way forward is having them be external to the engine itself. Instead of a module or a part of it, it's just a piece of code, which you are intended to copy-paste and modify. They should just be a basic template that shows a general path that is intended to work and also shows you a way to do something like this, if you don't already know. But it is no more than that and it is built in a way that expects games to modify it.

If not 80% of games need it, would increase compile time or binary size noticably for projects that don't use it and it can be implemented using the existing public API, I will try to put it into pasta/ instead.


# Libraries
* [SDL2](https://www.libsdl.org/)
* [cgltf](https://github.com/jkuhlmann/cgltf/) (optional)
* [fast_obj](https://github.com/thisistherk/fast_obj)
* [glad](https://github.com/Dav1dde/glad)
* [miniaudio](https://github.com/mackron/miniaudio)
* [stb_image, stb_truetype](https://github.com/nothings/stb)

# TODO
* Collision! ([wuzy](https://github.com/pfirsich/wuzy))
* Auto-Instancing
* Post-Processing pasta
* Abstract Input pasta
* High-Level Renderer pasta (with lights and shadows)
* Path library (to select XDG and special Windows dirs)
* UTF8 functions
* Asset baking