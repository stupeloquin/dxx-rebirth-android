/*
 * Stub definitions for build environment variables.
 * SCons normally generates these via DXX_RBE in vers_id.cpp;
 * for the Android/CMake build we provide empty stubs.
 */
#include "dxxsconf.h"

extern const char g_descent_CPPFLAGS[];
constexpr char g_descent_CPPFLAGS[] dxx_compiler_attribute_used = "";

extern const char g_descent_CXX[];
constexpr char g_descent_CXX[] dxx_compiler_attribute_used = "clang++";

extern const char g_descent_CXXFLAGS[];
constexpr char g_descent_CXXFLAGS[] dxx_compiler_attribute_used = "";

extern const char g_descent_CXX_version[];
constexpr char g_descent_CXX_version[] dxx_compiler_attribute_used = "NDK r26";

extern const char g_descent_LINKFLAGS[];
constexpr char g_descent_LINKFLAGS[] dxx_compiler_attribute_used = "";

extern const char g_descent_git_diffstat[];
constexpr char g_descent_git_diffstat[] dxx_compiler_attribute_used = "";

extern const char g_descent_git_status[];
constexpr char g_descent_git_status[] dxx_compiler_attribute_used = "";
