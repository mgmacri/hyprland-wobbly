#pragma once

#include <hyprland/src/render/Transformer.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>
#include <hyprland/src/render/Shader.hpp>
#include <GLES3/gl32.h>
#include <vector>
#include "WobblyModel.hpp"

struct WVertex { float x, y, u, v; };

class WobblyTransformer : public IWindowTransformer {
  public:
    explicit WobblyTransformer(wobbly::Model* model) : m_model(model) {}
    ~WobblyTransformer() override;

    SP<Render::IFramebuffer> transform(SP<Render::IFramebuffer> in) override;

  private:
    void buildMesh(Vector2D winSize);

    wobbly::Model*                 m_model  = nullptr;
    bool                           m_ready  = false;
    GLuint                         m_vao    = 0;
    GLuint                         m_vbo    = 0;
    GLuint                         m_ebo    = 0;
    GLint                          m_texLoc = -1;
    CShader                        m_shader;
    SP<Render::GL::CGLFramebuffer> m_outFB;
    std::vector<WVertex>           m_verts;
    std::vector<uint16_t>          m_indices;
};
