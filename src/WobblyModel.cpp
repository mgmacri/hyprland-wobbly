#include "WobblyModel.hpp"
#include <cmath>
#include <algorithm>

namespace wobbly {

Model::Model(const Config& c)
    : m_width(c.sizeX), m_height(c.sizeY),
      m_friction(c.friction), m_springK(c.springK * 0.5),
      m_mass(100.0 - c.mass) {

    const double hpad = m_width / (GRID_W - 1);
    const double vpad = m_height / (GRID_H - 1);

    int i = 0;
    for (int gy = 0; gy < GRID_H; ++gy) {
        for (int gx = 0; gx < GRID_W; ++gx) {
            m_posX[i] = static_cast<float>(gx * hpad);
            m_posY[i] = static_cast<float>(gy * vpad);
            ++i;
        }
    }

    m_springs.reserve(2 * N - GRID_W - GRID_H);
    i = 0;
    for (int gy = 0; gy < GRID_H; ++gy) {
        for (int gx = 0; gx < GRID_W; ++gx) {
            if (gx > 0)
                m_springs.push_back({i-1, i, hpad, 0.0});
            if (gy > 0)
                m_springs.push_back({i - GRID_W, i, 0.0, vpad});
            ++i;
        }
    }
}

int Model::nearestObject(double x, double y) const {
    int best = 0;
    double bestDist = 1e20;
    for (int i = 0; i < N; ++i) {
        const double d = std::abs(m_posX[i] - x) + std::abs(m_posY[i] - y);
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

void Model::grab(double x, double y) {
    for (auto& f : m_immobile) f = 0;
    m_immobileIdx = nearestObject(x, y);
    m_immobile[m_immobileIdx] = 1;
}

void Model::release() {
    if (m_immobileIdx >= 0) {
        m_immobile[m_immobileIdx] = 0;
        m_immobileIdx = -1;
    }
}

void Model::move(double dx, double dy) {
    if (m_immobileIdx < 0) return;
    m_posX[m_immobileIdx] += static_cast<float>(dx);
    m_posY[m_immobileIdx] += static_cast<float>(dy);
}

void Model::kickNeighbours(std::vector<Spring>& springs,
                           int pinIdx, double intensity,
                           float* velX, float* velY) {
    for (auto& s : springs) {
        if (s.a == pinIdx) {
            velX[s.b] -= static_cast<float>(s.offsetX * intensity);
            velY[s.b] -= static_cast<float>(s.offsetY * intensity);
        } else if (s.b == pinIdx) {
            velX[s.a] -= static_cast<float>(s.offsetX * intensity);
            velY[s.a] -= static_cast<float>(s.offsetY * intensity);
        }
    }
}

void Model::maximize() {
    m_immobileIdx = -1;
    for (auto& f : m_immobile) f = 0;

    auto tl = nearestObject(0, 0);
    auto tr = nearestObject(m_width, 0);
    auto bl = nearestObject(0, m_height);
    auto br = nearestObject(m_width, m_height);

    m_immobile[tl] = m_immobile[tr] = m_immobile[bl] = m_immobile[br] = 1;
    m_friction = std::min(m_friction * 2.0, 10.0);

    for (auto pin : {tl, tr, bl, br})
        kickNeighbours(m_springs, pin, m_intensity,
                       m_velX.data(), m_velY.data());
    step(0);
}

void Model::unmaximize() {
    m_immobileIdx = nearestObject(m_width / 2, m_height / 2);
    for (auto& f : m_immobile) f = 0;
    m_immobile[m_immobileIdx] = 1;
    m_friction = std::min(m_friction * 2.0, 10.0);
    kickNeighbours(m_springs, m_immobileIdx, m_intensity,
                   m_velX.data(), m_velY.data());
}

void Model::step(int extraSteps) {
    m_movement = false;

    const float fric = static_cast<float>(m_friction);
    const float sk   = static_cast<float>(m_springK);
    const float invM = 1.0f / static_cast<float>(m_mass);

    float* fx = m_forceX.data();
    float* fy = m_forceY.data();
    float* px = m_posX.data();      // was const – now mutable
    float* py = m_posY.data();
    float* vx = m_velX.data();
    float* vy = m_velY.data();
    const uint8_t* im = m_immobile.data();

    for (int j = extraSteps; j >= 0; --j) {
        // Spring forces
        for (const auto& s : m_springs) {
            const float dx = px[s.b] - px[s.a] - static_cast<float>(s.offsetX);
            const float dy = py[s.b] - py[s.a] - static_cast<float>(s.offsetY);
            const float ffx = sk * dx;   // added 'float'
            const float ffy = sk * dy;
            fx[s.a] += ffx; fx[s.b] -= ffx;
            fy[s.a] += ffy; fy[s.b] -= ffy;
        }

        // Integration
        for (int i = 0; i < N; ++i) {
            if (im[i]) continue;

            fx[i] -= fric * vx[i];
            fy[i] -= fric * vy[i];
            vx[i] += fx[i] * invM;
            vy[i] += fy[i] * invM;
            px[i] += vx[i];
            py[i] += vy[i];

            m_movement |= (std::abs(fx[i]) > 0.01f || std::abs(fy[i]) > 0.01f);

            fx[i] = 0.0f;
            fy[i] = 0.0f;
        }
    }
}

} // namespace wobbly
