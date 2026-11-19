#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <lemon/cli_parser.h>
#include <lemon/cluster/cluster_settings.h>

using lemon::CLIParser;
using lemon::ServerConfig;
using lemon::cluster::ClusterRole;
using lemon::cluster::ClusterSettings;
using lemon::cluster::validate_cluster_settings;

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

static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

static void set_env(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value) {
        setenv(name, value, 1);
    } else {
        unsetenv(name);
    }
#endif
}

struct ParseResult {
    bool ok;
    ServerConfig config;
};

static ParseResult parse(std::vector<std::string> args) {
    args.insert(args.begin(), "lemond");
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    CLIParser parser;
    parser.parse(static_cast<int>(argv.size()), argv.data());
    return {parser.should_continue(), parser.get_config()};
}

static void test_validation() {
    ClusterSettings s;
    check(s.role == ClusterRole::Standalone, "default role is standalone");
    check(validate_cluster_settings(s).empty(), "default settings are valid");

    s = {};
    s.role = ClusterRole::Coordinator;
    s.node_name = "node-a";
    check(validate_cluster_settings(s).empty(), "coordinator with a name is valid");

    s = {};
    s.role = ClusterRole::Worker;
    std::string err = validate_cluster_settings(s);
    check(contains(err, "--controller") && contains(err, "--join-token"),
          "worker without controller or token names both flags");

    s.controller_url = "http://10.0.0.1:13305";
    err = validate_cluster_settings(s);
    check(!contains(err, "--controller") && contains(err, "--join-token"),
          "worker without token names only the token");

    s.join_token = "secret";
    check(validate_cluster_settings(s).empty(), "worker with controller and token is valid");

    s.controller_url = "10.0.0.1:13305";
    check(contains(validate_cluster_settings(s), "http://"), "controller without scheme fails");

    s = {};
    s.role = ClusterRole::Coordinator;
    s.controller_url = "http://10.0.0.1:13305";
    check(contains(validate_cluster_settings(s), "--controller"), "coordinator with controller fails");

    s = {};
    s.role = ClusterRole::Coordinator;
    s.join_token = "secret";
    check(contains(validate_cluster_settings(s), "--join-token"), "coordinator with join token fails");

    s = {};
    s.node_name = "node-a";
    check(contains(validate_cluster_settings(s), "--name"), "standalone with a name fails");
}

static void test_parser() {
    set_env("LEMONADE_CLUSTER_JOIN_TOKEN", nullptr);

    auto r = parse({});
    check(r.ok && r.config.cluster.role == ClusterRole::Standalone, "no flags parses as standalone");

    r = parse({"--role", "coordinator", "--name", "node-a"});
    check(r.ok && r.config.cluster.role == ClusterRole::Coordinator &&
              r.config.cluster.node_name == "node-a",
          "--role coordinator --name parses");

    r = parse({"--role", "Worker", "--controller", "http://10.0.0.1:13305", "--join-token", "t"});
    check(r.ok && r.config.cluster.role == ClusterRole::Worker &&
              r.config.cluster.controller_url == "http://10.0.0.1:13305" &&
              r.config.cluster.join_token == "t",
          "--role is case-insensitive and worker flags parse");

    check(!parse({"--role", "leader"}).ok, "unknown role fails");
    check(!parse({"--role", "1"}).ok, "numeric role fails");
    check(!parse({"--role", "worker"}).ok, "worker without controller or token fails");
    check(!parse({"--role", "coordinator", "--controller", "http://x:1"}).ok,
          "coordinator with --controller fails");

    set_env("LEMONADE_CLUSTER_JOIN_TOKEN", "from-env");
    r = parse({"--role", "worker", "--controller", "http://10.0.0.1:13305"});
    check(r.ok && r.config.cluster.join_token == "from-env", "worker reads token from environment");

    r = parse({"--role", "worker", "--controller", "http://10.0.0.1:13305", "--join-token", "flag"});
    check(r.ok && r.config.cluster.join_token == "flag", "--join-token overrides the environment");

    r = parse({});
    check(r.ok && r.config.cluster.join_token.empty(),
          "standalone ignores the environment token");
    set_env("LEMONADE_CLUSTER_JOIN_TOKEN", nullptr);
}

int main() {
    test_validation();
    test_parser();
    std::printf("\n%d passed, %d failed\n", passed, failures);
    return failures == 0 ? 0 : 1;
}
