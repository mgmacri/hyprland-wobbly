#include "WobblyModel.hpp"

#include <cmath>

namespace wobbly {

Model::Model(const Config& c)
    : m_width(c.sizeX),
      m_height(c.sizeY),
      m_friction(c.friction),
      m_springK(c.springK * 0.5),
      m_mass(100.0 - c.mass) {

    const double hpad = m_width / (GRID_W - 1);
    const double vpad = m_height / (GRID_H - 1);

    int i = 0;
    for (int gy = 0; gy < GRID_H; ++gy) {
        for (int gx = 0; gx < GRID_W; ++gx) {
            m_objects[i].x = gx * hpad;
            m_objects[i].y = gy * vpad;
            ++i;
        }
    }

    m_springs.reserve(2 * N);
    i = 0;
    for (int gy = 0; gy < GRID_H; ++gy) {
        for (int gx = 0; gx < GRID_W; ++gx) {
            if (gx > 0)
                m_springs.push_back({&m_objects[i - 1], &m_objects[i], hpad, 0});
            if (gy > 0)
                m_springs.push_back({&m_objects[i - GRID_W], &m_objects[i], 0, vpad});
            ++i;
        }
    }
}

Object* Model::nearestObject(double x, double y) {
    Object* best        = nullptr;
    double  bestDist    = -1.0;
    for (auto& o : m_objects) {
        const double d = std::abs(o.x - x) + std::abs(o.y - y);
        if (bestDist < 0 || d < bestDist) {
            bestDist = d;
            best     = &o;
        }
    }
    return best;
}

void Model::grab(double x, double y) {
    m_immobile           = nearestObject(x, y);
    m_immobile->immobile = true;
}

void Model::move(double dx, double dy) {
    if (!m_immobile)
        return;
    m_immobile->x += dx;
    m_immobile->y += dy;
}

static void kickNeighbours(std::vector<Spring>& springs, const Object* pin, double intensity) {
    for (auto& s : springs) {
        if (s.a == pin) {
            s.b->velocityX -= s.offsetX * intensity;
            s.b->velocityY -= s.offsetY * intensity;
        } else if (s.b == pin) {
            s.a->velocityX -= s.offsetX * intensity;
            s.a->velocityY -= s.offsetY * intensity;
        }
    }
}

void Model::maximize() {
    m_immobile  = nullptr;
    auto* tl    = nearestObject(0, 0);
    auto* tr    = nearestObject(m_width, 0);
    auto* bl    = nearestObject(0, m_height);
    auto* br    = nearestObject(m_width, m_height);
    tl->immobile = tr->immobile = bl->immobile = br->immobile = true;

    m_friction = std::min(m_friction * 2.0, 10.0);

    for (auto* pin : {tl, tr, bl, br})
        kickNeighbours(m_springs, pin, m_intensity);

    step(0);
}

void Model::unmaximize() {
    m_immobile           = nearestObject(m_width / 2, m_height / 2);
    m_immobile->immobile = true;
    m_friction           = std::min(m_friction * 2.0, 10.0);
    kickNeighbours(m_springs, m_immobile, m_intensity);
    step(0);
}

void Model::step(int extraSteps) {
    bool moved = false;

    for (int j = extraSteps; j >= 0; --j) {
        for (auto& s : m_springs) {
            const double fx = m_springK * (s.b->x - s.a->x - s.offsetX);
            s.a->forceX += fx;
            s.b->forceX -= fx;
            const double fy = m_springK * (s.b->y - s.a->y - s.offsetY);
            s.a->forceY += fy;
            s.b->forceY -= fy;
        }

        for (auto& o : m_objects) {
            if (o.immobile)
                continue;
            o.forceX -= m_friction * o.velocityX;
            o.forceY -= m_friction * o.velocityY;
            o.velocityX += o.forceX / m_mass;
            o.velocityY += o.forceY / m_mass;
            o.x += o.velocityX;
            o.y += o.velocityY;

            moved |= std::abs(o.forceX) > 1.0 || std::abs(o.forceY) > 1.0;

            o.forceX = 0;
            o.forceY = 0;
        }
    }

    m_movement = moved;
}

} // namespace wobbly
