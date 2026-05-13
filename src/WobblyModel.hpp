// Spring-mass wobbly window physics, ported from
// https://github.com/hermes83/compiz-windows-effect (Mauro Pepe), which in
// turn derives from the original Compiz wobbly plugin by Reveman/Moreau
// (Novell, 2005). Spring model by Kristian Hogsberg.

#pragma once

#include <array>
#include <vector>

namespace wobbly {

struct Config {
    double sizeX;
    double sizeY;
    double friction; // 1..10
    double springK;  // 1..10
    double mass;     // 1..99 (lower = heavier; matches upstream's `100 - mass`)
};

struct Object {
    double forceX = 0, forceY = 0;
    double x = 0, y = 0;
    double velocityX = 0, velocityY = 0;
    bool   immobile = false;
};

struct Spring {
    Object* a;
    Object* b;
    double  offsetX;
    double  offsetY;
};

class Model {
  public:
    static constexpr int GRID_W = 4;
    static constexpr int GRID_H = 4;
    static constexpr int N      = GRID_W * GRID_H;

    explicit Model(const Config& c);

    void grab(double x, double y);
    void move(double dx, double dy);
    void maximize();
    void unmaximize();
    void step(int extraSteps);

    bool   moving() const { return m_movement; }
    double width() const { return m_width; }
    double height() const { return m_height; }

    const std::array<Object, N>& objects() const { return m_objects; }

  private:
    Object* nearestObject(double x, double y);

    std::array<Object, N> m_objects{};
    std::vector<Spring>   m_springs;

    double  m_width;
    double  m_height;
    double  m_friction;
    double  m_springK;
    double  m_mass;
    bool    m_movement      = false;
    Object* m_immobile      = nullptr;
    double  m_intensity     = 0.8;
};

} // namespace wobbly
