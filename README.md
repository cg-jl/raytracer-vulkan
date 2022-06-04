# Vulkan Raytracer

This is a partial port of [TheCherno's `Walnut`](https://github.com/TheCherno/Walnut), using only
what I was interested in (mainly the Vulkan interfacing code), plus a CPU-bound raytracer based on RaytracingInOneWeekend book
that I'll be using to follow Cherno's Ray Tracing series (link [here](https://www.youtube.com/playlist?list=PLlrATfBNZ98edc5GshdBtREv5asFW3yXl)).

The code works in Linux but I haven't used any Linux-specific APIs.

# Building

I use Meson and Ninja as the building method for this project. Make sure you
have `$VULKAN_SDK` set when generating:

```
meson setup --buildtype debugoptimized build
```

Note that I recommend `debugoptimized` or `release` because if not the rendering
(which currently runs CPU-wise) is going to be pretty slow.

To actually compile:

```
ninja -C build
```

The executable is placed in `build/raytracer`.
