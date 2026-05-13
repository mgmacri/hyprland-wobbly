#pragma once
// SOA Physics Engine — Cache-Oblivious Design
#include <array>
#include <vector>
#include <cstdint>

namespace wobbly {

struct Config {
    double sizeX, sizeY;
    double friction; // 1..10
    double springK;  // 1..10  
    double mass;     // 1..99 (lower=heavier)
};

class Model {
public:
    static constexpr int GRID_W = 4;
    static constexpr int GRID_H = 4;
    static constexpr int N      = GRID_W * GRID_H; // 16 points

    explicit Model(const Config& c);

    void grab(double x, double y);
    void move(double dx, double dy);
    void release();
    void maximize();
    void unmaximize();
    void step(int extraSteps);

    [[nodiscard]] bool   moving() const { return m_movement; }
    [[nodiscard]] bool   grabbed() const { return m_immobileIdx >= 0; }
    [[nodiscard]] double width() const { return m_width; }
    [[nodiscard]] double height() const { return m_height; }

    [[nodiscard]] const float* posX() const { return m_posX.data(); }
    [[nodiscard]] const float* posY() const { return m_posY.data(); }

private:
    int nearestObject(double x, double y) const;
    static void kickNeighbours(std::vector<Spring>& springs,
                               const int pinIdx, double intensity,
                               const float* posX, const float* posY,
                               float* velX, float* velY);

    // SOA arrays (AVX-friendly, cache-line aligned)
    alignas(32) std::array<float, N>  m_posX{}, m_posY{};
    alignas(32) std::array<float, N>  m_velX{}, m_velY{};
    alignas(32) std::array<float, N>  m_forceX{}, m_forceY{};
    std::array<uint8_t, N>            m_immobile{}; // packed flags

    std::vector<Spring> m_springs;

    double  m_width{}, m_height{}, m_friction{}, m_springK{}, m_mass{};
    bool    m_movement = false;
    int     m_immobileIdx = -1;
    double  m_intensity = 0.8;
};

struct Spring {
    int a, b;          // indices into SOA arrays
    double offsetX, offsetY;
};

} // namespace wobbly
