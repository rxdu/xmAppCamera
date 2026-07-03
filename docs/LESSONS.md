# Lessons

Project-visible record of mistakes and their corrections. Consult during intake and after errors.

### Must stay C++17 (xmBase logging breaks under C++20)
- **Pattern:** Bumping the project to C++20 (to satisfy quickviz's Yoga auto-layout) broke the build: `xmBase/.../logger_vendor_spdlog.hpp: 'args#0' is not a constant expression`. spdlog 1.9 / fmt 8 enforce compile-time format-string checking under C++20, which xmBase's (formerly xmSigma) runtime-format wrapper violates.
- **Correction:** Keep the whole project on C++17. Do not raise `CMAKE_CXX_STANDARD` while depending on `xmotion::xmBase`.
- **Context:** CMake, xmBase + spdlog/fmt, C++ standard selection.

### Link quickviz `viewer`, not the `quickviz` aggregate
- **Pattern:** Linking the `quickviz` INTERFACE target pulls in the `scene` module, whose `scene_app.cpp` unconditionally calls Yoga flex methods that only exist under `ENABLE_AUTO_LAYOUT` (C++20). With auto-layout OFF (needed for C++17) the scene module fails to compile.
- **Correction:** Link only the specific quickviz targets used — `viewer` (Panel/Viewer/ImGui/glad/GL) — and set `ENABLE_AUTO_LAYOUT OFF`. `EXCLUDE_FROM_ALL` on the quickviz subdir then keeps `scene` out of the build graph.
- **Context:** CMake, quickviz submodule, module-level linking.

### CI: submodule URLs must be HTTPS; ubuntu-24.04 for GStreamer
- **Pattern:** (1) SSH submodule URLs (`git@github.com:...`) fail in GitHub Actions (no SSH key). (2) `ubuntu-22.04` runners can't install `libgstreamer1.0-dev` — its `libunwind-dev` dep conflicts with the runner's preinstalled LLVM libunwind ("held broken packages").
- **Correction:** Use HTTPS submodule URLs (public repos fetch tokenless). Run CI on `ubuntu-24.04`. Remember `libfontconfig1-dev` — quickviz `viewer` links Fontconfig.
- **Context:** GitHub Actions, git submodules, apt on hosted runners.

### Visual verification must check orientation, not just color
- **Pattern:** The GPU YUV shader shipped with an unnecessary V-flip; the verification screenshot was checked for correct colors but the upside-down orientation was missed — the user caught it.
- **Correction:** When adding a V-flip in a render path, derive it end-to-end (upload row order → FBO v-coord → ImGui uv convention) instead of guessing, and verify screenshots against a scene with obvious up/down (floor/ceiling). Here: planes upload image-top at v=0 and ImGui shows v=0 at top → identity UVs, no flip.
- **Context:** OpenGL FBO render-to-texture + ImGui::Image display path.

### avdec_h264 frame-threading silently adds ~N-frames of latency
- **Pattern:** The H.264 USB decode branch produced no output for the first ~11 frames and lagged thereafter; ffmpeg's frame-threading (default threads = cores) delays output by ~thread-count frames — 12 frames = 400ms at 30fps, fatal for live preview and invisible until measured (surfaced as per-frame decode timeouts).
- **Correction:** Set `avdec_h264 max-threads=1` for low-latency pipelines; single-threaded H.264 decode outpaces camera rates at typical resolutions. Test decoders with a synthetic AU replay (videotestsrc ! x264enc) counting decoded-vs-timeout per push.
- **Context:** GStreamer avdec/ffmpeg, live-preview latency.
