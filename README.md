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
- New default color preset **"Osefe (Modern)"** (deep dark `#0E0F17` + violet `#7B5BFF`)
- Desktop rendering defaults raised one quality step: soft shadow filter
  (Medium) and anisotropic filtering (8×) — mobile overrides untouched
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

## License

MIT — see [LICENSE.txt](LICENSE.txt).
