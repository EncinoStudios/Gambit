# GambitCLICore holds everything except main() so that the dispatcher and the commands can be unit tested.
file(GLOB_RECURSE GB_CLI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Cli/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Cli/*.h")

add_library(GambitCLICore STATIC ${GB_CLI_SOURCES})
gambit_configure_target(GambitCLICore)
target_include_directories(GambitCLICore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(GambitCLICore PUBLIC Gambit::Gambit)

add_executable(GambitCLI src/Main.cpp)
gambit_configure_target(GambitCLI)
target_link_libraries(GambitCLI PRIVATE GambitCLICore)
set_target_properties(GambitCLI PROPERTIES OUTPUT_NAME gambit)

if(GB_BUILD_TESTS)
    # End-to-end checks of the real binary; the dispatcher itself is unit tested in GambitTests.
    string(REPLACE "." "\\." GB_VERSION_REGEX "${PROJECT_VERSION}")
    add_test(NAME Cli.Version COMMAND GambitCLI --version)
    set_tests_properties(Cli.Version PROPERTIES PASS_REGULAR_EXPRESSION "^gambit ${GB_VERSION_REGEX}\n$")
    add_test(NAME Cli.Help COMMAND GambitCLI --help)
    set_tests_properties(Cli.Help PROPERTIES PASS_REGULAR_EXPRESSION "Usage: gambit")
    add_test(NAME Cli.UnknownCommand COMMAND GambitCLI definitely-not-a-command)
    set_tests_properties(Cli.UnknownCommand PROPERTIES WILL_FAIL TRUE)
endif()
