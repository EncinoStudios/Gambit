add_executable(GambitRuntime src/Main.cpp)
gambit_configure_target(GambitRuntime)
target_link_libraries(GambitRuntime PRIVATE Gambit::Gambit)

if(GB_BUILD_TESTS)
    # Headless: no window, exactly ten synthetic frames of one simulation tick each.
    add_test(NAME GambitRuntime.Headless COMMAND GambitRuntime --headless --frames 10)
    set_tests_properties(GambitRuntime.Headless PROPERTIES PASS_REGULAR_EXPRESSION "ran 10 frames, 10 simulation ticks")
    add_test(NAME GambitRuntime.BadArguments COMMAND GambitRuntime --headless --frames zero)
    set_tests_properties(GambitRuntime.BadArguments PROPERTIES WILL_FAIL TRUE)

    # With a window, on a virtual display where one is available.
    find_program(GB_XVFB_RUN xvfb-run)
    if(GB_XVFB_RUN)
        add_test(NAME GambitRuntime.Window COMMAND ${GB_XVFB_RUN} -a $<TARGET_FILE:GambitRuntime> --frames 10)
        set_tests_properties(GambitRuntime.Window PROPERTIES PASS_REGULAR_EXPRESSION "ran 10 frames")
    endif()
endif()
