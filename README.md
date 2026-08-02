# Catalog

A GTK4 GUI for [`univ`](https://github.com/HuntedRaven7/univpkg-rs), inspired by the
[Bazaar](https://github.com/bazaar-org/bazaar). It talks to
`univ` the same way the built-in TUI does — by shelling out to the binary —
so the store stays the single source of truth.

The UI is written against **libadapta** (the theme-aware libadwaita soft fork) via
its `adw-compat.h` header, so the code uses the familiar `Adw*` names and would
also compile against plain libadwaita unchanged.

## Views

| View | What it does |
| --- | --- |
| Home | a large auto-scrolling banner of five random featured packages plus buttons for every configured repository |
| Installed | list installed packages, filter them, view details, reinstall or uninstall |
| Browse | search the deb/rpm repo indexes and install results |
| Log | live output of every `univ` operation |

The header bar has a refresh button, an upgrade-all button, and a spinner while
anything is running. Package installs/uninstalls stream their output into the
Log and pop a toast when done.

## Building

The only hard dependency is a C++17 compiler, meson/ninja, and a GTK4 +
libadapta stack. On any Ublue machine those come from Homebrew:

```sh
brew install meson ninja gtk4 glib graphene fribidi appstream json-glib sassc gettext

export PATH="/home/linuxbrew/.linuxbrew/opt/gettext/bin:$PATH"
export PKG_CONFIG_PATH="/home/linuxbrew/.linuxbrew/lib/pkgconfig:/home/linuxbrew/.linuxbrew/share/pkgconfig:/home/linuxbrew/.linuxbrew/opt/xorgproto/share/pkgconfig"

meson setup _build \
  -Dlibadapta:examples=false \
  -Dlibadapta:tests=false \
  -Dlibadapta:vapi=false \
  -Dlibadapta:introspection=disabled \
  -Dlibadapta:gtk_doc=false
ninja -C _build
```

Notes:

- **Leave `LD_LIBRARY_PATH` unset while building.** Pointing it at
  `/home/linuxbrew/.linuxbrew/lib` makes `/usr/bin/git` load brew's libcurl,
  which breaks GitHub TLS when meson clones subprojects
  (`unable to get local issuer certificate (20)`).
- The `xorgproto` directory in `PKG_CONFIG_PATH` is required — without it meson
  cannot find the X protocol headers (`xproto`, `kbproto`, `xextproto`, ...)
  that GTK4's pkg-config metadata expects, and it falls back to building
  gtk/pango/cairo from source.
- `sassc` is needed to build libadapta's stylesheet.

`libadapta` is pulled in automatically as a meson subproject
(`subprojects/libadapta.wrap`), pinned to the latest 1.5.x revision. If it is
ever installed system-wide instead, meson picks it up via pkg-config and skips
the subproject.

## Running

```sh
env LD_LIBRARY_PATH=/home/linuxbrew/.linuxbrew/lib ./_build/catalog
```

`LD_LIBRARY_PATH` is required at runtime so the binary finds brew's GTK4.
`univ` is resolved from `$UNIV_BIN` if set, otherwise from `PATH`.

## Platform

Wayland-first: the app is a native Wayland client. GTK4 selects the Wayland
backend automatically and falls back to X11 only if no Wayland compositor is
available; this has been verified to open on `GdkWaylandDisplay`.

## Project layout

| File | Responsibility |
| --- | --- |
| `src/main.cpp` | entry point, `AdwApplication` setup |
| `src/app.cpp` | window, three views, package list/details widgets, actions |
| `src/model.cpp` | `Package` struct + json-glib parsing of `univ` JSON output |
| `src/univ.cpp` | `univ` discovery, async queries, streaming install/uninstall tasks |
| `meson.build` | build glue, links libadapta subproject + json-glib |

## Roadmap

- package icons (deb control / rpm header metadata or generic by-kind icons)
- per-repository view and repo management
- dependencies graph / reverse-dependencies in the detail pane
- profile management (`univ profile ...`) from the GUI
- transaction rollback status surfacing from the Log
