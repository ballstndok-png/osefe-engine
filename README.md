# Osefe Engine

<p align="center">
  <a href="#">
    <img src="editor/icons/osefe_logo.svg" width="400" alt="Osefe Engine">
  </a>
</p>

## A modern 2D and 3D cross-platform game engine

**Osefe Engine is a modern, independent game engine built on the proven
Godot 4.7.2 core.** It ships with a refreshed identity, a new modern editor
theme ("Osefe (Modern)" — dark violet accents), a redesigned splash screen and
brand icon set, plus carefully tuned 3D rendering defaults for better
out-of-the-box visuals.

## What's different from upstream

- New engine identity: **Osefe Engine** (`version.py`, window titles, file properties)
- New vector logo, editor icons and Windows `.ico`
- New boot splash (800×600, gradient "O" mark)
- New default color preset **"Osefe (Modern)"** (deep navy-violet `#0B0C16` + electric violet accent `#8C66FF`, crisper accent borders, richer contrast)
- Desktop rendering defaults raised one quality step: soft shadow filter
  (Medium) and anisotropic filtering (8×) — mobile overrides untouched
- **Beginner-friendly defaults**: editor UI starts in **Arabic**, auto-save on focus loss, snappier code completion (0.15s)
- **Osefe AI Boost**: stronger out-of-the-box game AI defaults
  - 3D navigation mesh precision doubled: default cell size/height `0.125` (was `0.25`) — smoother agent paths that hug walls less
  - 2D navigation polygon precision doubled: default cell size `0.5` px (was `1.0`)
  - Pathfinding limits raised: `path_search_max_polygons` `16384` (was `4096`) for huge levels
  - Smoother agent following: `path_desired_distance` halved for NavigationAgent2D/3D
  - Dense-crowd avoidance: default avoidance `max_neighbors` `20` (was `10`)
  - All values remain fully configurable per-project via ProjectSettings — no breaking changes
- **Beginner-friendly defaults**: editor UI starts in **Arabic**, auto-save on focus loss, snappier code completion (0.15s)
- Full project/API compatibility with Godot 4.7 projects

## Building from source

```bash
scons platform=windows target=editor arch=x86_64 werror=no d3d12=no winrt=no accesskit=no angle=no -j8
```

Supported platforms: Windows, Linux, macOS, Android, iOS, Web (same platform
backends as the upstream core).

## Acknowledgments

Osefe Engine is built on the open-source **Godot Engine 4.7.2** core
(https://github.com/godotengine/godot), © Juan Linietsky, Ariel Manzur and
Godot Engine contributors, released under the MIT license. All upstream
copyright and license notices are preserved throughout the source tree.

## 🇵🇸 Free Palestine

Osefe Engine stands with Palestine and its people.

## License

MIT — see [LICENSE.txt](LICENSE.txt).
