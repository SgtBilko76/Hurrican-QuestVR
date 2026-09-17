[![CI build](https://github.com/HurricanGame/Hurrican/actions/workflows/build.yml/badge.svg)](https://github.com/HurricanGame/Hurrican/actions/workflows/build.yml)

A fork of Hurrican, freeware jump and shoot game created by Poke53280, with SDL2 enabled by default, support for libopenmpt and CRT simulation.
Also uses XDG compliant data/config paths on UNIX.
Additional userlevels from http://turricanforever.de included.

Original code by Eiswuxe (Poke53280) [[Winterworks](https://www.winterworks.de/project/hurrican/)]  
Further work by [Pickle136](https://sourceforge.net/projects/hurrican/), Stefan Schmidt ([thrimbor](https://github.com/thrimbor/Hurrican)) and Leandro Nini ([drfiemost](https://github.com/drfiemost/Hurrican))  
CRT simulation partially based on [CRT effect - Shadertoy, Unity](https://luka712.github.io/2018/07/21/CRT-effect-Shadertoy-Unity/) article from luka712's blog

![screenshot](https://github.com/HurricanGame/Hurrican/wiki/images/level1.png)

---

### Dependencies

The code depends on SDL2 (or the old deprecated SDL) with the image and mixer components, and libepoxy.
Optionally libopenmpt can be used for the music in place of the standard from SDL_mixer (see below).
A compiler with c++17 support is required.

### Building

The code can be built using cmake (tested on Linux and MinGW)

    git clone --recurse-submodules https://github.com/HurricanGame/Hurrican.git
    cd Hurrican/Hurrican
    mkdir build && cd build
    cmake -DCMAKE_BUILD_TYPE=Release ..
    cmake --build .

The following build options are available:

Predefined platforms
* -DPLATFORM=<RPI|GCW|RDU|RDU2|PANDORA> : Compile for the specified platform, setting the correct options

OpenGL Options
* -DRENDERER=GL1          : Use the OpenGL 1.X code (fixed pipline)
* -DRENDERER=GLES1        : Use the OpenGL 1.X code with ES compatible
* -DRENDERER=GL2          : Use the OpenGL 2.0 code (programable pipline) [this is the default if not specified]
* -DRENDERER=GLES2        : Use the OpenGL 2.0 code with ES compatible
* -DRENDERER=GL3          : Use the OpenGL 3.0 code (programable pipline)
* -DRENDERER=GLES3        : Use the OpenGL 3.0 code with ES compatible
* -DFBO=ON                : Add FBO support, allow screen to be scaled to arbitrary dimensions, available only with GL2 or GL3 and enabled by default
* -DDEFAULT_SCREENBPP=<16|24|32> : Set the default screen depth, 32 if not specified

Sound
* -DOPENMPT=ON            : Use the libopenmpt code for music (SDL2_mixer uses libmodplug while SDL_mixer uses the lower quality mikmod engine)

Generic
* -DFAST_RANDOM=OFF             : Use standard C random function in place of the fast [LCG](https://en.wikipedia.org/wiki/Linear_congruential_generator)
* -DFAST_TRIG=ON                : Use fast approximation for trigonometric functions
* -DDISABLE_EXCEPTIONS=ON       : Disable exception handling to reduce binary size
* -DUSE_PRECOMPILED_HEADERS=ON  : Enable pre-compiled headers for better compile time

Debug
* -DDISABLE_MEMPOOLING=ON : Bypass pooled memory manager
* -DCMAKE_BUILD_TYPE=<Asan|Ubsan>: Enable the Address or Undefined Behaviour Sanitizer

### Building for Meta Quest (standalone VR APK)

The `Hurrican/android` Gradle project builds a standalone Quest APK: the game is presented on a
large virtual screen whose draw layers (sky, parallax planes, tiles, sprites, overlays, HUD) are
composited per eye at different depths for a stereoscopic "diorama" effect, and the Touch
controllers are mapped to the game (remappable in *Options → Define Buttons*).

Requirements: Android SDK with NDK 27, CMake 3.22 (SDK component), JDK 17, a Quest 2/3/Pro.
SDL2, SDL2_image and SDL2_mixer (with libxmp for the tracker music) are built from the
submodules; the OpenXR loader comes from the Khronos Maven AAR via prefab.

    git clone --recurse-submodules https://github.com/HurricanGame/Hurrican.git
    cd Hurrican/Hurrican/android
    ./gradlew assembleDebug                    # -PhurricanPlatform=ANDROID for a flat, non-VR test build
    adb install -r app/build/outputs/apk/debug/app-debug.apk

The game data is packed into the APK and extracted to the app's private storage on first
start. Settings, savegames and `vr.cfg` (virtual screen size/distance, depth strength,
refresh rate) live there too:
`/sdcard/Android/data/com.hurricangame.quest/files` is *not* used; use
`adb shell run-as com.hurricangame.quest` to inspect `files/`.

Default controls: left stick = move / look up / duck, right stick = look up/down,
A = jump, right trigger = shoot, B = lightning, X = powerline, Y = grenade,
left trigger = smart bomb, right grip = cycle weapon, Menu (left controller) = pause / back,
click both sticks = recenter the screen.

### Running

To launch Hurrican, go back under the Hurrican folder

    cd ..
    ./build/hurrican

To see the available command line options use the `--help` argument

    ./build/hurrican --help

or check online at https://github.com/HurricanGame/Hurrican/wiki/Help
