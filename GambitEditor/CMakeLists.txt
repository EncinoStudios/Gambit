add_executable(GambitEditor src/Main.cpp)
gambit_configure_target(GambitEditor)
target_link_libraries(GambitEditor PRIVATE Gambit::Gambit)

if(GB_BUILD_TESTS)
    add_test(NAME GambitEditor.Headless COMMAND GambitEditor --headless --frames 10)
    set_tests_properties(GambitEditor.Headless PROPERTIES PASS_REGULAR_EXPRESSION "ran 10 frames, 10 simulation ticks")

    find_program(GB_XVFB_RUN xvfb-run)
    if(GB_XVFB_RUN)
        add_test(NAME GambitEditor.Window COMMAND ${GB_XVFB_RUN} -a $<TARGET_FILE:GambitEditor> --frames 10)
        set_tests_properties(GambitEditor.Window PROPERTIES PASS_REGULAR_EXPRESSION "ran 10 frames")
    endif()
endif()
