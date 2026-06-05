#pragma once

// ─── Minimal ANSI terminal color helpers ────────────────────────────────────

namespace nyla
{
namespace Term
{

// Reset
constexpr const char *kReset = "\033[0m";

// Foreground colors
constexpr const char *kDim = "\033[2m";
constexpr const char *kGreen = "\033[32m";
constexpr const char *kYellow = "\033[33m";
constexpr const char *kRed = "\033[31m";
constexpr const char *kCyan = "\033[36m";
constexpr const char *kWhite = "\033[37m";

// Bold
constexpr const char *kBold = "\033[1m";

// Background colors
constexpr const char *kBgBlue = "\033[44m";
constexpr const char *kBgCyan = "\033[46m";

} // namespace Term
} // namespace nyla
