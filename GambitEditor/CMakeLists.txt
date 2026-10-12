add_executable(GambitEditor src/Main.cpp)
gambit_configure_target(GambitEditor)
target_link_libraries(GambitEditor PRIVATE Gambit::Gambit)

if(GB_BUILD_TESTS)
    add_test(NAME GambitEditor.Smoke COMMAND GambitEditor)
endif()
