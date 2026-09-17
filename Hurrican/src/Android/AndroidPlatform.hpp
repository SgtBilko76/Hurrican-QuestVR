// Android (Meta Quest) platform glue: logcat redirection and APK asset extraction.
//
// The engine reads all of its data through ordinary file paths (std::ifstream,
// IMG_Load(), Mix_LoadMUS(), std::filesystem::directory_iterator ...), which cannot
// address files packed inside the APK. So on first start (or whenever the asset
// stamp produced by the Gradle build changes) the data/ and lang/ trees are copied
// from the APK assets into the app's internal storage directory, and all of the
// game's path globals are pointed there.

#ifndef _ANDROIDPLATFORM_HPP_
#define _ANDROIDPLATFORM_HPP_

#include <string>

namespace AndroidPlatform {

// Forward stdout/stderr (i.e. everything Protokoll prints) to logcat, tag "Hurrican".
void RedirectStdioToLogcat();

// printf-style logcat output, tag "Hurrican".
void Log(const char *fmt, ...);

// The app's private files directory (SDL_AndroidGetInternalStoragePath()).
std::string GetInternalStoragePath();

// Extract the game data from the APK into `base` if the asset stamp changed.
// Returns false if the data could not be made available.
bool PrepareAssets(const std::string &base);

}  // namespace AndroidPlatform

#endif  // _ANDROIDPLATFORM_HPP_
