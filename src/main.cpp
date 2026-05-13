// main.cpp — Hyprland-Wobbly Plugin Entry Point
// Version: 0.3.0-fix1
// Compliant with Hyprland Plugin API v0.1

// ── Access private members (required for Window internals) ──
#define private public
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/desktop/Window.hpp>
#undef private

#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/types.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>

using Render::eRenderPassMode;
#include <hyprland/src/version.h>

#include "WobblyModel.hpp"
#include "WobblyTransformer.hpp"

#include <unordered_map>
#include <memory>
#include <cstdio>
#include <chrono>
#include <cmath>

// ============================================================================
// LOGGING SYSTEM (writes to journalctl -f -t Hyprland)
// ============================================================================
static void logInfo(const std::string& msg) {
    using namespace std::chrono;
    const auto ms = duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
    // Use fprintf to stderr (captured by Hyprland's logger)
    fprintf(stderr, "[wobbly %lld] INFO: %s\n", 
            static_cast<long long>(ms), msg.c_str());
    fflush(stderr);
}

static void logError(const std::string& msg) {
    using namespace std::chrono;
    const auto ms = duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
    fprintf(stderr, "[wobbly %lld] ERROR: %s\n", 
            static_cast<long long>(ms), msg.c_str());
    fflush(stderr);
}

#ifdef WOBBLY_DEBUG
#define DBG_LOG(msg) logInfo(msg)
#else
#define DBG_LOG(msg) 
#endif

inline HANDLE PHANDLE = nullptr;

// ============================================================================
// CONFIG VALUES (live-reloadable pointers)
// ============================================================================
static float* CFG_FRICTION  = nullptr;
static float* CFG_SPRING_K  = nullptr;
static float* CFG_MASS      = nullptr;

// ============================================================================
// GLOBAL STATE
// ============================================================================
static std::unordered_map<uintptr_t, std::unique_ptr<wobbly::Model>> g_models;
static std::unordered_map<uintptr_t, PHLWINDOWREF>                   g_windows;
static std::unordered_map<uintptr_t, CHyprSignalListener>            g_windowListeners;
static std::vector<CHyprSignalListener>                              g_listeners;

// Raw transformer pointers for cleanup
static std::unordered_map<uintptr_t, WobblyTransformer*>             g_transformers;

// Drag state per window
struct DragState {
    Vector2D lastPos     = {0, 0};
    bool     dragging    = false;
    int      zeroFrames  = 0;
    bool     initialized = false;
};
static std::unordered_map<uintptr_t, DragState> g_drag;

// Stats counters (for debugging)
static int g_windowCount   = 0;
static int g_tickCount     = 0;
static int g_renderCount   = 0;

// ============================================================================
// HELPERS
// ============================================================================

static wobbly::Config defaultConfig(const Vector2D& size) {
    return wobbly::Config{
        .sizeX    = std::max(size.x, 1.0),
        .sizeY    = std::max(size.y, 1.0),
        .friction = CFG_FRICTION ? (double)*CFG_FRICTION : 3.0,
        .springK  = CFG_SPRING_K ? (double)*CFG_SPRING_K : 8.0,
        .mass     = CFG_MASS     ? (double)*CFG_MASS     : 50.0,
    };
}

static wobbly::Model* modelFor(PHLWINDOW w) {
    if (!w) return nullptr;
    auto it = g_models.find((uintptr_t)w.get());
    return (it == g_models.end()) ? nullptr : it->second.get();
}

// ============================================================================
// EVENT HANDLERS
// ============================================================================

