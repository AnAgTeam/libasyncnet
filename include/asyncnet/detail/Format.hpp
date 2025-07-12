#pragma once
#if ASYNCNET_USE_FMTLIB
# include <fmt/format.hpp>
#else 
# include <format>
#endif

namespace asyncnet::detail {
#if ASYNCNET_USE_FMTLIB
	using fmt::format;
#else
	using std::format;
#endif
}