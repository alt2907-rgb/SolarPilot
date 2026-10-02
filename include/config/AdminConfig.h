#pragma once
#if __has_include("config/LocalAdminCredentials.h")
#include "config/LocalAdminCredentials.h"
#else
namespace solarpilot::config {
inline constexpr char kAdminUser[] = "admin";
inline constexpr char kAdminPassword[] = "";
}
#endif
