// ============================================================================
// 1. Standard headers (must be before #define private public)
// ============================================================================
#include <unordered_map>
#include <memory>
#include <cstdio>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <any>
#include <sstream>

// ============================================================================
// 2. Hyprland headers with private → public
// ============================================================================
#define private public
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#undef private

// ============================================================================
// 3. Remaining Hyprland headers
// ============================================================================
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/types.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/version.h>

#include <hyprutils/memory/UniquePtr.hpp>

#include "WobblyModel.hpp"
#include "WobblyTransformer.hpp"

// ----------------------------------------------------------------------------
// Logging
// ----------------------------------------------------------------------------
static void logInfo(const std::string& msg) {
    using namespace std::chrono;
    const auto ms = duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
    fprintf(stderr, "[wobbly %lld] INFO: %s\n",
            static_cast<long long>(ms), msg.c_str());
    fflush(stderr);
}

inline HANDLE PHANDLE = nullptr;

// Hardcoded physics constants
static constexpr double DEFAULT_FRICTION = 3.0;
static constexpr double DEFAULT_SPRING_K = 8.0;
static constexpr double DEFAULT_MASS     = 50.0;

static wobbly::Config defaultConfig(const Vector2D& size) {
    return wobbly::Config{
        .sizeX    = std::max(size.x, 1.0),
        .sizeY    = std::max(size.y, 1.0),
        .friction = DEFAULT_FRICTION,
        .springK  = DEFAULT_SPRING_K,
        .mass     = DEFAULT_MASS,
    };
}

static std::unordered_map<uintptr_t, std::unique_ptr<wobbly::Model>> g_models;
static std::unordered_map<uintptr_t, PHLWINDOWREF>                   g_windows;
static std::unordered_map<uintptr_t, CHyprSignalListener>            g_windowListeners;
static std::vector<CHyprSignalListener>                              g_listeners;
static std::unordered_map<uintptr_t, WobblyTransformer*>             g_transformers;

struct DragState {
    Vector2D lastPos{0,0};
    bool dragging = false;
    int zeroFrames = 0;
    bool initialized = false;
};
static std::unordered_map<uintptr_t, DragState> g_drag;
static int g_windowCount = 0, g_tickCount = 0;

static wobbly::Model* modelFor(PHLWINDOW w) {
    if (!w) return nullptr;
    auto it = g_models.find((uintptr_t)w.get());
    return (it == g_models.end()) ? nullptr : it->second.get();
}

static void onOpenEarly(PHLWINDOW w) {
    if (!w) return;
    const uintptr_t key = (uintptr_t)w.get();
    if (g_models.count(key)) return;

    const auto sz = w->m_realSize->goal();
    if (sz.x <= 0 || sz.y <= 0) return;

    auto m = std::make_unique<wobbly::Model>(defaultConfig(sz));
    m->maximize();

    g_windowCount++;
    logInfo("openEarly: win=" + std::to_string(key) +
            " sz=" + std::to_string((int)sz.x) + "x" + std::to_string((int)sz.y) +
            " total=" + std::to_string(g_windowCount));

    auto* rawModel = m.get();
    g_models[key] = std::move(m);
    g_windows[key] = PHLWINDOWREF(w);
    g_drag[key] = DragState{w->m_realPosition->value(), false, 0, true};

    auto transformer = Hyprutils::Memory::makeUnique<WobblyTransformer>(rawModel);
    g_transformers[key] = transformer.get();
    w->m_transformers.push_back(std::move(transformer));

    g_windowListeners[key] = w->m_events.resize.listen([wptr = PHLWINDOWREF(w)](){
        auto w2 = wptr.lock();
        if (!w2) return;
        auto* model = modelFor(w2);
        if (!model) return;
        const auto newSize = w2->m_realSize->goal();
        if (newSize.x > 0 && newSize.y > 0) {
            *model = wobbly::Model(defaultConfig(newSize));
            model->unmaximize();
        }
    });
}

static void onClose(PHLWINDOW w) {
    if (!w) return;
    const uintptr_t key = (uintptr_t)w.get();
    g_windowListeners.erase(key);
    g_transformers.erase(key);
    g_drag.erase(key);
    g_windows.erase(key);
    g_models.erase(key);
    g_windowCount--;
}

static void onTick() {
    g_tickCount++;
    constexpr int PHYSICS_TICK_DIVISOR = 2;
    if ((g_tickCount % PHYSICS_TICK_DIVISOR) != 0) return;

    for (auto& [key, m] : g_models) {
        auto w = g_windows[key].lock();
        if (!w) continue;

        auto& ds = g_drag[key];
        Vector2D cur = w->m_realPosition->value();

        if (!ds.initialized) {
            ds.lastPos = cur;
            ds.initialized = true;
            continue;
        }

        Vector2D delta = cur - ds.lastPos;
        constexpr double THRESHOLD = 0.5;
        bool hasDelta = (std::abs(delta.x) > THRESHOLD || std::abs(delta.y) > THRESHOLD);

        if (hasDelta) {
            if (!ds.dragging) {
                Vector2D mouse = g_pInputManager->getMouseCoordsInternal();
                Vector2D local = mouse - cur;
                m->grab(local.x, local.y);
                ds.dragging = true;
            } else {
                m->move(delta.x, delta.y);
            }
            ds.zeroFrames = 0;
        } else if (ds.dragging) {
            if (++ds.zeroFrames >= 2) {
                m->release();
                ds.dragging = false;
                ds.zeroFrames = 0;
            }
        }

        ds.lastPos = cur;

        if (m->moving()) {
            m->step(2);
            g_pHyprRenderer->damageWindow(w);
        }
    }
}

// ----------------------------------------------------------------------------
// Plugin entry points
// ----------------------------------------------------------------------------
APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string CH = __hyprland_api_get_hash();
    const std::string CL = __hyprland_api_get_client_hash();
    if (CH != CL) {
        HyprlandAPI::addNotification(PHANDLE, "[wobbly] Version mismatch!",
            CHyprColor{1.0,0.2,0.2,1.0}, 8000);
        throw std::runtime_error("Version mismatch");
    }

    logInfo("Version check passed");

    auto& bus = Event::bus();
    g_listeners.push_back(bus->m_events.window.openEarly.listen([](PHLWINDOW w){ onOpenEarly(w); }));
    g_listeners.push_back(bus->m_events.window.close.listen([](PHLWINDOW w){ onClose(w); }));
    g_listeners.push_back(bus->m_events.window.destroy.listen([](PHLWINDOW w){ onClose(w); }));
    g_listeners.push_back(bus->m_events.tick.listen([] { onTick(); }));
    g_listeners.push_back(bus->m_events.window.fullscreen.listen([](PHLWINDOW w){
        auto* m = modelFor(w);
        if (!m) return;
        if (w->m_fullscreenState.internal & FSMODE_MAXIMIZED)
            m->maximize();
        else
            m->unmaximize();
    }));

    HyprlandAPI::addNotification(PHANDLE, "[wobbly] Loaded – drag windows!",
        CHyprColor{0.4,0.9,0.5,1.0}, 4000);
    return {"hyprland-wobbly", "Wobbly windows", "mgmacri", "0.3.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    for (auto& [key, raw] : g_transformers) {
        if (auto w = g_windows[key].lock()) {
            auto& tv = w->m_transformers;
            tv.erase(std::remove_if(tv.begin(), tv.end(),
                [raw](const auto& t) { return t.get() == raw; }), tv.end());
        }
    }
    g_listeners.clear();
    g_windowListeners.clear();
    g_transformers.clear();
    g_drag.clear();
    g_windows.clear();
    g_models.clear();
}
