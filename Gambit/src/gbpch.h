#pragma once

// Precompiled header of the engine library. Every engine .cpp includes it first. Keep it to standard headers
// and the most widely used Core headers; anything volatile here recompiles the whole engine.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
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
