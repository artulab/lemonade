#pragma once

#include <string>
#include <vector>

namespace lemon {
namespace cluster {

enum class ClusterRole { Standalone, Coordinator, Worker };

struct ClusterSettings {
    ClusterRole role = ClusterRole::Standalone;
    std::string node_name;       // Empty = derive from the hostname
    std::string controller_url;  // Worker only: coordinator base URL
    std::string join_token;      // Worker only
};

// Returns an empty string when the settings are valid, otherwise a message
// naming every offending flag.
inline std::string validate_cluster_settings(const ClusterSettings& s) {
    std::vector<std::string> problems;

    switch (s.role) {
        case ClusterRole::Standalone:
            if (!s.node_name.empty()) problems.push_back("--name requires --role coordinator or worker");
            if (!s.controller_url.empty()) problems.push_back("--controller requires --role worker");
            if (!s.join_token.empty()) problems.push_back("--join-token requires --role worker");
            break;
        case ClusterRole::Coordinator:
            if (!s.controller_url.empty()) problems.push_back("--controller is only valid with --role worker");
            if (!s.join_token.empty()) problems.push_back("--join-token is only valid with --role worker");
            break;
        case ClusterRole::Worker: {
            if (s.controller_url.empty()) {
                problems.push_back("--role worker requires --controller");
            } else if (s.controller_url.rfind("http://", 0) != 0 &&
                       s.controller_url.rfind("https://", 0) != 0) {
                problems.push_back("--controller must start with http:// or https://");
            }
            if (s.join_token.empty()) {
                problems.push_back("--role worker requires --join-token or LEMONADE_CLUSTER_JOIN_TOKEN");
            }
            break;
        }
    }

    std::string message;
    for (const auto& p : problems) {
        if (!message.empty()) message += "; ";
        message += p;
    }
    return message;
}

} // namespace cluster
} // namespace lemon
