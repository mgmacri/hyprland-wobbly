#pragma once

#include <hyprland/src/render/Transformer.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>
#include <hyprland/src/render/Shader.hpp>
#include <GLES3/gl32.h>
#include "WobblyModel.hpp"

struct WVertex {
    float x, y, u, v;
};

class WobblyTransformer : public IWindowTransformer {
public:
    explicit WobblyTransformer(wobbly::Model* model) : m_model(model) {}
    ~WobblyTransformer() override;

    SP<Render::IFramebuffer> transform(SP<Render::IFramebuffer> in) override;

private:
    void buildMesh(Vector2D winSize);

    wobbly::Model*                 m_model = nullptr;
    bool                           m_ready = false;
    GLuint                         m_vao = 0;
    GLuint                         m_vbo = 0;
    GLuint                         m_ebo = 0;
    GLint                          m_texLoc = -1;
    CShader                        m_shader;
    SP<Render::GL::CGLFramebuffer> m_outFB;

    // Fixed‑size arrays (4x4 grid)
    static constexpr int VERT_COUNT = wobbly::Model::N;
    static constexpr int IDX_COUNT  = (wobbly::Model::GRID_W - 1) *
                                      (wobbly::Model::GRID_H - 1) * 6;
    WVertex    m_arenaVerts[VERT_COUNT];
    uint16_t   m_arenaIndices[IDX_COUNT];

    bool       m_indicesBuilt = false;
    GLuint     m_boundVAO = 0, m_boundProg = 0;
    uint64_t   m_frameCount = 0;
};
