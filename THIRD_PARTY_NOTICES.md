# Third-party notices

This inventory covers third-party code and assets present in, or fetched by, the engine and Hub source tree. Engine dependency revisions are pinned in `CMakeLists.txt`; Hub dependency resolutions are recorded in `hub-tauri/package-lock.json` and `hub-tauri/src-tauri/Cargo.lock`.

## Bundled in this repository

| Component | Version / form | Used from | License | Notice |
|---|---|---|---|---|
| GLAD-compatible OpenGL loader | Minimal OpenGL 3.3 loader source | `extern/glad/` | glad source: MIT; Khronos specification portions: Apache-2.0 / Khronos material notice | `third_party/licenses/GLAD.txt` |
| stb_image | 2.30 | `extern/stb/stb_image.h` | MIT or Public Domain | License block at end of the header |
| stb_image_write | 1.16 | `extern/stb/stb_image_write.h` | MIT or Public Domain | License block at end of the header |
| stb_easy_font | 1.1 | `src/ui/stb_easy_font.h` | MIT or Public Domain | License block at end of the header |
| tinyobjloader | 2.x single header | `extern/tinyobjloader/tiny_obj_loader.h` | MIT | License block at start of the header |
| fast_float portions in tinyobjloader | Embedded implementation | `extern/tinyobjloader/tiny_obj_loader.h` | MIT, Apache-2.0, or Boost-1.0 | Complete alternative-license notices embedded in the header |
| Inter | Regular, Medium, SemiBold font files | `resources/fonts/` | SIL Open Font License 1.1 | `resources/fonts/LICENSE.txt` |

## Downloaded by CMake at configure time

These projects are not copied into this Git repository. CMake FetchContent downloads the pinned revisions into the local build tree.

| Component | Pinned revision | License | License copy |
|---|---|---|---|
| GLFW 3.4 | `7b6aead9fb88b3623e3b3725ebb42670cbe4c579` | zlib/libpng-style | `third_party/licenses/GLFW.txt` |
| GLM 1.0.1 | `0af55ccecd98d4e5a8d1fad7de25ba429d60e863` | MIT alternative selected | `third_party/licenses/GLM.txt` |
| spdlog 1.15.3 | `6fa36017cfd5731d617e1a934f0e5ea9c4445b13` | MIT | `third_party/licenses/SPDLOG.txt` |
| fmt bundled by spdlog | Included in the pinned spdlog source | MIT | `third_party/licenses/FMT.txt` |
| nlohmann/json 3.11.3 | `9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03` | MIT | `third_party/licenses/NLOHMANN_JSON.txt` |
| Dear ImGui docking revision | `5098ce161db7f3919630b2b575347fc3b2ce5c62` | MIT | `third_party/licenses/DEAR_IMGUI.txt` |
| ImGuizmo | `a712ea83e937cc6f11e22c3b2c82920857ae13df` | MIT | `third_party/licenses/IMGUizmo.txt` |
| ufbx 0.14.1 | `2aef1971276442f9f6ce14c42b37deb8da9f4d3d` | MIT or Public Domain | `third_party/licenses/UFBX.txt` |
| Jolt Physics 5.2.0 | `a63aa3b8e24cf95f3fab2613f9a3015b164ef62c` | MIT | `third_party/licenses/JOLT_PHYSICS.txt` |

## Hub dependencies downloaded by npm

No `node_modules` content is stored in this repository. The checked lockfile currently resolves 135 packages with declared license metadata: 111 MIT, 13 Apache-2.0 OR MIT, 2 MIT OR Apache-2.0, 2 Apache-2.0, 5 ISC, 1 BSD-3-Clause, and 1 CC-BY-4.0 package (`caniuse-lite`). Direct runtime and build dependencies include Tauri API and plugins (MIT or Apache-2.0), React and React DOM (MIT), Vite and its React plugin (MIT), TypeScript (Apache-2.0), and the Tauri CLI (MIT or Apache-2.0).

The exact package names, versions, integrity hashes, and declared licenses are recorded in `hub-tauri/package-lock.json`. `caniuse-lite` supplies browser compatibility data under CC-BY-4.0 and is a build-time Vite dependency; its source is not vendored here.

## Hub dependencies downloaded by Cargo

No Cargo registry source or compiled crate is stored in this repository. `cargo metadata --locked` reports 499 packages across all resolved platform targets; the direct dependencies are Tauri and tauri-plugin-dialog (MIT or Apache-2.0), serde and serde_json (MIT or Apache-2.0), dirs (MIT or Apache-2.0), reqwest (MIT or Apache-2.0), zip (MIT), walkdir (Unlicense or MIT), windows-sys (MIT or Apache-2.0), and tauri-build (MIT or Apache-2.0).

Transitive metadata also includes permissive BSD, ISC, Zlib, Boost, CC0, Unicode-3.0, CDLA-Permissive-2.0, bzip2, and MPL-2.0 components. The MPL-2.0 entries are unmodified registry dependencies (`cssparser`, `cssparser-macros`, `dtoa-short`, `option-ext`, and `selectors`); they are not vendored into this source tree. Two `r-efi` versions offer MIT or Apache-2.0 alternatives in addition to LGPL-2.1-or-later, so this project may use the permissive alternative. Exact resolved versions and checksums are recorded in `hub-tauri/src-tauri/Cargo.lock`.

## External tools and runtimes not distributed here

- PSn00bSDK is an external PS1 toolchain. It is not vendored, downloaded, or redistributed by this repository.
- Ninja is expected on `PATH` for PS1 builds and is not bundled.
- PCSX-Redux, DuckStation, and other emulators are not bundled.
- OpenBIOS, retail BIOS images, Sony SDK files, PlayStation disc license data, and prebuilt PS1 executables/disc images are not bundled.
- Windows SDK and system libraries are supplied by the user's development environment and are not redistributed.

The original Mipsync GTE compatibility source and its provenance statement are located in `templates/ps1/starter/runtime/`. That implementation does not include Sony SDK, PSn00bSDK `libpsxgte`, or other third-party GTE source.
