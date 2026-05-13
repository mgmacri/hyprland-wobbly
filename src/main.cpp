#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/version.h>

#include "WobblyModel.hpp"

#include <unordered_map>
#include <memory>
#include <stdexcept>

inline HANDLE                                                       PHANDLE = nullptr;
static std::unordered_map<uintptr_t, std::unique_ptr<wobbly::Model>> g_models;
static std::vector<CHyprSignalListener>                              g_listeners;

static wobbly::Config readConfig(const Vector2D& size) {
    // Defaults match upstream compiz-windows-effect.
    return wobbly::Config{
        .sizeX    = size.x,
        .sizeY    = size.y,
        .friction = 3.0,
        .springK  = 8.0,
        .mass     = 50.0,
    };
}

static wobbly::Model* modelFor(PHLWINDOW w) {
    if (!w)
        return nullptr;
    auto it = g_models.find((uintptr_t)w.get());
    return it == g_models.end() ? nullptr : it->second.get();
}

static void onOpen(PHLWINDOW w) {
    if (!w)
        return;
    const auto sz                = w->m_realSize->goal();
    g_models[(uintptr_t)w.get()] = std::make_unique<wobbly::Model>(readConfig(sz));
}

static void onClose(PHLWINDOW w) {
    if (w)
        g_models.erase((uintptr_t)w.get());
}

static void onTick() {
    bool anyMoving = false;
    for (auto& [id, m] : g_models) {
        if (m->moving()) {
            m->step(0);
            anyMoving = true;
        }
    }
    if (anyMoving)
        g_pCompositor->scheduleFrameForMonitor(g_pCompositor->getMonitorFromCursor());
}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    auto& bus = Event::bus();
    g_listeners.push_back(bus->m_events.window.open.listen([](PHLWINDOW w) { onOpen(w); }));
    g_listeners.push_back(bus->m_events.window.close.listen([](PHLWINDOW w) { onClose(w); }));
    g_listeners.push_back(bus->m_events.window.destroy.listen([](PHLWINDOW w) { onClose(w); }));
    g_listeners.push_back(bus->m_events.tick.listen([] { onTick(); }));

    HyprlandAPI::addNotification(PHANDLE,
        "[hyprland-wobbly] loaded (physics only — render deformation pending).",
        CHyprColor{0.9, 0.7, 0.4, 1.0}, 4000);

    return {"hyprland-wobbly", "Compiz-style wobbly windows for Hyprland.", "mgmacri", "0.1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_listeners.clear();
    g_models.clear();
}
