# hyprland-wobbly

Compiz-style wobbly windows for [Hyprland](https://hypr.land), ported from
[hermes83/compiz-windows-effect](https://github.com/hermes83/compiz-windows-effect)
(itself derived from the original 2005 Compiz wobbly plugin by David Reveman
and Scott Moreau, spring model by Kristian Hogsberg).

> **Status: WIP — physics works, rendering integration not finished.**
> The spring-mass simulation runs per-window and updates each frame, but the
> deformation is not yet applied to the GL texture pass. PRs welcome.

## What's done

- Faithful C++ port of the Compiz `WobblyModel` (4×4 grid, springs, friction,
  mass, intensity, maximize/unmaximize impulses).
- Plugin entry that subscribes to Hyprland's event bus:
  - `window.open` — creates a model sized to the window
  - `window.close` / `window.destroy` — frees the model
  - `tick` — steps the physics each frame and schedules a redraw when any
    model is still moving
- `hyprpm.toml` and `Makefile` set up for `hyprpm add` builds against
  Hyprland 0.55.

## What's missing

- **Mesh deformation in the render path.** The plugin currently simulates
  wobble offsets but does not feed them into `CHyprRenderer::renderWindow` /
  `CHyprOpenGLImpl`. A correct implementation needs to either:
  1. Trampoline-hook `renderWindow`, sample the model's 4×4 grid, tessellate
     the window texture, and submit a vertex array with a custom shader; or
  2. Add a per-window post-process pass that warps the rendered FBO using
     the grid as control points.
- Grab/move/resize hookups (the model has `grab`/`move`/`maximize`
  functions; they're not yet wired to Hyprland's window-manipulation events).
- Config values (`plugin:wobbly:friction`, etc.) — currently hardcoded to
  the upstream defaults.

## Building

```bash
# Via hyprpm (recommended once the repo is public)
hyprpm add https://github.com/<you>/hyprland-wobbly
hyprpm enable hyprland-wobbly

# Or manually
make
```

The Makefile uses `pkg-config --cflags hyprland`, so you need the
`hyprland` development headers installed (Arch: included in the `hyprland`
package).

## Configuration

Once config plumbing lands the planned knobs will mirror upstream:

| Key                                | Default | Notes                          |
| ---------------------------------- | ------- | ------------------------------ |
| `plugin:wobbly:friction`           | `3.0`   | 1..10, higher = more damping   |
| `plugin:wobbly:spring_k`           | `8.0`   | 1..10, stiffness               |
| `plugin:wobbly:mass`               | `50`    | 1..99, higher = heavier feel   |

## License

MIT for the Hyprland integration code. The `WobblyModel` derives from
Compiz code originally published by Novell under a permissive license;
see `LICENSE` and the file headers for the full notices.
