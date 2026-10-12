file(GLOB_RECURSE GB_RUNTIME_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

add_executable(GambitRuntime ${GB_RUNTIME_SOURCES})
gambit_configure_target(GambitRuntime)
target_link_libraries(GambitRuntime PRIVATE Gambit::Gambit)

if(GB_BUILD_TESTS)
    # Any error or critical log line fails a smoke test (that includes Vulkan validation errors). A machine without a
    # usable Vulkan device skips the rendering ones instead of failing them.
    set(GB_RUNTIME_FAIL_REGEX "\\[error\\]|\\[critical\\]")
    set(GB_RUNTIME_SKIP_REGEX "Vulkan is not available|no suitable Vulkan device")

    # Simulation only: no window, no GPU, exactly ten synthetic frames of one simulation tick each.
    add_test(NAME GambitRuntime.Headless COMMAND GambitRuntime --headless --no-gpu --frames 10)
    set_tests_properties(GambitRuntime.Headless PROPERTIES
        PASS_REGULAR_EXPRESSION "ran 10 frames, 10 simulation ticks"
        FAIL_REGULAR_EXPRESSION "${GB_RUNTIME_FAIL_REGEX}")
    add_test(NAME GambitRuntime.BadArguments COMMAND GambitRuntime --headless --frames zero)
    set_tests_properties(GambitRuntime.BadArguments PROPERTIES WILL_FAIL TRUE)

    # Offscreen rendering into the headless back buffers.
    add_test(NAME GambitRuntime.HeadlessRender COMMAND GambitRuntime --headless --frames 10)
    set_tests_properties(GambitRuntime.HeadlessRender PROPERTIES
        PASS_REGULAR_EXPRESSION "rendered 10 frames"
        FAIL_REGULAR_EXPRESSION "${GB_RUNTIME_FAIL_REGEX}"
        SKIP_REGULAR_EXPRESSION "${GB_RUNTIME_SKIP_REGEX}")

    # With a window and a swap chain, on a virtual display where one is available.
    find_program(GB_XVFB_RUN xvfb-run)
    if(GB_XVFB_RUN)
        add_test(NAME GambitRuntime.Window COMMAND ${GB_XVFB_RUN} -a $<TARGET_FILE:GambitRuntime> --frames 10)
        set_tests_properties(GambitRuntime.Window PROPERTIES
            PASS_REGULAR_EXPRESSION "rendered 10 frames"
            FAIL_REGULAR_EXPRESSION "${GB_RUNTIME_FAIL_REGEX}"
            SKIP_REGULAR_EXPRESSION "${GB_RUNTIME_SKIP_REGEX}")
    endif()
endif()
