package com.hurricangame.quest;

import org.libsdl.app.SDLActivity;

/**
 * Meta Quest entry point. Everything (lifecycle, JNI, asset access, audio) is handled by
 * SDL's stock SDLActivity; we only tell it which native libraries to load and which one
 * contains SDL_main().
 */
public class HurricanActivity extends SDLActivity {

    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL2",
            "openxr_loader",
            "hurrican"
        };
    }

    @Override
    protected String getMainSharedObject() {
        return getContext().getApplicationInfo().nativeLibraryDir + "/libhurrican.so";
    }
}