static void onOpenEarly(PHLWINDOW w) {
    if (!w) {
        logError("onOpenEarly: received null window!");
        return;
    }

    const uintptr_t key = (uintptr_t)w.get();
    
    // Skip if already tracked (shouldn't happen, but safety)
    if (g_models.count(key)) {
        DBG_LOG("onOpenEarly: window already tracked, skipping");
        return;
    }

    const auto sz = w->m_realSize->goal();
    
    // Validate size
    if (sz.x <= 0 || sz.y <= 0) {
        logError("onOpenEarly: invalid size " + 
                 std::to_string(sz.x) + "x" + std::to_string(sz.y));
        return;
    }

    auto m = std::make_unique<wobbly::Model>(defaultConfig(sz));
    
    // Initialize with maximize effect (spreads grid)
    m->maximize();
    
    g_windowCount++;
    logInfo("openEarly: win=" + std::to_string(key) + 
            " sz=" + std::to_string((int)sz.x) + "x" + std::to_string((int)sz.y) +
            " (total windows: " + std::to_string(g_windowCount) + ")");

    auto* rawModel = m.get();
    g_models[key]  = std::move(m);
    g_windows[key] = PHLWINDOWREF(w);
    g_drag[key]    = DragState{w->m_realPosition->value(), false, 0, true};

    // ── ATTACH TRANSFORMER ──
    // This modifies the render pipeline for this window
    auto transformer = std::make_unique<WobblyTransformer>(rawModel);
    g_transformers[key] = transformer.get();
    
    // Push to window's transformer chain (Hyprland calls these in order during render)
    w->m_transformers.push_back(std::move(transformer));
    
    logInfo("openEarly: transformers count=" + 
            std::to_string(w->m_transformers.size()) + 
            " (expected >= 1)");

    // ── RESIZE LISTENER ──
    // Re-kick physics when window resizes
    g_windowListeners[key] = w->m_events.resize.listen(
        [wptr = PHLWINDOWREF(w)]() {
            auto w2 = wptr.lock();
            if (!w2) return;
            
            auto* model = modelFor(w2);
            if (!model) return;
            
            // Re-initialize grid with new size
            const auto newSize = w2->m_realSize->goal();
            if (newSize.x > 0 && newSize.y > 0) {
                *model = wobbly::Model(defaultConfig(newSize));
                model->unmaximize();
                DBG_LOG("resize: re-initialized model");
            }
        });
}

static void onClose(PHLWINDOW w) {
    if (!w) return;
    
    const uintptr_t key = (uintptr_t)w.get();
    logInfo("onClose: removing win=" + std::to_string(key));
    
    g_windowListeners.erase(key);
    g_transformers.erase(key);
    g_drag.erase(key);
    g_windows.erase(key);
    g_models.erase(key);
    
    g_windowCount--;
}

static void onTick() {
    g_tickCount++;
    
    // Throttle: only process every 2nd tick at 144Hz (~72Hz physics)
    // Remove this throttle if you want full precision
    constexpr int PHYSICS_TICK_DIVISOR = 2;
    if ((g_tickCount % PHYSICS_TICK_DIVISOR) != 0) return;

    for (auto& [key, m] : g_models) {
        auto w = g_windows[key].lock();
        if (!w) continue; // window was destroyed

        // ── DRAG DETECTION ──
        auto&    ds    = g_drag[key];
        Vector2D cur   = w->m_realPosition->value();
        
        // Validate current position
        if (!ds.initialized) {
            ds.lastPos     = cur;
            ds.initialized = true;
            continue;
        }
        
        Vector2D delta = cur - ds.lastPos;
        
        // Use threshold to filter noise (sub-pixel jitter)
        constexpr double THRESHOLD = 0.5;
        bool hasDelta = (std::abs(delta.x) > THRESHOLD || 
                        std::abs(delta.y) > THRESHOLD);

        if (hasDelta) {
            if (!ds.dragging) {
                // GRAB: calculate local coords relative to window top-left
                Vector2D mouse = g_pInputManager->getMouseCoordsInternal();
                Vector2D local = mouse - cur;
                
                m->grab(local.x, local.y);
                ds.dragging = true;
                
                DBG_LOG("drag START: win=" + std::to_string(key) +
                       " local=(" + std::to_string(local.x) + "," + 
                       std::to_string(local.y) + ")");
            } else {
                // MOVE: delta since last frame
                m->move(delta.x, delta.y);
            }
            ds.zeroFrames = 0;
        } else if (ds.dragging) {
            // Detect release: 2 consecutive frames with no movement
            if (++ds.zeroFrames >= 2) {
                m->release();
                ds.dragging = false;
                DBG_LOG("drag END: win=" + std::to_string(key));
                ds.zeroFrames = 0;
            }
        }
        
        ds.lastPos = cur;

        // ── PHYSICS STEP ──
        if (m->moving()) {
            // extraSteps=2 for stability at high refresh rates
            m->step(2); 
            
            // Request repaint of this window's area
            g_pHyprRenderer->damageWindow(w);
        }
    }
}

