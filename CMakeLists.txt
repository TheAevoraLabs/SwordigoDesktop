cmake_minimum_required(VERSION 3.16)
project(SwordigoDesktop VERSION 4.5.0 LANGUAGES C CXX ASM)

# Build directly into the source tree's distribution folder — no separate
# `cmake --install` step needed. Executables land in <dist>/, shared libs in
# <dist>/libs/.
#
# The dist folder is `bin` for native (Linux) builds and `binw` for Windows
# builds (incl. MinGW cross-compiles) so Windows .exe/.dll artifacts never land
# in — or clobber — the Linux `bin/` tree. Override with -DSWORDIGO_DIST_DIR=...
#
# Test executables are NOT emitted here — they go to <dist>/tests/ and only in
# CMAKE MODE DEV. See the banner below.
if (NOT DEFINED SWORDIGO_DIST_DIR)
    if (WIN32)
        set(SWORDIGO_DIST_DIR "binw")
    else()
        set(SWORDIGO_DIST_DIR "bin")
    endif()
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/${SWORDIGO_DIST_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/${SWORDIGO_DIST_DIR}/libs")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/${SWORDIGO_DIST_DIR}/libs")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Modular build. Each component lives in cmake/components/*.cmake and defines
# its own targets; shared platform/option/helper logic is in cmake/*.cmake.
# ---------------------------------------------------------------------------

include(cmake/SwordigoOptions.cmake)     # options + cache paths

# ===========================================================================
#  CMAKE MODE DEV  —  ONE switch decides whether tests exist at all.          READ THIS.
# ===========================================================================
#
#  Tests are a DEVELOPER-ONLY artifact. A normal build (plain `cmake`, or
#  ./run_swordigo.sh / ./run_openswordigo.sh, both of which run an unqualified
#  `cmake --build <dir>` with no target list) used to compile + link all ~30
#  test executables into bin/ and leave them there next to the real app
#  binaries. That polluted bin/ and made every ordinary build pay for code
#  nobody was running.
#
#  From now on:
#
#    * DEFAULT (SWORDIGO_DEV_MODE=OFF)  ->  BUILD_TESTING is forced OFF, no
#      test target is declared, nothing extra is compiled, and <dist>/tests/
#      stays empty. This is what a normal build / release / packaging run gets.
#
#    * CMAKE MODE DEV (-DSWORDIGO_DEV_MODE=ON)  ->  BUILD_TESTING is forced ON,
#      every test target is declared, and all test executables are written to
#        <dist>/tests/          (Linux/macOS: bin/tests/, Windows: binw/tests/)
#      — i.e. in the dist tree but one level below the app, so `ls bin/` shows
#      applications only. Test binaries also get $ORIGIN/../libs as their
#      BUILD_RPATH, which keeps them runnable straight out of tests/.
#
#    * Configure dev mode + run the suite:
#        cmake -S . -B build-gg -DSWORDIGO_DEV_MODE=ON
#        cmake --build build-gg -j"$(nproc)"
#        ctest --test-dir build-gg --output-on-failure
#
#  NOTE ON CACHES: BUILD_TESTING is rewritten (FORCE) to mirror SWORDIGO_DEV_MODE
#  on every configure. That is deliberate — pre-existing build dirs still carry
#  a stale BUILD_TESTING=ON from the old `include(CTest)` default, and without
#  the force those caches would keep rebuilding the whole test suite after this
#  change. Do not pass -DBUILD_TESTING by hand; the dev-mode switch owns it.
# ===========================================================================

set(BUILD_TESTING "${SWORDIGO_DEV_MODE}" CACHE BOOL
    "Build the developer test suite into <dist>/tests/ (mirrors SWORDIGO_DEV_MODE — use -DSWORDIGO_DEV_MODE=ON instead)" FORCE)
include(CTest)                           # enable_testing() + add_test()/`test` target

include(cmake/SwordigoPlatform.cmake)    # includes, defines, platform flags
include(cmake/SwordigoDeps.cmake)        # external dependencies
include(cmake/SwordigoHelpers.cmake)     # swordigo_target()/swordigo_library()

# Component order matters: libraries before the executables that consume them.
include(cmake/components/swcore.cmake)
include(cmake/components/swgui.cmake)
include(cmake/components/swfmt_swpod.cmake)
include(cmake/components/filerift.cmake)
include(cmake/components/caver.cmake)      # Ruby's own recovered Caver runtime
include(cmake/components/opensw.cmake)     # Boot the recovered runtime into a level
include(cmake/components/swgfx.cmake)
include(cmake/components/swemu.cmake)
include(cmake/components/swordfare.cmake)
include(cmake/components/swordfare_boot.cmake)
include(cmake/components/ruby.cmake)
include(cmake/components/ruby_cli.cmake)
include(cmake/components/ruby_gg.cmake)
include(cmake/components/build_studio.cmake)

# Guest + third-party build steps and install rules.
include(cmake/components/sre.cmake)
include(cmake/components/sre13.cmake)
include(cmake/components/misc.cmake)

if (BUILD_TESTING)
    # -----------------------------------------------------------------------
    # Test binaries go to <dist>/tests/ — NEVER <dist>/ itself.
    # -----------------------------------------------------------------------
    # CMake captures CMAKE_RUNTIME_OUTPUT_DIRECTORY when each add_executable()
    # runs, so overriding it here (and putting it back at the end of this
    # block) rebinds every test target below into <dist>/tests/ while leaving
    # the applications in <dist>/. Same scoping trick as the libs/ layout.
    #
    # Only active in CMAKE MODE DEV (see the banner near the top): when
    # SWORDIGO_DEV_MODE is OFF this whole block is skipped, so a normal build
    # never declares — let alone compiles — a test target, and <dist>/tests/
    # is left to whatever is already in it.
    #
    # NOTE: cmake auto-adds absolute build RPATHs for linked project libs, so
    # these stay runnable from any cwd; SWORDIGO_ORIGIN_LIBS_DIR additionally
    # points $ORIGIN at ../libs for the targets built via swordigo_target().
    set(_swordigo_app_runtime_out "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/${SWORDIGO_DIST_DIR}/tests")
    set(SWORDIGO_ORIGIN_LIBS_DIR "$ORIGIN/../libs")

    add_executable(trampoline_mgr_test
        tests/trampoline_mgr_test.cpp
        ${SRC_DIR}/platform/arm64_reloc.cpp)
    target_include_directories(trampoline_mgr_test PRIVATE ${SRC_DIR})
    add_test(NAME trampoline_mgr_test COMMAND trampoline_mgr_test)

    add_executable(sre_hud_coin_regression_test
        tests/sre_hud_coin_regression_test.cpp)
    target_compile_definitions(sre_hud_coin_regression_test PRIVATE
        SRE_SOURCE_DIR="${SRC_DIR}/sre/sre12")
    add_test(NAME sre_hud_coin_regression_test COMMAND sre_hud_coin_regression_test)

    add_executable(filerift_smoke tests/filerift_smoke.cpp)
    target_include_directories(filerift_smoke PRIVATE ${SRC_DIR} ${SRC_DIR}/sre/base/lua/src)
    target_link_libraries(filerift_smoke PRIVATE filerift swpod OpenGL::GL)
    add_test(NAME filerift_smoke COMMAND filerift_smoke)

    # Smart-save / corruption regression: scene round trip must be structure-
    # identical (in-place apply, no reload) and encode errors must throw before
    # the target file is touched. Data file is optional (SKIP when absent).
    add_executable(scene_smart_save_test tests/scene_smart_save_test.cpp)
    target_include_directories(scene_smart_save_test PRIVATE ${SRC_DIR})
    target_compile_definitions(scene_smart_save_test PRIVATE
        SWORDIGO_SRC_DIR="${SRC_DIR}")
    target_link_libraries(scene_smart_save_test PRIVATE filerift swpod OpenGL::GL)
    add_test(NAME scene_smart_save_test COMMAND scene_smart_save_test)

    # TEMPORARY perf probe (timing on real scenes — remove after diagnosis).
    # TEMPORARY perf driver: real RubyMainWindow + the user's real scene, offscreen.
    set(SCENE_PERF_DRIVER_SOURCES ${RUBY_GG_SOURCES})
    list(REMOVE_ITEM SCENE_PERF_DRIVER_SOURCES ${SRC_DIR}/ruby/main.cpp)
    add_executable(scene_perf_driver
        tests/scene_perf_driver.cpp
        ${SCENE_PERF_DRIVER_SOURCES})
    set_target_properties(scene_perf_driver PROPERTIES
        AUTOMOC ON AUTORCC ON AUTOUIC ON)
    target_compile_definitions(scene_perf_driver PRIVATE
        SWORDIGO_NO_IMGUI
        BATCH_CONVERTER_NO_UI
        SWORDIGO_SRC_DIR="${SRC_DIR}")
    target_include_directories(scene_perf_driver PRIVATE
        ${SRC_DIR}
        ${SRC_DIR}/ruby
        ${SRC_DIR}/ruby/git/include
        ${SRC_DIR}/tools
        ${SRC_DIR}/tools/ufbx
        ${SRC_DIR}/stb
        ${SRC_DIR}/platform
        ${SRC_DIR}/sre/base/lua/src
        ${MPG123_INCLUDE_DIRS}
        ${VORBISFILE_INCLUDE_DIRS})
    target_link_libraries(scene_perf_driver PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets Qt6::Network
        OpenGL::GL ZLIB::ZLIB swcore swfmt swpod filerift caver git2_static
        SDL3::SDL3 ${SDL3_IMAGE_LINK}
        ${MPG123_LIBRARIES} ${VORBISFILE_LIBRARIES}
        ${SWORDIGO_LIBM} ${SWORDIGO_SOCKLIB})
    if (NOT WIN32)
        target_link_libraries(scene_perf_driver PRIVATE dl pthread util)
    endif()
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(scene_perf_driver PRIVATE rt)
    endif()

    add_executable(scene_perf_probe tests/scene_perf_probe.cpp
        ${SRC_DIR}/ruby/viewport/ruby_picking.cpp)
    target_include_directories(scene_perf_probe PRIVATE ${SRC_DIR})
    target_compile_definitions(scene_perf_probe PRIVATE
        SWORDIGO_NO_IMGUI
        BATCH_CONVERTER_NO_UI
        SWORDIGO_SRC_DIR="${SRC_DIR}")
    target_link_libraries(scene_perf_probe PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets Qt6::Network
        OpenGL::GL ZLIB::ZLIB swcore swfmt swpod filerift git2_static
        SDL3::SDL3 ${SDL3_IMAGE_LINK}
        ${MPG123_LIBRARIES} ${VORBISFILE_LIBRARIES}
        ${SWORDIGO_LIBM} ${SWORDIGO_SOCKLIB})
    if (NOT WIN32)
        target_link_libraries(scene_perf_probe PRIVATE dl pthread util)
    endif()
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(scene_perf_probe PRIVATE rt)
    endif()
    add_test(NAME scene_perf_probe COMMAND scene_perf_probe)

    # Scene-text corruption regression: a .scene that became FileRift text
    # (single or double banner) must re-encode to a binary that loads back to
    # the same structure via av::scene_load_bytes — the viewport's in-memory
    # recovery path. Data file is optional (SKIP when absent).
    add_executable(scene_text_recovery_test tests/scene_text_recovery_test.cpp)
    target_include_directories(scene_text_recovery_test PRIVATE ${SRC_DIR})
    target_compile_definitions(scene_text_recovery_test PRIVATE
        SWORDIGO_SRC_DIR="${SRC_DIR}")
    target_link_libraries(scene_text_recovery_test PRIVATE filerift swpod OpenGL::GL)
    add_test(NAME scene_text_recovery_test COMMAND scene_text_recovery_test)

    # Direct scene boot: the libc++ std::string contract the Ruby Touch runner relies on
    # when it swaps CaverShell's boot-scene field in CaverShell::InitView.
    #
    # Built twice, once per engine ABI layout, because libc++'s inline short-string
    # capacity is sizeof(__long) - 1 — 22 chars on arm64-v8a but only 10 on armeabi-v7a.
    # A single hardcoded threshold corrupts the engine's own string on 32-bit builds, so
    # the ILP32 target is a real regression guard, not a duplicate of the LP64 one.
    foreach(_boot_layout IN ITEMS lp64 ilp32)
        set(_boot_target scene_boot_string_${_boot_layout}_test)
        set(_boot_defs STRING_SHORT_CAP=23)
        if(_boot_layout STREQUAL "ilp32")
            set(_boot_defs STRING_SHORT_CAP=11 TEST_ILP32_LAYOUT=1)
        endif()
        add_executable(${_boot_target}
            tests/scene_boot_string_test.cpp
            ${SRC_DIR}/ruby/android/runner/stdstring.c
        )
        target_include_directories(${_boot_target} PRIVATE
            ${SRC_DIR}
            ${SRC_DIR}/ruby/android/runner
        )
        target_compile_definitions(${_boot_target} PRIVATE ${_boot_defs})
        add_test(NAME ${_boot_target} COMMAND ${_boot_target})
    endforeach()

    # Scale-gizmo payload propagation (web editor `scaleObjectData` parity):
    # scaling an object must also scale LocalAABB + ShapeComponent geometry
    # (rect/circle/polygon) + CollisionShapeComponent depth extents, with the
    # model-object dominant-axis-uniform rule. Pure RAM unit test, no GL/Qt.
    add_executable(scene_scale_payload_test tests/scene_scale_payload_test.cpp)
    target_include_directories(scene_scale_payload_test PRIVATE ${SRC_DIR})
    target_link_libraries(scene_scale_payload_test PRIVATE swpod swcore)
    add_test(NAME scene_scale_payload_test COMMAND scene_scale_payload_test)

    # Memory Research catalog schema contract.  The embedded recovery DB keys
    # its tables by TEXT ids with snake_case columns while RecoveryCatalog
    # exposes dense int ids; a mismatch makes every getter return an empty
    # vector silently (blank Catalog tab).  Pins the struct/field/relationship/
    # vtable/proto mapping, the GameSceneController offsets the Live Inspector
    # reads, and the shipped row counts.  Pure RAM test, no Qt/GL.
    if (SWORDIGO_HAVE_SQLITE3)
        add_executable(recovery_catalog_test tests/recovery_catalog_test.cpp
            ${SRC_DIR}/game/research/recovery_catalog.cpp
            ${SRC_DIR}/game/research/embedded_recovery_db.cpp)
        target_include_directories(recovery_catalog_test PRIVATE
            ${SRC_DIR} ${SWORDIGO_SQLITE3_INCLUDE_DIRS})
        target_link_libraries(recovery_catalog_test PRIVATE
            ${SWORDIGO_SQLITE3_LIBRARIES} Threads::Threads)
        add_test(NAME recovery_catalog_test COMMAND recovery_catalog_test)
    endif()

    # Object classification address-space contract.  A live object's vptr is a
    # relocated VA (libswordigo is mapped at 0x1000000) while every known-vtable
    # key is a module-relative RVA; classify() used to compare them directly, so
    # nothing could ever be classified.  Also covers the RTTI strategy, which
    # classifies an object from its type_info name with no table entry at all.
    # Pure RAM test (synthetic guest memory), no Qt/GL.
    if (SWORDIGO_HAVE_SQLITE3)
        add_executable(vtable_classifier_test tests/vtable_classifier_test.cpp
            ${SRC_DIR}/game/research/vtable_classifier.cpp
            ${SRC_DIR}/game/research/recovery_catalog.cpp
            ${SRC_DIR}/game/research/embedded_recovery_db.cpp)
        target_include_directories(vtable_classifier_test PRIVATE
            ${SRC_DIR} ${SWORDIGO_SQLITE3_INCLUDE_DIRS})
        target_link_libraries(vtable_classifier_test PRIVATE
            ${SWORDIGO_SQLITE3_LIBRARIES} Threads::Threads)
        add_test(NAME vtable_classifier_test COMMAND vtable_classifier_test)
    endif()

    # Deterministic memory identity for the live inspector.  Identity must never
    # depend on a runtime address, so it is derived only from static facts
    # (build + module + kind + container + offset + type): two separate boots
    # must produce the identical base name, while the live *instances* of one
    # static site stay distinguishable.  Covers the VAR naming rules, stack-local
    # naming, provenance-anchored vs session tags, multi-instance handling and
    # the persistence store.  Pure RAM test, no Qt/GL/SQLite.
    add_executable(mem_identity_test tests/mem_identity_test.cpp
        ${SRC_DIR}/game/research/mem_identity.cpp)
    target_include_directories(mem_identity_test PRIVATE ${SRC_DIR})
    target_link_libraries(mem_identity_test PRIVATE Threads::Threads)
    add_test(NAME mem_identity_test COMMAND mem_identity_test)

    # The loaded image's dynamic symbol table, used as static-site evidence.
    # Pins the mangled-name transform against real symbols copied out of the
    # shipped libswordigo.so 1.4.13 .dynsym, the floor/ceil lookup semantics
    # (including that an address *between* symbols is never claimed as belonging
    # to one), the raw Elf64/Elf32 array build, and — most importantly — that
    # symbol evidence cannot move any identity.  Pure RAM test.
    add_executable(elf_symbols_test tests/elf_symbols_test.cpp
        ${SRC_DIR}/game/research/elf_symbols.cpp
        ${SRC_DIR}/game/research/mem_identity.cpp)
    target_include_directories(elf_symbols_test PRIVATE ${SRC_DIR})
    target_link_libraries(elf_symbols_test PRIVATE Threads::Threads)
    add_test(NAME elf_symbols_test COMMAND elf_symbols_test)

    # Sections — the coarse half of "where is this address?".  Needed because a
    # symbol table has a hole below its lowest symbol, and that hole is exactly
    # where a module-scoped scan floods: measured on the shipped image, 8 of 12
    # hits for a common 4-byte value were inside .dynsym and .rela.plt.  Pins the
    # classification, the containment rules (a zero-size section matches nothing),
    # the metadata ranges a scan can skip, and the nameless-image fallback.
    add_executable(elf_sections_test tests/elf_sections_test.cpp
        ${SRC_DIR}/game/research/elf_sections.cpp)
    target_include_directories(elf_sections_test PRIVATE ${SRC_DIR})
    target_link_libraries(elf_sections_test PRIVATE Threads::Threads)
    add_test(NAME elf_sections_test COMMAND elf_sections_test)

    # The scan engine (Cheat Engine / GameGuardian class) and the identity-routed
    # address list.  Pins the whole scan matrix (exact/changed/increased-by/
    # between/AoB/string/unknown-initial/max-results/alignment), the bounds and
    # field-spill write rules, freeze-from-tick, rename-preserves-identity, and
    # the categorisation filters.  Runs over a synthetic flat guest buffer.
    add_executable(mem_scanner_test tests/mem_scanner_test.cpp
        ${SRC_DIR}/game/research/mem_scanner.cpp
        ${SRC_DIR}/game/research/mem_address_list.cpp
        ${SRC_DIR}/game/research/mem_identity.cpp)
    target_include_directories(mem_scanner_test PRIVATE ${SRC_DIR})
    target_link_libraries(mem_scanner_test PRIVATE Threads::Threads)
    add_test(NAME mem_scanner_test COMMAND mem_scanner_test)

    # The Live Object Map (static->runtime->object->struct, with pointer
    # re-resolution), the AArch64 access decoder + access trace (instruction
    # evidence, never proof), and the .swordfare workspace (research survives a
    # crash because it is keyed by identity, not by address).  Runs over a
    # synthetic guest plus a temp directory.
    add_executable(live_memory_engine_test tests/live_memory_engine_test.cpp
        ${SRC_DIR}/game/research/live_object_map.cpp
        ${SRC_DIR}/game/research/mem_access_trace.cpp
        ${SRC_DIR}/game/research/research_workspace.cpp
        ${SRC_DIR}/game/research/mem_identity.cpp
        ${SRC_DIR}/game/research/mem_scanner.cpp
        ${SRC_DIR}/game/research/mem_address_list.cpp)
    target_include_directories(live_memory_engine_test PRIVATE ${SRC_DIR})
    target_link_libraries(live_memory_engine_test PRIVATE Threads::Threads)
    add_test(NAME live_memory_engine_test COMMAND live_memory_engine_test)

    # The unclaimed memory explorer: ranks the memory NOT covered by any known
    # field by how it moves relative to scene changes and object rebuilds.  Pins
    # the properties that make it trustworthy — claimed ranges are skipped, a
    # pass is bounded so it can never block a frame, static memory is never
    # tracked, scene-correlated movers outrank mere churn, adjacent movers are
    # reported as one probable struct, and a lead is never presented as proven.
    add_executable(unclaimed_explorer_test tests/unclaimed_explorer_test.cpp
        ${SRC_DIR}/game/research/unclaimed_explorer.cpp
        ${SRC_DIR}/game/research/live_object_map.cpp
        ${SRC_DIR}/game/research/mem_identity.cpp
        ${SRC_DIR}/game/research/mem_scanner.cpp)
    target_include_directories(unclaimed_explorer_test PRIVATE ${SRC_DIR})
    target_link_libraries(unclaimed_explorer_test PRIVATE Threads::Threads)
    add_test(NAME unclaimed_explorer_test COMMAND unclaimed_explorer_test)

    # Schema-driven component field editors (master TODO 2.1): the inspector's
    # field-editor path (scene_component_fields + scene_set_component_field)
    # must round-trip every wire type — VARINT, I32 float, printable LEN,
    # nested LEN — plus unknown binary fields from newer game versions, and
    # the Program (Lua) helpers must both store the source AND regenerate the
    # compiled bytecode the engine actually runs. RAM unit test, no Qt; links
    # filerift because the Lua-aware Program writer lives with the host Lua.
    add_executable(component_field_editors_test tests/component_field_editors_test.cpp)
    target_include_directories(component_field_editors_test PRIVATE ${SRC_DIR})
    target_link_libraries(component_field_editors_test PRIVATE filerift swpod swcore)
    add_test(NAME component_field_editors_test COMMAND component_field_editors_test)

    # Caver Core C1 — the ObjectLibrary registry (library_manager.h): load by
    # name through the search path, identity from ObjectLibrary.Name, template
    # lookup with owner reporting, the content-hash fast path, on-disk hot swap
    # (added/removed templates), in-memory swap, imports and missing diagnostics.
    # Pure RAM/FS unit test, no Qt/GL; the corpus check skips when assets are absent.
    add_executable(library_manager_test tests/library_manager_test.cpp)
    target_include_directories(library_manager_test PRIVATE ${SRC_DIR})
    target_link_libraries(library_manager_test PRIVATE caver swpod swcore Threads::Threads)
    add_test(NAME library_manager_test COMMAND library_manager_test)

    # Component decoding against real shipping .scl data: every payload slot of a
    # component is decoded (a CollisionShape carries ShapeComponent *and*
    # CollisionShapeComponent, a MonsterEntity carries the base Entity slot too).
    # Skips when the extracted assets are not on the machine.
    add_executable(caver_runtime_test tests/caver_runtime_test.cpp)
    target_include_directories(caver_runtime_test PRIVATE ${SRC_DIR})
    target_link_libraries(caver_runtime_test PRIVATE caver swpod swcore Threads::Threads)
    add_test(NAME caver_runtime_test COMMAND caver_runtime_test)

    # DoorController / ElevatorController: the transition and the velocity-driven
    # state machine, transcribed from their own functions. Guards against the
    # invented per-second constants the port used to move these two with.
    add_executable(caver_level_mechanics_test tests/caver_level_mechanics_test.cpp)
    target_include_directories(caver_level_mechanics_test PRIVATE ${SRC_DIR})
    target_link_libraries(caver_level_mechanics_test PRIVATE caver)
    add_test(NAME caver_level_mechanics_test COMMAND caver_level_mechanics_test)

    # ProgramState (the script scheduler: gate, dt scaling, wait countdown,
    # child recursion, abort sweep) and the recovered Scene::Update frame order.
    # Guards the two guesses the port made — a wait read off the yielded stack,
    # and components ticking before scripts.
    add_executable(caver_program_state_test tests/caver_program_state_test.cpp)
    target_include_directories(caver_program_state_test PRIVATE ${SRC_DIR})
    target_link_libraries(caver_program_state_test PRIVATE caver)
    add_test(NAME caver_program_state_test COMMAND caver_program_state_test)

    # The animation node tree and bone matching: the clip sampler, the crossfade
    # that mutates its left child's buffer, the FloatDataTrack stride records, the
    # quaternion hemisphere flip, and the skeleton's local/world/skin chain.
    add_executable(caver_anim_bones_test tests/caver_anim_bones_test.cpp)
    target_include_directories(caver_anim_bones_test PRIVATE ${SRC_DIR})
    target_link_libraries(caver_anim_bones_test PRIVATE caver)
    add_test(NAME caver_anim_bones_test COMMAND caver_anim_bones_test)

    # The character animation *controller*: the six outlets, the three-node blend
    # tree, the exact flag words each transition writes, IsMoving as a flag rather
    # than a speed, the phase entry points, the action/casting slot and the base
    # class's BlendToAnimationNode that the derived class replaces wholesale.
    add_executable(caver_char_anim_test tests/caver_char_anim_test.cpp)
    target_include_directories(caver_char_anim_test PRIVATE ${SRC_DIR})
    target_link_libraries(caver_char_anim_test PRIVATE caver)
    add_test(NAME caver_char_anim_test COMMAND caver_char_anim_test)

    # The character animation *driver*: CharControllerComponent::Update — the
    # air-timer thresholds that cut the run cycle and expire the jump grace, the
    # falling-clip gates, the swing-tail test that picks which jump clip a jump
    # uses, and the spawn path where `moving` is a separate `if` and not part of
    # the if/else-if chain.
    add_executable(caver_char_controller_test tests/caver_char_controller_test.cpp)
    target_include_directories(caver_char_controller_test PRIVATE ${SRC_DIR})
    target_link_libraries(caver_char_controller_test PRIVATE caver)
    add_test(NAME caver_char_controller_test COMMAND caver_char_controller_test)

    # The ground-mesh harness: boulderx regenerated onto a real shipped object and
    # diffed against the bake. With no argument it runs built-in fixtures; with a
    # path it reads the TSV that tools/ground_mesh_truth.py writes from a decoded
    # scene, so a corpus object can be diffed without a rebuild.
    add_executable(ground_mesh_compare_test
        tests/ground_mesh_compare_test.cpp
        ${SRC_DIR}/tools/boulderx.cpp)
    target_include_directories(ground_mesh_compare_test PRIVATE ${SRC_DIR})
    add_test(NAME ground_mesh_compare_test COMMAND ground_mesh_compare_test)

    # The WHOLE-mesh differential: boulderx's triangle SETS against a shipped
    # bake, family by family, in outline-node space. ground_mesh_compare_test
    # checks statistics (face counts, Z envelope, per-node depth) and every one
    # of those passes on thecave_part15 obj9#10 while only 3 of the bake's 57 cap
    # triangles are reproduced — statistics cannot see a different surface.
    # Input is `tools/ground_mesh_truth.py dump`, which carries the full bake.
    add_executable(ground_mesh_exact_test
        tests/ground_mesh_exact_test.cpp
        ${SRC_DIR}/tools/boulderx.cpp)
    target_include_directories(ground_mesh_exact_test PRIVATE ${SRC_DIR})

    # Template Palette + template hierarchy (master TODO 2.3/2.4): the .scl/
    # .pod scan, the link → override → materialize → reset lifecycle and the
    # Model-component / LocalAABB wire builders. Pure RAM unit test.
    add_executable(template_sources_test tests/template_sources_test.cpp)
    target_include_directories(template_sources_test PRIVATE ${SRC_DIR})
    target_link_libraries(template_sources_test PRIVATE swpod swcore)
    add_test(NAME template_sources_test COMMAND template_sources_test)

    # SCL studio mutations (master TODO 2.4a): add/rename/remove a template in
    # an ObjectLibrary while preserving every other byte, plus a byte-exact
    # save/reload round trip. Pure RAM unit test.
    add_executable(scl_studio_test tests/scl_studio_test.cpp)
    target_include_directories(scl_studio_test PRIVATE ${SRC_DIR})
    target_link_libraries(scl_studio_test PRIVATE swpod swcore)
    add_test(NAME scl_studio_test COMMAND scl_studio_test)

    # POD -> GLB ANIMATION: the clip the GLB plays must reproduce the engine's
    # poses, frame for frame. gltf_bridge_roundtrip_test deliberately excludes
    # animation from its fingerprint, so before this nothing compared an
    # exported clip against av::skin_mesh() — which is how an exported merged
    # clip could carry the clip's first frame as the bind pose AND write POD
    # quaternions without the file's documented xyz negation, while every
    # static-model test stayed green. Links only tools/ sources; no Qt, no GL.
    # Skips (loudly, exit 0) when the shipped assets are absent.
    add_executable(glb_animation_skinning_test
        tests/glb_animation_skinning_test.cpp
        ${SRC_DIR}/tools/gltf_export.cpp
        ${SRC_DIR}/tools/pod_loader.cpp
        # pod_loader.cpp calls pod_warn_if_stale() at load time, so the stamp
        # reader must be in every target that compiles the loader directly
        # (these tests link no swpod library).
        ${SRC_DIR}/tools/pod_stamp.cpp
        ${SRC_DIR}/tools/tiny_gltf_v3.c
        ${SRC_DIR}/tools/stb_impl_shim.cpp)
    target_include_directories(glb_animation_skinning_test PRIVATE
        ${SRC_DIR} ${SRC_DIR}/tools ${SRC_DIR}/stb)
    target_link_libraries(glb_animation_skinning_test PRIVATE ZLIB::ZLIB)
    add_test(NAME glb_animation_skinning_test COMMAND glb_animation_skinning_test)

    # The other half of the same pipeline: glb_animation_skinning_test pins the
    # EXPORT direction against the engine's own skinning, this pins the IMPORT
    # direction against an evaluator written from the glTF 2.0 spec (T*R*S node
    # locals, parent-chain worlds, STEP/LINEAR/CUBICSPLINE, shortest-path slerp).
    # They are compared by pairwise inter-joint distance, which is invariant
    # under any change of basis — so POD and glTF may disagree about axes and
    # the two still compare exactly. Runs on third-party GLBs (assets our own
    # exporter never touched, so a shared bug cannot hide behind itself).
    # Skips (loudly, exit 0) when the machine has none.
    add_executable(glb_import_spec_conformance_test
        tests/glb_import_spec_conformance_test.cpp
        ${SRC_DIR}/tools/gltf_import.cpp
        ${SRC_DIR}/tools/pod_loader.cpp
        ${SRC_DIR}/tools/pod_stamp.cpp
        ${SRC_DIR}/tools/tiny_gltf_v3.c
        ${SRC_DIR}/tools/stb_impl_shim.cpp)
    target_include_directories(glb_import_spec_conformance_test PRIVATE
        ${SRC_DIR} ${SRC_DIR}/tools ${SRC_DIR}/stb)
    target_link_libraries(glb_import_spec_conformance_test PRIVATE ZLIB::ZLIB)
    add_test(NAME glb_import_spec_conformance_test
             COMMAND glb_import_spec_conformance_test)

    # Skinned vertex-space coherence (GLB -> POD): convert a real GLB through the
    # shipping pipeline (gltf_import -> pod_write -> pod_load) and assert the
    # skinned mesh keeps the size it was baked at on every frame. Catches the
    # class of bug where the bind pose looks perfect and the animation tears the
    # model apart, because the vertices and the skeleton disagree about their
    # unit (pilot.glb rendered at 62-130x). Also checks the bind pose skins to
    # itself. Skips (loudly, exit 0) without real assets; SWORDIGO_GLB_ASSETS
    # points it at a corpus directory.
    add_executable(glb_import_skinning_coherence_test
        tests/glb_import_skinning_coherence_test.cpp
        ${SRC_DIR}/tools/gltf_import.cpp
        ${SRC_DIR}/tools/pod_loader.cpp
        ${SRC_DIR}/tools/pod_stamp.cpp
        ${SRC_DIR}/tools/pod_writer.cpp
        ${SRC_DIR}/tools/tiny_gltf_v3.c
        ${SRC_DIR}/tools/stb_impl_shim.cpp)
    target_include_directories(glb_import_skinning_coherence_test PRIVATE
        ${SRC_DIR} ${SRC_DIR}/tools ${SRC_DIR}/stb)
    target_link_libraries(glb_import_skinning_coherence_test PRIVATE ZLIB::ZLIB)
    add_test(NAME glb_import_skinning_coherence_test
             COMMAND glb_import_skinning_coherence_test)

    # Which bone the 1-bone rigid bake picks. The engine reads ONE bone index
    # per vertex and ignores weights, so the collapse is a modelling decision:
    # it used to be argmax(weight) at the bind pose, i.e. chosen from one
    # instant for a mesh that exists to be animated. refine_rigid_skin() scores
    # each vertex's influences against every pose of every clip instead. This
    # asserts it changes something on a real rig, never worsens the deviation
    # from smooth skinning, never touches the geometry, and is a strict no-op
    # when there are no clips. Skips (loudly, exit 0) without real assets.
    add_executable(rigid_skin_refine_test
        tests/rigid_skin_refine_test.cpp
        ${SRC_DIR}/tools/gltf_import.cpp
        ${SRC_DIR}/tools/pod_loader.cpp
        ${SRC_DIR}/tools/pod_stamp.cpp
        ${SRC_DIR}/tools/tiny_gltf_v3.c
        ${SRC_DIR}/tools/stb_impl_shim.cpp)
    target_include_directories(rigid_skin_refine_test PRIVATE
        ${SRC_DIR} ${SRC_DIR}/tools ${SRC_DIR}/stb)
    target_link_libraries(rigid_skin_refine_test PRIVATE ZLIB::ZLIB)
    add_test(NAME rigid_skin_refine_test COMMAND rigid_skin_refine_test)

    # The tags we write must be the tags the GAME reads, which is not the same
    # set our own loader accepts. Tag 5010 (a node's raw 4x4) is a NO-OP in
    # CPVRTModelPOD::ReadFromMemory — `case 0x1392u: goto LABEL_202;` skips the
    # payload and stores nothing — so every matrix-only node (i.e. every
    # Sketchfab GLB export) ended up with no transform at all and the node's
    # world matrix was built from uninitialised storage. ruby_gg never noticed
    # because it reads 5010 happily. This scans the bytes pod_write produced
    # with a walker that shares no code with pod_loader, so a future edit can
    # not reintroduce the tag without failing here. Also scans converter-stamped
    # PODs on disk; skips (loudly, exit 0) when there are none.
    add_executable(pod_game_reader_contract_test
        tests/pod_game_reader_contract_test.cpp
        ${SRC_DIR}/tools/pod_loader.cpp
        ${SRC_DIR}/tools/pod_writer.cpp
        ${SRC_DIR}/tools/pod_stamp.cpp)
    target_include_directories(pod_game_reader_contract_test PRIVATE
        ${SRC_DIR} ${SRC_DIR}/tools)
    add_test(NAME pod_game_reader_contract_test COMMAND pod_game_reader_contract_test)

    # APK session import + registry (master TODO 4.1a): builds a tiny fake
    # APK with the shared zip writer, imports it, asserts byte-identical
    # extraction + session.json registry round trip + validation failures.
    # $RUBY_SESSIONS_DIR keeps the test hermetic (no ~/.ruby writes).
    add_executable(apk_session_test
        tests/apk_session_test.cpp
        ${SRC_DIR}/platform/zip_archive.cpp
        ${SRC_DIR}/tools/apk_session.cpp)
    target_include_directories(apk_session_test PRIVATE ${SRC_DIR})
    target_link_libraries(apk_session_test PRIVATE ZLIB::ZLIB)
    add_test(NAME apk_session_test COMMAND apk_session_test)

    # Ground Mesh Studio: simple-polygon enforcement (repaint-lag fix) + the
    # "Add to Scene" object round trip. Runs headless (offscreen platform).
    # boulderx.cpp comes along because the studio now writes both .swdm dialects
    # and drives the generator choice; this target links filerift (boulder) but
    # not boulderx, so without it the studio's symbols would be unresolved.
    add_executable(ground_mesh_studio_test
        tests/ground_mesh_studio_test.cpp
        ${SRC_DIR}/ruby/tools/ground_mesh_studio.cpp
        ${SRC_DIR}/tools/boulderx.cpp
        ${SRC_DIR}/ruby/core/project_context.cpp)
    set_target_properties(ground_mesh_studio_test PROPERTIES AUTOMOC ON)
    target_include_directories(ground_mesh_studio_test PRIVATE ${SRC_DIR})
    target_link_libraries(ground_mesh_studio_test PRIVATE
        filerift swpod Qt6::Widgets Qt6::Gui Qt6::Core OpenGL::GL)
    add_test(NAME ground_mesh_studio_test COMMAND ground_mesh_studio_test)
    set_tests_properties(ground_mesh_studio_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # GroundMesh CollisionShapeComponent wire format. Field NUMBERS and wire
    # types are asserted against the decompiled engine, not just against
    # boulder's own output: Ruby renders ground meshes from parsed vertex data,
    # so a shape written at the wrong field numbers still draws correctly in the
    # editor and only means something different to the game.
    add_executable(boulder_collision_fields_test
        tests/boulder_collision_fields_test.cpp)
    target_include_directories(boulder_collision_fields_test PRIVATE
        ${SRC_DIR}
        ${SRC_DIR}/tools
        ${SRC_DIR}/platform)
    target_link_libraries(boulder_collision_fields_test PRIVATE filerift)
    add_test(NAME boulder_collision_fields_test COMMAND boulder_collision_fields_test)

    # BoulderX — the Caver-faithful ground generator. Behaviour suite covers the
    # shape classes boulder cannot produce (concave outlines, arcs, per-node
    # depth, HorizNoise relief); the parity suite pins the output against meshes
    # the 1.4.13 engine itself baked into the shipped scenes.
    # boulder.cpp is in this target on purpose: the .swdm dialect gate is a
    # two-sided contract (a v1 sheet loads in BoulderX, a v2 sheet is refused by
    # boulder) and neither half can be asserted without the other parser.
    add_executable(boulderx_test
        tests/boulderx_test.cpp
        ${SRC_DIR}/tools/boulderx.cpp
        ${SRC_DIR}/tools/boulder.cpp)
    target_include_directories(boulderx_test PRIVATE ${SRC_DIR})
    add_test(NAME boulderx_test COMMAND boulderx_test)

    add_executable(boulderx_engine_parity_test
        tests/boulderx_engine_parity_test.cpp
        ${SRC_DIR}/tools/boulderx.cpp)
    target_include_directories(boulderx_engine_parity_test PRIVATE ${SRC_DIR})
    add_test(NAME boulderx_engine_parity_test COMMAND boulderx_engine_parity_test)

    # Zypher (Gen 3 Procedural World Mesh Engine) behavioral & regression test
    add_executable(zypher_test
        tests/zypher_test.cpp
        ${SRC_DIR}/tools/roller.cpp
        ${SRC_DIR}/tools/boulderx.cpp
        ${SRC_DIR}/tools/pod_loader.cpp
        ${SRC_DIR}/tools/pod_stamp.cpp)
    target_include_directories(zypher_test PRIVATE ${SRC_DIR} ${SRC_DIR}/tools)
    add_test(NAME zypher_test COMMAND zypher_test)

    # Title-bar window controls must render visibly (painted icons). This
    # guards the frameless window's Close/Min/Max buttons against font-glyph
    # fallbacks that made them invisible on several Linux font stacks (KDE
    # Plasma Wayland in particular — no WM decorations there either).
    add_executable(titlebar_render_test
        tests/titlebar_render_test.cpp
        ${SRC_DIR}/ruby/editor/ruby_title_bar.cpp)
    set_target_properties(titlebar_render_test PROPERTIES AUTOMOC ON)
    target_include_directories(titlebar_render_test PRIVATE ${SRC_DIR})
    target_link_libraries(titlebar_render_test PRIVATE
        Qt6::Widgets Qt6::Gui Qt6::Core)
    add_test(NAME titlebar_render_test COMMAND titlebar_render_test)
    set_tests_properties(titlebar_render_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Asset browser quick sync: files created by an external process (and a
    # root directory that appears after set_root_path) must show up in the
    # panel's tree without any manual refresh.
    add_executable(asset_sync_test
        tests/asset_sync_test.cpp
        ${SRC_DIR}/ruby/panels/asset_browser_panel.cpp)
    set_target_properties(asset_sync_test PROPERTIES AUTOMOC ON)
    target_include_directories(asset_sync_test PRIVATE ${SRC_DIR})
    target_link_libraries(asset_sync_test PRIVATE
        Qt6::Widgets Qt6::Gui Qt6::Core)
    add_test(NAME asset_sync_test COMMAND asset_sync_test)
    set_tests_properties(asset_sync_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Audio viewer (WAV/MP3/OGG): decodes a real RIFF WAV, asserts metadata +
    # waveform, exercises the async load path and corrupt-input handling. The
    # SDL3 / mpg123 / vorbisfile decoders are already REQUIRED project deps.
    add_executable(audio_viewer_test
        tests/audio_viewer_test.cpp
        ${SRC_DIR}/ruby/panels/audio_viewer_panel.cpp)
    set_target_properties(audio_viewer_test PROPERTIES AUTOMOC ON)
    target_include_directories(audio_viewer_test PRIVATE
        ${SRC_DIR} ${MPG123_INCLUDE_DIRS} ${VORBISFILE_INCLUDE_DIRS})
    target_link_libraries(audio_viewer_test PRIVATE
        Qt6::Widgets Qt6::Gui Qt6::Core
        SDL3::SDL3 ${SDL3_IMAGE_LINK}
        ${MPG123_LIBRARIES} ${VORBISFILE_LIBRARIES})
    add_test(NAME audio_viewer_test COMMAND audio_viewer_test)
    set_tests_properties(audio_viewer_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(pvr_astc_test tests/pvr_astc_test.cpp)
    target_include_directories(pvr_astc_test PRIVATE ${SRC_DIR})
    target_link_libraries(pvr_astc_test PRIVATE swfmt swcore OpenGL::GL)
    add_test(NAME pvr_astc_test COMMAND pvr_astc_test)

    # The real Viewport3DWidget + its backends, shared by the scene-editing
    # acceptance tests below (offscreen GL, Mesa swrast; data file optional).
    set(RUBY_VIEWPORT_TEST_SOURCES
        ${SRC_DIR}/ruby/viewport/viewport_3d_widget.cpp
        ${SRC_DIR}/ruby/viewport/camera_bounds_gizmo.cpp
        ${SRC_DIR}/ruby/viewport/ruby_gizmo.cpp
        ${SRC_DIR}/ruby/viewport/ruby_picking.cpp
        ${SRC_DIR}/ruby/viewport/scene_loading_overlay.cpp
        # The forward render pipeline the viewport widget owns (task 3.1 added
        # water/portal/particle + the FBO chain): viewport_3d_widget.cpp
        # instantiates these directly, so their vtables must link here too.
        ${SRC_DIR}/ruby/render/viewport_shader.cpp
        ${SRC_DIR}/ruby/render/light_rig.cpp
        ${SRC_DIR}/ruby/render/fbo_chain.cpp
        ${SRC_DIR}/ruby/render/ssao_pass.cpp
        ${SRC_DIR}/ruby/render/post_pass.cpp
        ${SRC_DIR}/ruby/render/water_renderer.cpp
        ${SRC_DIR}/ruby/render/portal_renderer.cpp
        ${SRC_DIR}/ruby/render/particle_engine.cpp
        # glb_model.cpp owns GLBModel::update_animation()/set_active_animation()/
        # current_animation_duration(), which viewport_3d_widget.cpp calls. It
        # was missing from this list, so scene_edit_acceptance_test and
        # scene_mesh_edit_test could never link (undefined GLBModel symbols)
        # and silently showed up as "Not Run" in ctest.
        ${SRC_DIR}/ruby/render/glb_model.cpp
        ${SRC_DIR}/ruby/core/project_context.cpp
        ${SRC_DIR}/tools/pod_loader.cpp
        ${SRC_DIR}/tools/pod_writer.cpp
        ${SRC_DIR}/tools/pod_convert.cpp
        ${SRC_DIR}/tools/scene_loader.cpp
        ${SRC_DIR}/tools/filerift.cpp
        ${SRC_DIR}/tools/scene_asset_resolver.cpp
        ${SRC_DIR}/tools/scene_workspace.cpp
        ${SRC_DIR}/tools/scene_entity.cpp
        ${SRC_DIR}/tools/scene_physics.cpp
        ${SRC_DIR}/tools/scene_collision.cpp
        ${SRC_DIR}/tools/scene_terrain.cpp
        ${SRC_DIR}/tools/scene_lua.cpp
        ${SRC_DIR}/tools/scene_game.cpp
        ${SRC_DIR}/tools/scene_creator.cpp
        ${SRC_DIR}/tools/scene_generator.cpp
        ${SRC_DIR}/tools/scene_generator_v2.cpp
        ${SRC_DIR}/tools/scene_generator_v2_3d.cpp
        ${SRC_DIR}/tools/scene_generator_v3.cpp
        ${SRC_DIR}/tools/scene_v3_db.cpp
        ${SRC_DIR}/tools/boulder.cpp
        ${SRC_DIR}/tools/boulderx.cpp
        ${SRC_DIR}/tools/roller.cpp
        ${SRC_DIR}/tools/rubymesh.cpp
        ${SRC_DIR}/tools/obj_loader.cpp
        ${SRC_DIR}/tools/ani_loader.cpp
        ${SRC_DIR}/tools/scn_loader.cpp
        ${SRC_DIR}/tools/map_loader.cpp
        ${SRC_DIR}/tools/gltf_export.cpp
        ${SRC_DIR}/tools/gltf_import.cpp
        ${SRC_DIR}/tools/gltf_bridge.cpp
        ${SRC_DIR}/tools/fbx_import.cpp
        ${SRC_DIR}/tools/intellij.cpp
        ${SRC_DIR}/tools/batch_converter.cpp
        ${SRC_DIR}/tools/ufbx/ufbx.c
        ${SRC_DIR}/tools/tiny_gltf_v3.c
        ${SRC_DIR}/platform/pvr_loader.cpp
        ${SRC_DIR}/platform/pvrtc_decoder.cpp
        ${SRC_DIR}/platform/astc_decoder.cpp
        ${SRC_DIR}/platform/win_dll_dir.cpp)
    function(ruby_viewport_test NAME SOURCE)
        add_executable(${NAME}
            ${SOURCE}
            ${RUBY_VIEWPORT_TEST_SOURCES})
        set_target_properties(${NAME} PROPERTIES AUTOMOC ON)
        target_compile_definitions(${NAME} PRIVATE
            SWORDIGO_NO_IMGUI
            BATCH_CONVERTER_NO_UI
            SWORDIGO_SRC_DIR="${SRC_DIR}")
        target_include_directories(${NAME} PRIVATE
            ${SRC_DIR}
            ${SRC_DIR}/ruby
            ${SRC_DIR}/tools
            ${SRC_DIR}/tools/ufbx
            ${SRC_DIR}/stb
            ${SRC_DIR}/platform
            ${SRC_DIR}/sre/base/lua/src)
        target_link_libraries(${NAME} PRIVATE
            Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets
            OpenGL::GL ZLIB::ZLIB swcore swfmt swpod filerift
            SDL3::SDL3 ${SDL3_IMAGE_LINK} ${SWORDIGO_LIBM} ${SWORDIGO_SOCKLIB})
        if (NOT WIN32)
            target_link_libraries(${NAME} PRIVATE dl pthread util)
        endif()
        if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_link_libraries(${NAME} PRIVATE rt)
        endif()
        add_test(NAME ${NAME} COMMAND ${NAME})
        set_tests_properties(${NAME} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endfunction()

    # Scene-object clipboard editing: copy/paste/duplicate/delete/move through
    # the real Viewport3DWidget public API against a real .scene file.
    ruby_viewport_test(scene_edit_acceptance_test tests/scene_edit_acceptance_test.cpp)

    # In-scene mesh editor (projection lock): camera locks to the ground
    # polygon's plane, synthesized mouse drags move a vertex, and committing
    # regenerates the object's ground geometry through boulder.
    ruby_viewport_test(scene_mesh_edit_test tests/scene_mesh_edit_test.cpp)

    if (SWORDIGO_BUILD_RUBY_GG)
        # Ctrl+S must preserve viewport edits. Regression: the FileRift text
        # sync refreshed the cached buffer but not the IDE's visible text, so
        # scene_has_user_text_edits() misread our own sync as user typing and
        # the save reverted the edit. Drives the real RubyMainWindow through
        # its private slots (offscreen Qt + Mesa swrast).
        set(SCENE_SAVE_TEST_SOURCES ${RUBY_GG_SOURCES})
        list(REMOVE_ITEM SCENE_SAVE_TEST_SOURCES ${SRC_DIR}/ruby/main.cpp)
        add_executable(scene_save_preserves_edit_test
            tests/scene_save_preserves_edit_test.cpp
            ${SCENE_SAVE_TEST_SOURCES})
        set_target_properties(scene_save_preserves_edit_test PROPERTIES
            AUTOMOC ON AUTORCC ON AUTOUIC ON)
        target_compile_definitions(scene_save_preserves_edit_test PRIVATE
            SWORDIGO_NO_IMGUI
            BATCH_CONVERTER_NO_UI
            SWORDIGO_SRC_DIR="${SRC_DIR}")
        target_include_directories(scene_save_preserves_edit_test PRIVATE
            ${SRC_DIR}
            ${SRC_DIR}/ruby
            ${SRC_DIR}/ruby/git/include
            ${SRC_DIR}/tools
            ${SRC_DIR}/tools/ufbx
            ${SRC_DIR}/stb
            ${SRC_DIR}/platform
            ${SRC_DIR}/sre/base/lua/src
            ${MPG123_INCLUDE_DIRS}
            ${VORBISFILE_INCLUDE_DIRS})
        target_link_libraries(scene_save_preserves_edit_test PRIVATE
            Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets Qt6::Network
            OpenGL::GL ZLIB::ZLIB swcore swfmt swpod filerift git2_static caver
            SDL3::SDL3 ${SDL3_IMAGE_LINK}
            ${MPG123_LIBRARIES} ${VORBISFILE_LIBRARIES}
            ${SWORDIGO_LIBM} ${SWORDIGO_SOCKLIB})
        if (NOT WIN32)
            target_link_libraries(scene_save_preserves_edit_test PRIVATE dl pthread util)
        endif()
        if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_link_libraries(scene_save_preserves_edit_test PRIVATE rt)
        endif()
        add_test(NAME scene_save_preserves_edit_test COMMAND scene_save_preserves_edit_test)
        set_tests_properties(scene_save_preserves_edit_test PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

        # Scene-wide snapshot undo (web-editor model): duplicate/delete/
        # visibility/component-paste edits land on the per-scene QUndoStack as
        # full scene-binary snapshots; undo/redo restores them (structural via
        # rebuild, in-place via the fast apply). Also drives the inspector's
        # componentPasted signal end-to-end (fresh type id + undoable).
        set(SCENE_UNDO_TEST_SOURCES ${RUBY_GG_SOURCES})
        list(REMOVE_ITEM SCENE_UNDO_TEST_SOURCES ${SRC_DIR}/ruby/main.cpp)
        add_executable(scene_undo_test
            tests/scene_undo_test.cpp
            ${SCENE_UNDO_TEST_SOURCES})
        set_target_properties(scene_undo_test PROPERTIES
            AUTOMOC ON AUTORCC ON AUTOUIC ON)
        target_compile_definitions(scene_undo_test PRIVATE
            SWORDIGO_NO_IMGUI
            BATCH_CONVERTER_NO_UI
            SWORDIGO_SRC_DIR="${SRC_DIR}")
        target_include_directories(scene_undo_test PRIVATE
            ${SRC_DIR}
            ${SRC_DIR}/ruby
            ${SRC_DIR}/ruby/git/include
            ${SRC_DIR}/tools
            ${SRC_DIR}/tools/ufbx
            ${SRC_DIR}/stb
            ${SRC_DIR}/platform
            ${SRC_DIR}/sre/base/lua/src
            ${MPG123_INCLUDE_DIRS}
            ${VORBISFILE_INCLUDE_DIRS})
        target_link_libraries(scene_undo_test PRIVATE
            Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets Qt6::Network
            OpenGL::GL ZLIB::ZLIB swcore swfmt swpod filerift git2_static caver
            SDL3::SDL3 ${SDL3_IMAGE_LINK}
            ${MPG123_LIBRARIES} ${VORBISFILE_LIBRARIES}
            ${SWORDIGO_LIBM} ${SWORDIGO_SOCKLIB})
        if (NOT WIN32)
            target_link_libraries(scene_undo_test PRIVATE dl pthread util)
        endif()
        if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_link_libraries(scene_undo_test PRIVATE rt)
        endif()
        add_test(NAME scene_undo_test COMMAND scene_undo_test)
        set_tests_properties(scene_undo_test PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()

    if (SWORDIGO_BUILD_RUBY)
        add_executable(styx_smoke tests/styx_smoke.cpp ${SRC_DIR}/tools/intellij.cpp)
        swordigo_target(styx_smoke)
        target_link_libraries(styx_smoke PRIVATE swgui)
        add_test(NAME styx_smoke COMMAND styx_smoke)

        # RubyMesh .rbm data layer (pure std C++17, no external links).
        add_executable(rubymesh_roundtrip_test
            tests/rubymesh_roundtrip_test.cpp
            ${SRC_DIR}/tools/rubymesh.cpp)
        swordigo_target(rubymesh_roundtrip_test)
        add_test(NAME rubymesh_roundtrip_test COMMAND rubymesh_roundtrip_test)

        # Raylib ray picking & matrix calculation test
        add_executable(ruby_picking_test
            tests/ruby_picking_test.cpp
            ${SRC_DIR}/ruby/viewport/ruby_picking.cpp)
        swordigo_target(ruby_picking_test)
        target_link_libraries(ruby_picking_test PRIVATE Qt6::Core Qt6::Gui)
        add_test(NAME ruby_picking_test COMMAND ruby_picking_test)

        # Graphy node geometry: content-sized rects, reserved header bands,
        # elided pills. Locks down the reported text-overlap/clipping bugs.
        # rbsrc: the merged ObjectStudio + visual-scripter format. Headless, so
        # it also serves ruby_cli and any future CI arc check.
        add_executable(rbsrc_test
            tests/rbsrc_test.cpp
            ${SRC_DIR}/tools/rbsrc.cpp)
        target_include_directories(rbsrc_test PRIVATE ${SRC_DIR} ${SRC_DIR}/tools)
        swordigo_target(rbsrc_test)
        add_test(NAME rbsrc_test COMMAND rbsrc_test)

        add_executable(graphy_layout_test
            tests/graphy_layout_test.cpp
            ${SRC_DIR}/ruby/graph/graphy.cpp
            ${SRC_DIR}/ruby/graph/graphy_canvas.cpp
            ${SRC_DIR}/ruby/graph/graphy_layout.cpp)
        set_target_properties(graphy_layout_test PROPERTIES AUTOMOC ON)
        swordigo_target(graphy_layout_test)
        target_link_libraries(graphy_layout_test PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
        add_test(NAME graphy_layout_test COMMAND graphy_layout_test)
        set_tests_properties(graphy_layout_test PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

        # Graphy's document backend: .scene / .scl binary -> node graph. The
        # fixtures are real Swordigo protobuf, so this covers the schema
        # resolution, the loader and the builder together.
        add_executable(graphy_scene_builder_test
            tests/graphy_scene_builder_test.cpp
            ${SRC_DIR}/ruby/graph/graphy.cpp
            ${SRC_DIR}/ruby/graph/graphy_layout.cpp
            ${SRC_DIR}/ruby/graph/graphy_scene_builder.cpp)
        set_target_properties(graphy_scene_builder_test PROPERTIES AUTOMOC ON)
        swordigo_target(graphy_scene_builder_test)
        target_include_directories(graphy_scene_builder_test PRIVATE
            ${SRC_DIR} ${SRC_DIR}/tools ${SRC_DIR}/platform ${SRC_DIR}/ruby/graph)
        # swpod brings the scene loader; filerift brings the FileRift recoder and
        # the host Lua chunk compiler. swcore/swfmt are not named directly: their
        # weak `g_instance_assets_dir` definition cannot satisfy an executable's
        # strong reference, so asking for them here would fail to link.
        target_link_libraries(graphy_scene_builder_test PRIVATE
            Qt6::Core Qt6::Gui Qt6::Widgets
            swpod filerift ZLIB::ZLIB)
        add_test(NAME graphy_scene_builder_test COMMAND graphy_scene_builder_test)
        set_tests_properties(graphy_scene_builder_test PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

        # Asset browser panel test
        add_executable(asset_browser_test
            tests/asset_browser_test.cpp
            ${SRC_DIR}/ruby/panels/asset_browser_panel.cpp)
        set_target_properties(asset_browser_test PROPERTIES AUTOMOC ON)
        target_include_directories(asset_browser_test PRIVATE ${SRC_DIR})
        target_link_libraries(asset_browser_test PRIVATE Qt6::Widgets Qt6::Gui Qt6::Core)
        add_test(NAME asset_browser_test COMMAND asset_browser_test)
        set_tests_properties(asset_browser_test PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()

    # Restore the app-level output dir so anything added below this block still
    # lands in <dist>/ next to the applications, and put $ORIGIN back to the
    # app layout.
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${_swordigo_app_runtime_out}")
    set(SWORDIGO_ORIGIN_LIBS_DIR "$ORIGIN/libs")
    unset(_swordigo_app_runtime_out)
else()
    # -----------------------------------------------------------------------
    # Normal build: declare no test target — AND make the build tree admit it.
    # -----------------------------------------------------------------------
    # `include(CTest)` above only calls enable_testing() when BUILD_TESTING is
    # ON, so a normal configure writes no CTestTestfile.cmake — but it also
    # never touches one already sitting there from an earlier dev-mode
    # configure of the SAME directory. `ctest --test-dir <normal build>` would
    # then list those stale entries, and happily RUN them, because the dev-mode
    # binaries are still over in <dist>/tests/. That is the same bin/-pollution
    # problem in a different disguise: the directory would be lying about what
    # it built. Drop the generated test metadata so the tree tells the truth
    # (a later -DSWORDIGO_DEV_MODE=ON reconfigure writes it back).
    #
    # Only ever removes a file CMake generated inside the build directory.
    file(GLOB_RECURSE _swordigo_stale_ctest
         "${CMAKE_BINARY_DIR}/CTestTestfile.cmake")
    if (_swordigo_stale_ctest)
        file(REMOVE ${_swordigo_stale_ctest})
        message(STATUS
            "CMAKE MODE DEV is OFF: removed ${_swordigo_stale_ctest} from a previous "
            "dev configure (no test target is declared in a normal build)")
    endif()
    unset(_swordigo_stale_ctest)
endif()
