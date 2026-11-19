#include "lemon/backends/llamacpp/llamacpp_runtime_env.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include "lemon/backends/backend_utils.h"
#include "lemon/system_info.h"
#include "lemon/utils/aixlog.hpp"
#include "lemon/utils/path_utils.h"
#ifdef __APPLE__
#include <pwd.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace lemon::utils;

namespace lemon {
namespace backends {
namespace llamacpp {

static bool is_llamacpp_rocm_backend(const std::string& backend) {
    return backend == "rocm-stable" || backend == "rocm-nightly";
}

static bool is_llamacpp_cuda_backend(const std::string& backend) {
    return backend == "cuda";
}

std::vector<std::pair<std::string, std::string>> build_llamacpp_env(const std::string& llamacpp_backend,
                                                                    const std::string& executable,
                                                                    const std::string& llamacpp_device) {
    // For ROCm on Linux, set LD_LIBRARY_PATH to include the ROCm library directory
    std::vector<std::pair<std::string, std::string>> env_vars;
#ifndef _WIN32
    if (is_llamacpp_rocm_backend(llamacpp_backend)) {
        // Get the directory containing the executable (where ROCm .so files are)
        fs::path exe_dir = fs::path(executable).parent_path();
        std::string lib_path = exe_dir.string();

        if (llamacpp_backend == "rocm-stable") {
            std::string rocm_arch = SystemInfo::get_rocm_arch();
            if (!rocm_arch.empty()) {
                std::string therock_dirs = BackendUtils::join_runtime_dirs(
                    BackendUtils::get_therock_lib_paths(rocm_arch));
                if (!therock_dirs.empty()) {
                    lib_path = therock_dirs + ":" + lib_path;
                }
            }
        }

        // Preserve existing LD_LIBRARY_PATH if it exists
        const char* existing_ld_path = std::getenv("LD_LIBRARY_PATH");
        if (existing_ld_path && strlen(existing_ld_path) > 0) {
            lib_path = lib_path + ":" + std::string(existing_ld_path);
        }

        env_vars.push_back({"LD_LIBRARY_PATH", lib_path});
        LOG(DEBUG, "LlamaCpp") << "Setting LD_LIBRARY_PATH=" << lib_path << std::endl;
    } else if (is_llamacpp_cuda_backend(llamacpp_backend)) {
        // The llama.cpp-builds Linux tarballs ship the bundled CUDA runtime
        // (libcudart.so, libcublas.so, etc.) alongside llama-server, so add the
        // executable's directory to LD_LIBRARY_PATH like we do for ROCm.
        fs::path exe_dir = fs::path(executable).parent_path();
        std::string lib_path = exe_dir.string();

        const char* existing_ld_path = std::getenv("LD_LIBRARY_PATH");
        if (existing_ld_path && strlen(existing_ld_path) > 0) {
            lib_path = lib_path + ":" + std::string(existing_ld_path);
        }

        env_vars.push_back({"LD_LIBRARY_PATH", lib_path});
        LOG(DEBUG, "LlamaCpp") << "Setting LD_LIBRARY_PATH=" << lib_path << std::endl;
    }
#else
    // For ROCm on Windows with gfx1151, set OCL_SET_SVMSIZE
    // This is a patch to enable loading larger models
    if (is_llamacpp_rocm_backend(llamacpp_backend)) {
        std::string new_path;

        if (llamacpp_backend == "rocm-stable") {
            std::string rocm_arch = SystemInfo::get_rocm_arch();
            if (!rocm_arch.empty()) {
                // Prepend ALL TheRock runtime dirs (not just _rocm_sdk_core/bin) so
                // HIP + BLAS DLLs resolve; _rocm_sdk_core/bin alone → STATUS_DLL_NOT_FOUND.
                std::string therock_dirs = BackendUtils::join_runtime_dirs(
                    BackendUtils::get_therock_lib_paths(rocm_arch));
                if (!therock_dirs.empty()) {
                    new_path = therock_dirs;
                }
            }
        }

        if (!new_path.empty()) {
            const char* existing_path = std::getenv("PATH");
            if (existing_path && strlen(existing_path) > 0) {
                new_path += ";" + std::string(existing_path);
            }
            env_vars.push_back({"PATH", new_path});
        }

        // Windows DLL search order checks System32 BEFORE PATH, so a stale
        // System32 amdhip64_7.dll shadows the TheRock runtime on PATH. Copying
        // to the exe directory overrides both.
        if (llamacpp_backend == "rocm-stable") {
            std::string rocm_arch = SystemInfo::get_rocm_arch();
            if (!rocm_arch.empty()) {
                BackendUtils::stage_therock_hip_runtime(
                    rocm_arch, fs::path(executable).parent_path());
            }
        }

        std::string arch = lemon::SystemInfo::get_rocm_arch();
        if ((arch == "gfx1151") || (arch == "gfx1152")){
            env_vars.push_back({"OCL_SET_SVM_SIZE", "262144"});
            LOG(DEBUG, "LlamaCpp") << "Setting OCL_SET_SVM_SIZE=262144 for gfx1151/gfx1152 (enables loading larger models)" << std::endl;
        }
    } else if (is_llamacpp_cuda_backend(llamacpp_backend)) {
        // CUDA Windows builds bundle cudart64_*.dll, cublas64_*.dll, etc. next to
        // llama-server.exe. Prepend the executable directory to PATH so the loader
        // resolves them before any system-wide CUDA install.
        fs::path exe_dir = fs::absolute(fs::path(executable)).parent_path();
        std::string new_path = path_to_utf8(exe_dir);

        const char* existing_path = std::getenv("PATH");
        if (existing_path && strlen(existing_path) > 0) {
            new_path += ";" + std::string(existing_path);
        }
        env_vars.push_back({"PATH", new_path});
        LOG(DEBUG, "LlamaCpp") << "Prepending CUDA exe dir to PATH: " << path_to_utf8(exe_dir) << std::endl;
    }
#endif

    if (is_llamacpp_cuda_backend(llamacpp_backend)) {
        std::string existing_llama_device = utils::get_environment_variable_utf8("LLAMA_ARG_DEVICE");
        const bool has_llama_device_override = !existing_llama_device.empty();

        bool skip_visible_devices = false;
        if (!llamacpp_device.empty()) {
            LOG(INFO, "LlamaCpp")
                << "Using explicit llama.cpp CUDA device selection: " << llamacpp_device
                << std::endl;
            skip_visible_devices = true;
        } else if (has_llama_device_override) {
            LOG(INFO, "LlamaCpp")
                << "Respecting existing LLAMA_ARG_DEVICE=" << existing_llama_device
                << std::endl;
            skip_visible_devices = true;
        }
        BackendUtils::apply_cuda_env_vars(env_vars, "LlamaCpp", skip_visible_devices);
    }

#ifdef __APPLE__
    // Forward GGML_METAL_NO_RESIDENCY to llama-server if set in the parent
    // environment. Metal residency sets crash on paravirtualized GPUs (e.g.
    // GitHub Actions macOS runners with MTLGPUFamilyApple5).
    const char* no_residency = std::getenv("GGML_METAL_NO_RESIDENCY");
    if (no_residency) {
        env_vars.push_back({"GGML_METAL_NO_RESIDENCY", no_residency});
        LOG(DEBUG, "LlamaCpp") << "Forwarding GGML_METAL_NO_RESIDENCY=" << no_residency << std::endl;
    }

    // Ensure HOME is set in the child. llama.cpp b8884+ (libllama-common's
    // fs_get_cache_directory / hf_cache::migrate_old_cache_to_hf_cache)
    // calls getenv("HOME") during CLI arg parsing and passes the result
    // straight into std::string without a NULL check, segfaulting when
    // HOME is unset. LaunchDaemons installed at /Library/LaunchDaemons/
    // get a minimal env from launchd and do not inherit HOME, so llama-server
    // crashes before the model ever loads. Terminal/sudo spawns preserve
    // HOME and do not hit this.
    //
    // Upstream fix in flight: https://github.com/ggml-org/llama.cpp/pull/22263
    // Once that PR merges and lemonade's pinned llama.cpp version (in
    // src/cpp/resources/backend_versions.json) includes it, this HOME
    // fallback can be deleted.
    const char* home = std::getenv("HOME");
    if (!home || home[0] == '\0') {
        struct passwd* pw = getpwuid(getuid());
        std::string fallback_home = (pw && pw->pw_dir) ? pw->pw_dir : "/var/root";
        env_vars.push_back({"HOME", fallback_home});
        LOG(DEBUG, "LlamaCpp") << "Parent HOME unset; setting child HOME=" << fallback_home << std::endl;
    }
#endif

    return env_vars;
}

} // namespace llamacpp
} // namespace backends
} // namespace lemon
