add_executable(GambitRuntime src/Main.cpp)
gambit_configure_target(GambitRuntime)
target_link_libraries(GambitRuntime PRIVATE Gambit::Gambit)

if(GB_BUILD_TESTS)
    add_test(NAME GambitRuntime.Smoke COMMAND GambitRuntime)
endif()
