// Stereoscopic "diorama" presentation for the Meta Quest build.
//
// The engine keeps drawing exactly as on the desktop, but the frame is split into a
// handful of depth layers (sky, parallax planes, tiles, sprites, overlays, HUD). Each
// layer is drawn into its own transparent FBO; at the end of the frame the layers are
// composited per eye as textured quads at increasing distance from the viewer, so the
// background really sits behind the playfield and the HUD floats in front of it.

#ifndef _VRRENDER_HPP_
#define _VRRENDER_HPP_

#include <cstdint>
#include <string>

#include <glm/mat4x4.hpp>

namespace VRRender {

// Layer indices (see VRLayerEnum in DX8Graphics.hpp, must match)
constexpr int LAYER_COUNT = 7;

bool Init(const std::string &shaderDir, int layerWidth, int layerHeight);
void Shutdown();

// Bind a layer FBO as render target (viewport set to the layer size).
void BindLayer(int layer);
int LayerWidth();
int LayerHeight();

// Clear all layers (transparent; the sky layer opaque black). Leaves FBO 0 bound.
void ClearLayers();

// Draw all layers back to front into the currently bound eye framebuffer.
// screenModel places the centre of the 4:3 game plane; screenWidth in metres;
// depthStrength scales the per-layer distance factors (0 = flat).
void CompositeEye(const glm::mat4 &proj, const glm::mat4 &view, const glm::mat4 &screenModel,
                  float screenWidth, float screenDistance, float depthStrength, bool srgbDecode);

}  // namespace VRRender

#endif  // _VRRENDER_HPP_