// ============================================================================
// PLUGIN ENTRY POINTS
// ============================================================================

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    // DO NOT MODIFY - must match server exactly
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    // ════════════════════════════════════════════════════════
    // VERSION CHECK (MANDATORY - prevents random crashes)
    // ════════════════════════════════════════════════════════
    const std::string COMPOSITOR_HASH = __hyprland_api_get_hash();
    const std::string CLIENT_HASH    = __hyprland_api_get_client_hash();

    logInfo("=== INIT: Version Check ===");
    logInfo("Compositor hash: " + COMPOSITOR_HASH.substr(0, 20) + "...");
    logInfo("Client hash: " + CLIENT_HASH.substr(0, 20) + "...");

    if (COMPOSITOR_HASH != CLIENT_HASH) {
        HyprlandAPI::addNotification(
            PHANDLE,
            "[wobbly] ❌ Mismatched headers! Recompile plugin.",
            CHyprColor{1.0, 0.2, 0.2, 1.0}, 
            8000
        );
        
        // Throw to prevent loading with bad ABI
        throw std::runtime_error(
            "[wobbly] Version mismatch!\n"
            "Compositor: " + COMPOSITOR_HASH + "\n"
            "Plugin: " + CLIENT_HASH + "\n"
            "Run 'sudo make installheaders' in Hyprland source, then recompile plugin."
        );
    }

    logInfo("✓ Version check passed");

    // ════════════════════════════════════════════════════════
    // REGISTER CONFIG VALUES (only allowed here)
    // ════════════════════════════════════════════════════════
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:wobbly:friction", 
                                SConfigValue{.floatValue = 3.0f});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:wobbly:spring_k", 
                                SConfigValue{.floatValue = 8.0f});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:wobbly:mass", 
                                SConfigValue{.floatValue = 50.0f});

    // Get pointers (valid for plugin lifetime)
    CFG_FRICTION = &HyprlandAPI::getConfigValue(
        PHANDLE, "plugin:wobbly:friction")->floatValue;
    CFG_SPRING_K = &HyprlandAPI::getConfigValue(
        PHANDLE, "plugin:wobbly:spring_k")->floatValue;
    CFG_MASS = &HyprlandAPI::getConfigValue(
        PHANDLE, "plugin:wobbly:mass")->floatValue;

    logInfo("✓ Config registered: friction=" + std::to_string(*CFG_FRICTION) +
           " spring_k=" + std::to_string(*CFG_SPRING_K) +
           " mass=" + std::to_string(*CFG_MASS));

    // ════════════════════════════════════════════════════════
    // SUBSCRIBE TO EVENTS
    // ════════════════════════════════════════════════════════
    auto& bus = Event::bus();

    // Window lifecycle
    g_listeners.push_back(
        bus->m_events.window.openEarly.listen([](PHLWINDOW w) { 
            onOpenEarly(w); 
        })
    );
    g_listeners.push_back(
        bus->m_events.window.close.listen([](PHLWINDOW w) { 
            onClose(w); 
        })
    );
    g_listeners.push_back(
        bus->m_events.window.destroy.listen([](PHLWINDOW w) { 
            onClose(w); 
        })
    );

    // Physics tick (fires every display frame)
    g_listeners.push_back(
        bus->m_events.tick.listen([] { 
            onTick(); 
        })
    );

    // Fullscreen/maximize events
    g_listeners.push_back(
        bus->m_events.window.fullscreen.listen([](PHLWINDOW w) {
            auto* m = modelFor(w);
            if (!m) return;
            
            if (w->m_fullscreenState.internal & FSMODE_MAXIMIZED) {
                m->maximize();
                DBG_LOG("fullscreen: MAXIMIZE");
            } else {
                m->unmaximize();
                DBG_LOG("fullscreen: UNMAXIMIZE");
            }
        })
    );

    logInfo("✓ Event listeners registered (" + 
           std::to_string(g_listeners.size()) + " total)");

    // ════════════════════════════════════════════════════════
    // SUCCESS NOTIFICATION
    // ════════════════════════════════════════════════════════
    HyprlandAPI::addNotification(
        PHANDLE,
        "[wobbly] ✅ Loaded successfully — drag windows to test!",
        CHyprColor{0.4, 0.9, 0.5, 1.0}, 
        4000
    );

    return {
        "hyprland-wobbly",
        "Compiz-style wobbly windows for Hyprland",
        "mgmacri",
        "0.3.0"
    };
}

APICALL EXPORT void PLUGIN_EXIT() {
    logInfo("=== EXIT: Cleaning up ===");
    
    // Remove transformers from all alive windows
    for (auto& [key, raw] : g_transformers) {
        if (auto w = g_windows[key].lock()) {
            auto& tv = w->m_transformers;
            tv.erase(
                std::remove_if(tv.begin(), tv.end(),
                    [raw](const auto& t) { return t.get() == raw; }),
                tv.end()
            );
            DBG_LOG("EXIT: removed transformer from window");
        }
    }

    // Clear all state
    g_listeners.clear();
    g_windowListeners.clear();
    g_transformers.clear();
    g_drag.clear();
    g_windows.clear();
    g_models.clear();

    logInfo("EXIT complete. Windows tracked: " + std::to_string(g_windowCount));
}
