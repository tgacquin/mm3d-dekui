# Dedicated developer presentation slice; never enabled in the unmodified baseline.
set(MM3D_IMGUI_DIR "${PROJECT_SOURCE_DIR}/externals/mm3d-imgui")
add_library(mm3d_imgui STATIC
    ${MM3D_IMGUI_DIR}/imgui.cpp
    ${MM3D_IMGUI_DIR}/imgui_draw.cpp
    ${MM3D_IMGUI_DIR}/imgui_tables.cpp
    ${MM3D_IMGUI_DIR}/imgui_widgets.cpp
    ${MM3D_IMGUI_DIR}/backends/imgui_impl_vulkan.cpp)
target_include_directories(mm3d_imgui SYSTEM PUBLIC ${MM3D_IMGUI_DIR})
target_compile_definitions(mm3d_imgui PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
target_link_libraries(mm3d_imgui PUBLIC vulkan-headers)

add_library(mm3d_overlay STATIC overlay.cpp overlay.h settings.cpp settings.h notebook.cpp notebook.h diagnostics.h trace.cpp trace.h shader_usage.cpp shader_usage.h texture_picker.cpp texture_picker.h)
target_sources(mm3d_overlay PRIVATE prompt_layer.cpp prompt_layer.h camera_writer.cpp camera_writer.h scene_effects.cpp scene_effects.h)
target_sources(mm3d_overlay PRIVATE link_pose_capture.cpp link_pose_capture.h effect_capture.cpp effect_capture.h)
target_sources(mm3d_overlay PRIVATE profiles.cpp profiles.h classic_saving.cpp classic_saving.h)
target_sources(mm3d_overlay PRIVATE translation.cpp translation.h)
target_link_libraries(mm3d_overlay PUBLIC mm3d_imgui PRIVATE citra_common Qt6::Core nihstro-headers)

if (ANDROID)
    # Desktop test executables and window fixtures do not belong in the APK.
    return()
endif()

add_executable(mm3d_player_settings_tests player_settings_bridge_tests.cpp)
add_executable(mm3d_translation_tests translation_tests.cpp translation.cpp)
target_include_directories(mm3d_translation_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(mm3d_translation_tests PRIVATE Qt6::Core citra_common ${PLATFORM_LIBRARIES})
add_test(NAME mm3d_translation COMMAND mm3d_translation_tests)
target_include_directories(mm3d_player_settings_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
add_test(NAME mm3d_player_settings COMMAND mm3d_player_settings_tests)

add_executable(mm3d_visibility_depth_tests visibility_depth_tests.cpp)
target_include_directories(mm3d_visibility_depth_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
add_test(NAME mm3d_visibility_depth COMMAND mm3d_visibility_depth_tests)

add_executable(mm3d_interpolation_timeline_tests interpolation_timeline_tests.cpp)
target_include_directories(mm3d_interpolation_timeline_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
add_test(NAME mm3d_interpolation_timeline COMMAND mm3d_interpolation_timeline_tests)

add_executable(mm3d_cpu_vertex_replay_tests cpu_vertex_replay_tests.cpp)
target_link_libraries(mm3d_cpu_vertex_replay_tests PRIVATE mm3d_overlay video_core
    nihstro-headers tsl::robin_map Qt6::Core ${PLATFORM_LIBRARIES})
add_test(NAME mm3d_cpu_vertex_replay COMMAND mm3d_cpu_vertex_replay_tests)

add_executable(mm3d_tests tests.cpp settings.cpp trace.cpp shader_usage.cpp camera_writer.cpp)
target_link_libraries(mm3d_tests PRIVATE Qt6::Core nihstro-headers Boost::boost)
add_test(NAME mm3d_diagnostics COMMAND mm3d_tests)
add_executable(mm3d_fast_time_tests fast_time_tests.cpp)
target_include_directories(mm3d_fast_time_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
add_test(NAME mm3d_fast_time COMMAND mm3d_fast_time_tests)
add_executable(mm3d_item_slide_tests item_slide_interpolation_tests.cpp)
target_include_directories(mm3d_item_slide_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
add_test(NAME mm3d_item_slide COMMAND mm3d_item_slide_tests)

add_executable(mm3d_inventory_interpolation_tests inventory_interpolation_tests.cpp)
target_include_directories(mm3d_inventory_interpolation_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
add_test(NAME mm3d_inventory_interpolation COMMAND mm3d_inventory_interpolation_tests)
add_executable(mm3d_classic_save_tests classic_save_store_tests.cpp)
target_link_libraries(mm3d_classic_save_tests PRIVATE Qt6::Core)
add_test(NAME mm3d_classic_save_storage COMMAND mm3d_classic_save_tests)
add_executable(mm3d_classic_saving_tests classic_saving_tests.cpp classic_saving.cpp)
target_include_directories(mm3d_classic_saving_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(mm3d_classic_saving_tests PRIVATE Qt6::Core)
add_test(NAME mm3d_classic_save_coordinator COMMAND mm3d_classic_saving_tests)
add_executable(mm3d_content_tests content_tests.cpp)
target_link_libraries(mm3d_content_tests PRIVATE mm3d_overlay video_core Qt6::Core ${PLATFORM_LIBRARIES})
add_test(NAME mm3d_content_matching COMMAND mm3d_content_tests)
add_executable(mm3d_registry_tests registry_tests.cpp)
target_link_libraries(mm3d_registry_tests PRIVATE mm3d_overlay video_core Qt6::Core ${PLATFORM_LIBRARIES})
add_test(NAME mm3d_profile_registry COMMAND mm3d_registry_tests)

add_executable(mm3d_vulkan_fixture vulkan_fixture.cpp pass_switch_fixture.cpp visibility_depth_fixture.cpp scene_effects_fixture.cpp)
target_sources(mm3d_vulkan_fixture PRIVATE gpu_interpolation_fixture.cpp)
target_link_libraries(mm3d_vulkan_fixture PRIVATE mm3d_overlay video_core tsl::robin_map Qt6::Core ${PLATFORM_LIBRARIES})
# GPU fixture is explicit: CI machines without a Vulkan device should not silently skip a failure.

if (APPLE OR WIN32)
    add_executable(mm3d_present_fixture present_fixture.cpp)
    target_link_libraries(mm3d_present_fixture PRIVATE mm3d_overlay video_core Qt6::Gui ${PLATFORM_LIBRARIES})
    if (APPLE)
        target_sources(mm3d_present_fixture PRIVATE ../citra_qt/util/metal_util.mm)
        target_link_libraries(mm3d_present_fixture PRIVATE "-framework QuartzCore" "-framework AppKit")
    endif()
endif()

if(TARGET dynarmic)
    add_executable(mm3d_camera_writer_jit_tests camera_writer_jit_tests.cpp)
    target_include_directories(mm3d_camera_writer_jit_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(mm3d_camera_writer_jit_tests PRIVATE dynarmic)
    add_test(NAME mm3d_camera_writer_jit COMMAND mm3d_camera_writer_jit_tests)
    set_tests_properties(mm3d_camera_writer_jit PROPERTIES TIMEOUT 15)
endif()

add_executable(mm3d_camera_live_tests camera_live_tests.cpp)
target_include_directories(mm3d_camera_live_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
add_test(NAME mm3d_camera_live COMMAND mm3d_camera_live_tests)

if(ENABLE_SDL2)
    add_executable(mm3d_controller_tests controller_tests.cpp)
    # Keep this as a console test on Windows; the game targets use the GUI
    # subsystem, but this executable has a normal int main().
    set_target_properties(mm3d_controller_tests PROPERTIES WIN32_EXECUTABLE FALSE)
    target_link_libraries(mm3d_controller_tests PRIVATE input_common SDL2::SDL2)
    add_test(NAME mm3d_controller_defaults COMMAND mm3d_controller_tests)
endif()

# Exercise native menu routing/geometry on every release runner, with assertions
# active even in Release builds (these checks previously ran only by hand).
add_executable(mm3d_native_inventory_tests ../../tools/mm3d/native_inventory_check.cpp)
target_include_directories(mm3d_native_inventory_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
if(MSVC)
    target_compile_options(mm3d_native_inventory_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(mm3d_native_inventory_tests PRIVATE -UNDEBUG)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # Legacy native fixture/header formatting uses multiple statements per line.
        target_compile_options(mm3d_native_inventory_tests PRIVATE -Wno-misleading-indentation)
    endif()
endif()
if(APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_definitions(mm3d_native_inventory_tests PRIVATE
        _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG)
endif()
add_test(NAME mm3d_native_inventory COMMAND mm3d_native_inventory_tests)

add_executable(mm3d_camera_midpoint_tests camera_midpoint_tests.cpp)
target_include_directories(mm3d_camera_midpoint_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
add_test(NAME mm3d_camera_midpoint COMMAND mm3d_camera_midpoint_tests)

add_executable(mm3d_stick_controls_tests ../../tools/mm3d/stick_controls_check.cpp)
target_include_directories(mm3d_stick_controls_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
if(MSVC)
    target_compile_options(mm3d_stick_controls_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(mm3d_stick_controls_tests PRIVATE -UNDEBUG)
endif()
add_test(NAME mm3d_stick_controls COMMAND mm3d_stick_controls_tests)

add_executable(mm3d_ice_particle_tests ../../tools/mm3d/ice_particle_check.cpp)
target_include_directories(mm3d_ice_particle_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
if(MSVC)
    target_compile_options(mm3d_ice_particle_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(mm3d_ice_particle_tests PRIVATE -UNDEBUG)
endif()
add_test(NAME mm3d_ice_particles COMMAND mm3d_ice_particle_tests)

add_executable(mm3d_photo_capture_tests ${PROJECT_SOURCE_DIR}/tools/mm3d/photo_capture_check.cpp)
target_include_directories(mm3d_photo_capture_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(mm3d_photo_capture_tests PRIVATE Threads::Threads)
add_test(NAME mm3d_photo_capture COMMAND mm3d_photo_capture_tests)
