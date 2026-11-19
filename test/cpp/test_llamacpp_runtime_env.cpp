#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>
#include <lemon/backends/llamacpp/llamacpp_runtime_env.h>

using lemon::backends::llamacpp::build_llamacpp_env;
using EnvVars = std::vector<std::pair<std::string, std::string>>;

static int passed = 0;
static int failures = 0;

static void check(bool cond, const char* desc) {
    if (cond) {
        std::printf("[PASS] %s\n", desc);
        ++passed;
    } else {
        std::printf("[FAIL] %s\n", desc);
        ++failures;
    }
}

static const std::string* find_var(const EnvVars& env, const std::string& name) {
    for (const auto& [key, value] : env) {
        if (key == name) return &value;
    }
    return nullptr;
}

static void test_no_env_for_vulkan_and_cpu() {
    check(build_llamacpp_env("vulkan", "/opt/llama/vulkan/llama-server", "").empty(),
          "vulkan sets no environment");
    check(build_llamacpp_env("cpu", "/opt/llama/cpu/llama-server", "").empty(),
          "cpu sets no environment");
}

#ifdef __linux__
// rocm-nightly is used because rocm-stable probes the GPU architecture.
static void test_rocm_library_path() {
    const std::string exe = "/opt/llama/rocm-nightly/llama-server";

    unsetenv("LD_LIBRARY_PATH");
    EnvVars env = build_llamacpp_env("rocm-nightly", exe, "");
    const std::string* lib_path = find_var(env, "LD_LIBRARY_PATH");
    check(lib_path && *lib_path == "/opt/llama/rocm-nightly",
          "rocm sets LD_LIBRARY_PATH to the executable directory");

    setenv("LD_LIBRARY_PATH", "/existing/lib", 1);
    env = build_llamacpp_env("rocm-nightly", exe, "");
    lib_path = find_var(env, "LD_LIBRARY_PATH");
    check(lib_path && *lib_path == "/opt/llama/rocm-nightly:/existing/lib",
          "rocm keeps an existing LD_LIBRARY_PATH after the executable directory");
    unsetenv("LD_LIBRARY_PATH");
}
#endif

int main() {
    test_no_env_for_vulkan_and_cpu();
#ifdef __linux__
    test_rocm_library_path();
#endif
    std::printf("\n%d passed, %d failed\n", passed, failures);
    return failures == 0 ? 0 : 1;
}
