#include "WobblyTransformer.hpp"
#include <cstdio>
#include <chrono>
#include <algorithm>

// Logging (same as main.cpp)
static void tlog(const std::string& s) {
    using namespace std::chrono;
    const auto ms = duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
    fprintf(stderr, "[wobbly-tx %lld] %s\n", (long long)ms, s.c_str());
    fflush(stderr);
}

// Shaders
static constexpr const char* VERT_SRC = R"glsl(
    #version 320 es
    layout(location = 0) in vec2 in_pos;
    layout(location = 1) in vec2 in_uv;
    out vec2 uv;
    void main() {
        gl_Position = vec4(in_pos, 0.0, 1.0);
        uv = in_uv;
    }
)glsl";

static constexpr const char* FRAG_SRC = R"glsl(
    #version 320 es
    precision mediump float;
    uniform sampler2D tex;
    in vec2 uv;
    out vec4 color;
    void main() {
        color = texture(tex, uv);
    }
)glsl";

WobblyTransformer::~WobblyTransformer() {
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_ebo) glDeleteBuffers(1, &m_ebo);
    tlog("Destructor: cleaned up GL resources");
}

void WobblyTransformer::buildMesh(Vector2D winSize) {
    const float* posX = m_model->posX();
    const float* posY = m_model->posY();
    
    // Prevent division by zero
    const float invW = (winSize.x > 0) ? (1.0f / winSize.x) : 1.0f;
    const float invH = (winSize.y > 0) ? (1.0f / winSize.y) : 1.0f;

    for (int r = 0; r < wobbly::Model::GRID_H; ++r) {
        for (int c = 0; c < wobbly::Model::GRID_W; ++c) {
            const int i = r * wobbly::Model::GRID_W + c;
            const float u = static_cast<float>(c) / (wobbly::Model::GRID_W - 1);
            const float v = static_cast<float>(r) / (wobbly::Model::GRID_H - 1);
            const float nx = posX[i] * invW * 2.0f - 1.0f;
            const float ny = 1.0f - posY[i] * invH * 2.0f;
            m_arenaVerts[i] = {nx, ny, u, v};
        }
    }

    // Build indices once (topology constant for 4x4 grid)
    if (!m_indicesBuilt) {
        int idx = 0;
        for (int r = 0; r < wobbly::Model::GRID_H - 1; ++r) {
            for (int c = 0; c < wobbly::Model::GRID_W - 1; ++c) {
                const uint16_t tl = r * wobbly::Model::GRID_W + c;
                const uint16_t tr = tl + 1;
                const uint16_t bl = tl + wobbly::Model::GRID_W;
                const uint16_t br = bl + 1;
                m_arenaIndices[idx++] = tl;
                m_arenaIndices[idx++] = bl;
                m_arenaIndices[idx++] = tr;
                m_arenaIndices[idx++] = tr;
                m_arenaIndices[idx++] = bl;
                m_arenaIndices[idx++] = br;
            }
        }
        m_indicesBuilt = true;
        tlog("Index buffer built: " + std::to_string(IDX_COUNT) + " indices");
    }
}

SP<Render::IFramebuffer> WobblyTransformer::transform(
    SP<Render::IFramebuffer> in) {
    
    m_frameCount++;

    // ── NULL CHECK ──
    if (!in) {
        if (m_frameCount % 60 == 0) // Log once per second max
            tlog("transform: received null framebuffer (frame " + 
                 std::to_string(m_frameCount) + ")");
        return in; // Pass through unchanged
    }

    if (!m_model) {
        tlog("ERROR: model is null!");
        return in;
    }

    // ── LAZY GL INIT (first call only) ──
    if (!m_ready) {
        tlog("Initializing GL resources...");

        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_vbo);
        glGenBuffers(1, &m_ebo);
        
        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(WVertex), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(WVertex), (void*)(2*sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);

        if (!m_shader.createProgram(VERT_SRC, FRAG_SRC)) {
            tlog("FATAL: Shader compilation failed!");
            return in;
        }
        
        m_texLoc = glGetUniformLocation(m_shader.program(), "tex");
        m_outFB  = makeShared<Render::GL::CGLFramebuffer>("wobbly-out");
        m_ready  = true;
        tlog("GL init SUCCESS");
    }

    // ── VALIDATE INPUT SIZE ──
    if (in->m_size.x <= 0 || in->m_size.y <= 0) {
        if (m_frameCount % 60 == 0)
            tlog("transform: invalid size " + 
                 std::to_string(in->m_size.x) + "x" + 
                 std::to_string(in->m_size.y));
        return in;
    }

    // ── ALLOCATE OUTPUT FRAMEBUFFER ──
    if (!m_outFB->isAllocated() || m_outFB->m_size != in->m_size) {
        if (!m_outFB->alloc((int)in->m_size.x, (int)in->m_size.y)) {
            tlog("ERROR: Framebuffer alloc failed!");
            return in;
        }
    }

    // ── BUILD MESH (into arena, no heap alloc) ──
    buildMesh(in->m_size);

    // ── RENDER ──
    m_outFB->bind();
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // State caching
    const GLuint prog = m_shader.program();
    if (m_boundProg != prog) {
        glUseProgram(prog);
        m_boundProg = prog;
    }
    
    if (m_boundVAO != m_vao) {
        glBindVertexArray(m_vao);
        m_boundVAO = m_vao;
    }

    // Texture binding
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, in->getTexture()->m_texID);
    glUniform1i(m_texLoc, 0);

    // Buffer orphaning technique (avoids GPU sync on NVIDIA)
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(m_arenaVerts), nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(m_arenaVerts), m_arenaVerts);

    // Index buffer (upload once)
    if (m_indicesBuilt) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(m_arenaIndices),
                     m_arenaIndices, GL_STATIC_DRAW);
        m_indicesBuilt = false; // Only upload once
    }

    // Draw call
    glDrawElements(GL_TRIANGLES, IDX_COUNT, GL_UNSIGNED_SHORT, nullptr);

    // Error check (debug builds only)
#ifdef WOBBLY_DEBUG
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        tlog("GL error: " + std::to_string(err));
    }
#endif

    return m_outFB;
}
