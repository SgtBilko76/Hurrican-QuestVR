# SDL's Java glue is reached from native code via JNI; keep it intact.
-keep class org.libsdl.app.** { *; }
-keep class com.hurricangame.quest.** { *; }
