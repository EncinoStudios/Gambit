#pragma once

// Precompiled header of the engine library. Every engine .cpp includes it first (it is also force-included when
// GB_USE_PCH is ON). Keep it to standard headers, glm and the most widely used, stable Core headers; anything
// volatile here recompiles the whole engine.

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "Gambit/Core/Assert.h"
#include "Gambit/Core/Base.h"
#include "Gambit/Core/Log.h"
#include "Gambit/Core/Result.h"
#include "Gambit/Math/Math.h"
