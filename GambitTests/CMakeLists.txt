file(GLOB_RECURSE GB_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

if(NOT GB_BUILD_CLI)
    list(FILTER GB_TEST_SOURCES EXCLUDE REGEX "/src/CLI/")
endif()

add_executable(GambitTests ${GB_TEST_SOURCES})
gambit_configure_target(GambitTests)
target_include_directories(GambitTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
# Tests that regenerate checked-in reference data (GB_UPDATE_GOLDEN=1) write into the source tree. Golden-image
# mismatches leave the actual and diff images in build/test-output.
target_compile_definitions(GambitTests PRIVATE
    GB_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src"
    GB_TEST_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Golden"
    GB_TEST_OUTPUT_DIR="${PROJECT_SOURCE_DIR}/build/test-output")

# Shaders used only by tests; TestSupport registers them with the ShaderLibrary.
file(GLOB_RECURSE GB_TEST_SHADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.glsl")
gb_add_shaders(GambitTests ${GB_TEST_SHADERS})
target_link_libraries(GambitTests PRIVATE
    Gambit::Gambit
    doctest::doctest
    glm::glm
    EnTT::EnTT
    nlohmann_json::nlohmann_json
    glfw::glfw
    # lz4 before xxhash on purpose: <xxhash.h> must still resolve to xxHash (ThirdPartyTests uses XXH3).
    lz4::lz4
    xxhash::xxhash
    monocypher::monocypher
    stb::stb
    # Tests reach the Vulkan objects behind NVRHI (the validation test) and pin the headers' version.
    Vulkan::Headers)

if(GB_BUILD_CLI)
    target_link_libraries(GambitTests PRIVATE GambitCLICore)
endif()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" FILES ${GB_TEST_SOURCES})

# One CTest entry for the whole doctest executable; scripts/test.py runs it directly to get doctest filtering and a
# summary, CTest is for IDEs and `ctest` users.
add_test(NAME GambitTests COMMAND GambitTests)
