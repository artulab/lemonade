#include <lemon/cli_parser.h>
#include <lemon/utils/path_utils.h>
#include <lemon/version.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

#define APP_NAME "lemond"
#define APP_DESC APP_NAME " - Lightweight LLM server"

namespace lemon {

CLIParser::CLIParser()
    : app_(APP_DESC) {

    app_.set_version_flag("-v,--version", (APP_NAME " version " LEMON_VERSION_STRING));

    // Positional arg: lemonade cache/data directory (optional)
    // Default to the platform-specific cache dir when not specified
    cache_dir_opt_ = app_.add_option("cache_dir", config_.cache_dir,
                    "Lemonade cache directory for downloaded/runtime data")
        ->type_name("DIR")
        ->default_val(utils::get_cache_dir());

    config_dir_opt_ = app_.add_option("config_dir", config_.config_dir,
                    "Lemonade config directory for persistent JSON state")
        ->type_name("DIR")
        ->default_val(utils::get_config_dir());

    app_.add_option("--port", config_.port, "Port number to serve on (runtime override)")
        ->type_name("PORT");

    app_.add_option("--host", config_.host, "Address to bind for connections (runtime override)")
        ->type_name("HOST");

    app_.add_flag("--broadcast,!--no-broadcast", config_.broadcast, "Enable or disable UDP broadcasting for server discovery");

    app_.add_option("--log-file", config_.log_file, "File logging mode: auto (default), disabled, enabled, or custom file path (overrides config.json)")
        ->type_name("MODE");

    app_.add_option("--log-max-size-mb", config_.log_max_file_size_mb, "Max active log file size in MB before rotation (1..2048, overrides config.json)")
        ->type_name("MB")
        ->check(CLI::Range(1, 2048));

    app_.add_option("--log-max-files", config_.log_max_files, "Max number of rotated log backup files to retain (0..100, overrides config.json)")
        ->type_name("N")
        ->check(CLI::Range(0, 100));

    app_.add_option("--watchdog-fd", config_.watchdog_fd, "Internal supervisor watchdog pipe descriptor")
        ->type_name("FD")
        ->group("");

    const std::string cluster_group = "Cluster (experimental)";
    app_.add_option("--role", cluster_role_, "Cluster role of this node")
        ->type_name("ROLE")
        ->check(CLI::IsMember({"standalone", "coordinator", "worker"}, CLI::ignore_case))
        ->capture_default_str()
        ->group(cluster_group);

    app_.add_option("--name", config_.cluster.node_name, "Node name shown in the cluster (default: hostname)")
        ->type_name("NAME")
        ->group(cluster_group);

    app_.add_option("--controller", config_.cluster.controller_url, "Coordinator base URL, e.g. http://10.0.0.1:13305 (worker only)")
        ->type_name("URL")
        ->group(cluster_group);

    app_.add_option("--join-token", config_.cluster.join_token,
                    "Token for enrolling with the coordinator (worker only; prefer LEMONADE_CLUSTER_JOIN_TOKEN)")
        ->type_name("TOKEN")
        ->group(cluster_group);
}

int CLIParser::parse(int argc, char** argv) {
    try {
        app_.parse(argc, argv);
        // Portable installs (custom cache_dir, no config_dir) keep files together.
        if (cache_dir_opt_->count() > 0 && config_dir_opt_->count() == 0) {
            config_.config_dir = config_.cache_dir;
        }
        std::transform(cluster_role_.begin(), cluster_role_.end(), cluster_role_.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (cluster_role_ == "coordinator") {
            config_.cluster.role = cluster::ClusterRole::Coordinator;
        } else if (cluster_role_ == "worker") {
            config_.cluster.role = cluster::ClusterRole::Worker;
        }
        // Read the token from the environment only for workers, so a token
        // exported in a shared shell does not break a standalone lemond.
        if (config_.cluster.role == cluster::ClusterRole::Worker && config_.cluster.join_token.empty()) {
            if (const char* env = std::getenv("LEMONADE_CLUSTER_JOIN_TOKEN")) {
                config_.cluster.join_token = env;
            }
        }
        std::string cluster_error = cluster::validate_cluster_settings(config_.cluster);
        if (!cluster_error.empty()) {
            throw CLI::ValidationError(cluster_error);
        }
        should_continue_ = true;
        exit_code_ = 0;
        return 0;  // Success, continue
    } catch (const CLI::ParseError& e) {
        // Help/version requested or parse error occurred
        // Let CLI11 handle printing and get the exit code
        exit_code_ = app_.exit(e);
        should_continue_ = false;  // Don't continue, just exit
        return exit_code_;
    }
}

} // namespace lemon
