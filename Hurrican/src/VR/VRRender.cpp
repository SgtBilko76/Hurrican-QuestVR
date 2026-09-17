// Stereoscopic layer compositing for the Meta Quest build, see VRRender.hpp

#include "VRRender.hpp"
#include "VRInternal.hpp"

#include "cshader.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace VRRender {

namespace {

struct LayerFbo {
    GLuint texture = 0;
    GLuint framebuffer = 0;
};

LayerFbo layers_[LAYER_COUNT];
int width_ = 0;
int height_ = 0;

// Distance factor per layer relative to the game plane (1.0). Farther layers are
// scaled up by the same factor so they keep their on-screen size from the viewpoint.
const float DEPTH_FACTORS[LAYER_COUNT] = {
    4.0f,   // sky / background image
    3.0f,   // far parallax layer
    2.0f,   // near parallax layer + clouds
    1.35f,  // back tiles
    1.0f,   // playfield: front tiles, enemies, player, projectiles, particles
    0.9f,   // overlay tiles / water / shadow
    0.8f    // HUD, texts, menus, console
};

CShader shader_;
GLint attrPos_ = -1;
GLint attrTex_ = -1;
GLint uniMvp_ = -1;
GLint uniTex_ = -1;
GLint uniSrgb_ = -1;

bool CreateLayer(LayerFbo &l) {
    glGenTextures(1, &l.texture);
    glBindTexture(GL_TEXTURE_2D, l.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenFramebuffers(1, &l.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, l.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, l.texture, 0);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        VR::Log("layer FBO incomplete: 0x%x", status);
        return false;
    }
    return true;
}

}  // namespace

bool Init(const std::string &shaderDir, int layerWidth, int layerHeight) {
    width_ = layerWidth;
    height_ = layerHeight;
    for (auto &l : layers_)
        if (!CreateLayer(l))
            return false;

    if (!shader_.Load(shaderDir + "/shader_vrquad.vert", shaderDir + "/shader_vrquad.frag")) {
        VR::Log("failed to load the VR composite shader from %s", shaderDir.c_str());
        return false;
    }
    attrPos_ = shader_.GetAttribute("a_Position");
    attrTex_ = shader_.GetAttribute("a_Texcoord0");
    uniMvp_ = shader_.GetUniform("u_MVPMatrix");
    uniTex_ = shader_.GetUniform("u_Texture0");
    uniSrgb_ = shader_.GetUniform("u_SrgbDecode");

    ClearLayers();
    VR::Log("%d layer FBOs of %dx%d ready", LAYER_COUNT, width_, height_);
    return true;
}

void Shutdown() {
    for (auto &l : layers_) {
        if (l.framebuffer != 0)
            glDeleteFramebuffers(1, &l.framebuffer);
        if (l.texture != 0)
            glDeleteTextures(1, &l.texture);
        l = LayerFbo();
    }
    shader_.Close();
}

void BindLayer(int layer) {
    if (layer < 0 || layer >= LAYER_COUNT)
        layer = LAYER_COUNT - 1;
    glBindFramebuffer(GL_FRAMEBUFFER, layers_[layer].framebuffer);
    glViewport(0, 0, width_, height_);
}

int LayerWidth() {
    return width_;
}

int LayerHeight() {
    return height_;
}

void ClearLayers() {
    for (int i = 0; i < LAYER_COUNT; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, layers_[i].framebuffer);
        if (i == 0)
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        else
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void CompositeEye(const glm::mat4 &proj, const glm::mat4 &view, const glm::mat4 &screenModel, float screenWidth,
                  float screenDistance, float depthStrength, bool srgbDecode) {
    const float halfW = screenWidth * 0.5f;
    const float halfH = halfW * 0.75f;  // 4:3

    // x, y, z, u, v  -- the game's framebuffers have the picture's top at v = 1
    const float quad[4][5] = {
        {-halfW, -halfH, 0.0f, 0.0f, 0.0f},
        {halfW, -halfH, 0.0f, 1.0f, 0.0f},
        {-halfW, halfH, 0.0f, 0.0f, 1.0f},
        {halfW, halfH, 0.0f, 1.0f, 1.0f},
    };

    shader_.Use();
    glUniform1i(uniTex_, 0);
    glUniform1i(uniSrgb_, srgbDecode ? 1 : 0);
    glActiveTexture(GL_TEXTURE0);

    glEnableVertexAttribArray(attrPos_);
    glEnableVertexAttribArray(attrTex_);
    glVertexAttribPointer(attrPos_, 3, GL_FLOAT, GL_FALSE, sizeof(quad[0]), &quad[0][0]);
    glVertexAttribPointer(attrTex_, 2, GL_FLOAT, GL_FALSE, sizeof(quad[0]), &quad[0][3]);

    // Layers hold premultiplied colour (see DirectGraphicsClass blend modes in VR)
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    const glm::mat4 viewProj = proj * view;
    for (int i = 0; i < LAYER_COUNT; i++) {
        const float z = 1.0f + (DEPTH_FACTORS[i] - 1.0f) * depthStrength;  // distance factor
        const glm::mat4 model = screenModel * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -(z - 1.0f) * screenDistance)) *
                                glm::scale(glm::mat4(1.0f), glm::vec3(z, z, 1.0f));
        const glm::mat4 mvp = viewProj * model;
        glUniformMatrix4fv(uniMvp_, 1, GL_FALSE, glm::value_ptr(mvp));
        glBindTexture(GL_TEXTURE_2D, layers_[i].texture);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    glDisableVertexAttribArray(attrPos_);
    glDisableVertexAttribArray(attrTex_);
    glBindTexture(GL_TEXTURE_2D, 0);
}

}  // namespace VRRender
