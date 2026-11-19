#pragma once

#include <string>
#include <utility>
#include <vector>

namespace lemon {
namespace backends {
namespace llamacpp {

// Environment a llama.cpp executable (llama-server, ggml-rpc-server) needs to
// load its GPU runtime for the given backend. Not pure: on Windows with
// rocm-stable it also stages the TheRock HIP runtime next to the executable.
std::vector<std::pair<std::string, std::string>> build_llamacpp_env(const std::string& llamacpp_backend,
                                                                    const std::string& executable,
                                                                    const std::string& llamacpp_device);

} // namespace llamacpp
} // namespace backends
} // namespace lemon
