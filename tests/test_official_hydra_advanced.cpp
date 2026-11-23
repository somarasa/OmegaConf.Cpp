#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include "omegaconf/omegaconf.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <cstdlib>

using namespace omegaconf;
namespace fs = std::filesystem;

namespace {

struct AdvancedTempDirFixture {
    fs::path temp_dir;

    AdvancedTempDirFixture() {
        auto base = fs::temp_directory_path();
        auto rand_id = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        temp_dir = base / ("omegaconf_hydra_adv_" + rand_id);
        fs::create_directories(temp_dir);
    }

    ~AdvancedTempDirFixture() {
        ConfigStore::instance().clear();
        std::error_code ec;
        fs::remove_all(temp_dir, ec);
    }

    void write_file(const std::string& rel_path, const std::string& content) {
        fs::path p = temp_dir / rel_path;
        fs::create_directories(p.parent_path());
        std::ofstream ofs(p);
        ofs << content;
    }
};

class DelegatingLogCallback : public ConfigCallback {
public:
    DelegatingLogCallback(std::string name, std::vector<std::string>& shared_log, bool throw_on_start = false)
        : name_(std::move(name)), log_(shared_log), throw_on_start_(throw_on_start) {}

    void on_compose_config(const std::shared_ptr<ConfigObjectNode>&,
                           const std::optional<std::string>&,
                           const std::vector<std::string>&) override {
        log_.push_back(name_ + ":on_compose_config");
    }

    void on_run_start(const std::shared_ptr<ConfigObjectNode>&, const std::optional<std::string>&) override {
        log_.push_back(name_ + ":on_run_start");
        if (throw_on_start_) {
            // Log warning or throw
        }
    }

    void on_run_end(const std::shared_ptr<ConfigObjectNode>&, const std::optional<std::string>&) override {
        log_.push_back(name_ + ":on_run_end");
    }

    void on_multirun_start(const std::shared_ptr<ConfigObjectNode>&, const std::optional<std::string>&) override {
        log_.push_back(name_ + ":on_multirun_start");
    }

    void on_multirun_end(const std::vector<std::shared_ptr<ConfigObjectNode>>& job_configs, const std::optional<std::string>&) override {
        log_.push_back(name_ + ":on_multirun_end:" + std::to_string(job_configs.size()));
    }

    void on_job_start(const std::shared_ptr<ConfigObjectNode>&, int job_id) override {
        log_.push_back(name_ + ":on_job_start:" + std::to_string(job_id));
    }

    void on_job_end(const std::shared_ptr<ConfigObjectNode>&, int job_id, const std::exception_ptr&) override {
        log_.push_back(name_ + ":on_job_end:" + std::to_string(job_id));
    }

private:
    std::string name_;
    std::vector<std::string>& log_;
    bool throw_on_start_;
};

} // namespace

// =========================================================================
// 1. Interpolated Defaults List
// =========================================================================

TEST_CASE("InterpolatedDefaults from self property and CLI override match Hydra", "[hydra_advanced]") {
    AdvancedTempDirFixture fixture;

    fixture.write_file("db/postgres.yaml", "driver: pg\nport: 5432");
    fixture.write_file("db/mysql.yaml", "driver: mysql\nport: 3306");
    fixture.write_file("config.yaml", R"(
defaults:
  - db: ${default_db}
  - _self_
default_db: postgres
)");

    // 1. Default resolution uses default_db=postgres
    auto node1 = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .build_node();
    REQUIRE(node1 != nullptr);
    CHECK(node1->select<std::string>("db.driver") == "pg");
    CHECK(node1->select<int>("db.port") == 5432);

    // 2. Overriding default_db via CLI or select_path updates the interpolated default group choice
    auto node2 = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .select_path("default_db", "mysql")
        .build_node();
    REQUIRE(node2 != nullptr);
    CHECK(node2->select<std::string>("db.driver") == "mysql");
    CHECK(node2->select<int>("db.port") == 3306);
}

TEST_CASE("InterpolatedDefaults referencing earlier group choices and shorthand paths match Hydra", "[hydra_advanced]") {
    AdvancedTempDirFixture fixture;

    fixture.write_file("group1/file1.yaml", "val: 10");
    fixture.write_file("group1/file2.yaml", "val: 20");
    fixture.write_file("group1_group2/file1_file2.yaml", "combined: from_file1_file2");
    fixture.write_file("group1_group2/file2_file2.yaml", "combined: from_file2_file2");
    fixture.write_file("group1/group2/file1.yaml", "nested: nested_file1");
    fixture.write_file("group1/group2/file2.yaml", "nested: nested_file2");

    fixture.write_file("config.yaml", R"(
defaults:
  - group1: file1
  - group1_group2: ${group1}_file2
  - group1/group2/${group1}
  - _self_
)");

    auto node1 = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .build_node();
    REQUIRE(node1 != nullptr);
    CHECK(node1->select<int>("group1.val") == 10);
    CHECK(node1->select<std::string>("group1_group2.combined") == "from_file1_file2");
    CHECK(node1->select<std::string>("group1.group2.nested") == "nested_file1");

    auto node2 = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .add_cli_arg("group1=file2")
        .build_node();
    REQUIRE(node2 != nullptr);
    CHECK(node2->select<int>("group1.val") == 20);
    CHECK(node2->select<std::string>("group1_group2.combined") == "from_file2_file2");
    CHECK(node2->select<std::string>("group1.group2.nested") == "nested_file2");
}

TEST_CASE("InterpolatedDefaults environment variable and circular detection match Hydra", "[hydra_advanced]") {
    AdvancedTempDirFixture fixture;

    fixture.write_file("db/postgres.yaml", "driver: pg");
    fixture.write_file("db/sqlite.yaml", "driver: sqlite");
    fixture.write_file("config.yaml", R"(
defaults:
  - db: ${oc.env:OMEGACONF_TEST_DEFAULT_DB, sqlite}
  - _self_
)");

    auto fallback_node = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .build_node();
    REQUIRE(fallback_node != nullptr);
    CHECK(fallback_node->select<std::string>("db.driver") == "sqlite");

    fixture.write_file("circular.yaml", R"(
defaults:
  - group1: ${group2}
  - group2: ${group1}
)");

    CHECK_THROWS_AS(
        OmegaConfBuilder::create()
            .set_root(fixture.temp_dir.string())
            .set_entry_file("circular.yaml")
            .build_node(),
        ConfigInterpolationException
    );
}

// =========================================================================
// 2. CLI Override Aliases
// =========================================================================

TEST_CASE("OverrideAliases key and value expansion matches official Hydra", "[hydra_advanced]") {
    auto raw = OmegaConfig::parse_yaml(R"(
hydra:
  aliases:
    "batch_size, bs": trainer.batch_size
    lr: optimizer.learning_rate
    config: config@_global_
    "verbose, v": verbose=true
    check: action=check
    seed: +trainer.seed=42
)");

    auto aliases = OverrideAliases::from_config(raw);
    CHECK_FALSE(aliases.is_empty());

    // Key aliases
    CHECK(aliases.expand("bs=32") == "trainer.batch_size=32");
    CHECK(aliases.expand("batch_size=32") == "trainer.batch_size=32");
    CHECK(aliases.expand("+bs=32") == "+trainer.batch_size=32");
    CHECK(aliases.expand("++bs=32") == "++trainer.batch_size=32");
    CHECK(aliases.expand("~bs") == "~trainer.batch_size");
    CHECK(aliases.expand("~bs=8") == "~trainer.batch_size=8");
    CHECK(aliases.expand("config=user") == "config@_global_=user");
    CHECK(aliases.expand("other=1") == "other=1");

    // Key alias without value throws ConfigSyntaxException
    CHECK_THROWS_AS(aliases.expand("bs"), ConfigSyntaxException);
    CHECK_THROWS_AS(aliases.expand("+bs"), ConfigSyntaxException);
    CHECK_THROWS_AS(aliases.expand("++bs"), ConfigSyntaxException);

    // Value aliases
    CHECK(aliases.expand("verbose") == "verbose=true");
    CHECK(aliases.expand("v") == "verbose=true");
    CHECK(aliases.expand("verbose=false") == "verbose=false");
    CHECK(aliases.expand("v=false") == "v=false");
    CHECK(aliases.expand("~v") == "~verbose=true");
    CHECK(aliases.expand("+v") == "+verbose=true");
    CHECK(aliases.expand("++v") == "++verbose=true");
    CHECK(aliases.expand("check") == "action=check");
    CHECK(aliases.expand("seed") == "+trainer.seed=42");
    CHECK(aliases.expand("++seed") == "++trainer.seed=42");
    CHECK(aliases.expand("~seed") == "~trainer.seed=42");
}

TEST_CASE("OverrideAliases validation rules match official Hydra", "[hydra_advanced]") {
    auto escaped = OmegaConfig::parse_yaml(R"(
hydra:
  aliases:
    a: \${not_interp}
)");
    auto escaped_aliases = OverrideAliases::from_config(escaped);
    CHECK(escaped_aliases.expand("a=1") == "\\${not_interp}=1");

    std::vector<std::string> invalid_yamls = {
        "hydra:\n  aliases:\n    - a\n    - b",
        "hydra:\n  aliases: ${some.ref}",
        "hydra:\n  aliases:\n    \"\": target",
        "hydra:\n  aliases:\n    \",bs\": target",
        "hydra:\n  aliases:\n    \"+bs\": target",
        "hydra:\n  aliases:\n    \"~bs\": target",
        "hydra:\n  aliases:\n    \"bs=16\": target",
        "hydra:\n  aliases:\n    \"${interp}\": target",
        "hydra:\n  aliases:\n    bs: x\n    \"batch_size, bs\": y",
        "hydra:\n  aliases:\n    a: \"\"",
        "hydra:\n  aliases:\n    a: null",
        "hydra:\n  aliases:\n    a: 42",
        "hydra:\n  aliases:\n    a: ${some.ref}",
        "hydra:\n  aliases:\n    a: +x",
        "hydra:\n  aliases:\n    a: ~x"
    };

    for (const auto& yaml : invalid_yamls) {
        auto node = OmegaConfig::parse_yaml(yaml);
        CHECK_THROWS_AS(OverrideAliases::from_config(node), ConfigSyntaxException);
    }

    CHECK_THROWS_AS(
        OverrideAliases::validate_cli_does_not_modify_aliases({"hydra.aliases.check=step"}),
        ConfigSyntaxException
    );
    CHECK_THROWS_AS(
        OverrideAliases::validate_cli_does_not_modify_aliases({"~hydra.aliases"}),
        ConfigSyntaxException
    );
}

TEST_CASE("OverrideAliases end-to-end single run, multi run, and included config guard", "[hydra_advanced]") {
    AdvancedTempDirFixture fixture;

    fixture.write_file("config.yaml", R"(
trainer:
  batch_size: 8
  lr: 0.01
verbose: false
hydra:
  aliases:
    "batch_size, bs": trainer.batch_size
    lr: trainer.lr
    "verbose, v": verbose=true
)");

    // 1. Single-run with aliases
    auto single = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .add_cli_args("bs=64", "v", "+seed=123")
        .build_node();
    REQUIRE(single != nullptr);
    CHECK(single->select<int>("trainer.batch_size") == 64);
    CHECK(single->select<bool>("verbose") == true);
    CHECK(single->select<int>("seed") == 123);

    // 2. Multi-run sweep with aliases
    auto sweeps = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .add_cli_args("-m", "bs=16,32", "v")
        .build_sweep_nodes();
    REQUIRE(sweeps.size() == 2);
    CHECK(sweeps[0]->select<int>("trainer.batch_size") == 16);
    CHECK(sweeps[0]->select<bool>("verbose") == true);
    CHECK(sweeps[1]->select<int>("trainer.batch_size") == 32);
    CHECK(sweeps[1]->select<bool>("verbose") == true);

    // 3. Struct mode error on alias includes expansion info
    fixture.write_file("bad_alias.yaml", R"(
trainer:
  batch_size: 8
hydra:
  aliases:
    bad: trainer.nonexistent
)");

    CHECK_THROWS_AS(
        OmegaConfBuilder::create()
            .set_root(fixture.temp_dir.string())
            .set_entry_file("bad_alias.yaml")
            .with_struct_mode()
            .add_cli_args("bad=10")
            .build_node(),
        ConfigStructException
    );

    // 4. Defining hydra.aliases in an included config group file is rejected
    fixture.write_file("group/with_aliases.yaml", R"(
x: 1
hydra:
  aliases:
    a: x
)");
    fixture.write_file("include_alias.yaml", R"(
defaults:
  - group: with_aliases
)");

    CHECK_THROWS_AS(
        OmegaConfBuilder::create()
            .set_root(fixture.temp_dir.string())
            .set_entry_file("include_alias.yaml")
            .build_node(),
        ConfigSyntaxException
    );
}

// =========================================================================
// 3. In-Memory ConfigStore
// =========================================================================

TEST_CASE("ConfigStore store, load, collision modes, and with_provider match Hydra", "[hydra_advanced]") {
    auto& cs = ConfigStore::instance();
    cs.clear();

    auto raw_node = std::make_shared<ConfigObjectNode>();
    raw_node->set_child("name", std::make_shared<ConfigValueNode>("test1"));
    cs.store("config", raw_node);

    auto loaded1 = cs.load("config.yaml");
    CHECK(loaded1->node()->select<std::string>("name") == "test1");
    loaded1->node()->set_child("name", std::make_shared<ConfigValueNode>("mutated"));
    CHECK(cs.load("config")->node()->select<std::string>("name") == "test1");

    // Empty group normalization
    auto root_node = std::make_shared<ConfigObjectNode>();
    root_node->set_child("k", std::make_shared<ConfigValueNode>(1));
    cs.store("root_cfg", root_node, "");
    CHECK_FALSE(cs.load("root_cfg")->group().has_value());

    // Subgroup storage and list
    auto mysql_node = std::make_shared<ConfigObjectNode>();
    mysql_node->set_child("host", std::make_shared<ConfigValueNode>("mysql.local"));
    mysql_node->set_child("port", std::make_shared<ConfigValueNode>(3306));
    cs.store("mysql", mysql_node, "db", "primary_db");

    auto pg_node = std::make_shared<ConfigObjectNode>();
    pg_node->set_child("host", std::make_shared<ConfigValueNode>("pg.local"));
    cs.store("postgres", pg_node, "db");

    CHECK(cs.has_group("db"));
    CHECK(cs.try_load("db", "mysql") != nullptr);
    CHECK(cs.try_load("db", "unknown") == nullptr);

    auto db_list = cs.list("db");
    CHECK(db_list.size() == 2);
    CHECK(db_list[0] == "mysql.yaml");
    CHECK(db_list[1] == "postgres.yaml");

    // Collision behavior
    {
        auto scoped = cs.with_provider("provider_a");
        auto val_node = std::make_shared<ConfigObjectNode>();
        val_node->set_child("v", std::make_shared<ConfigValueNode>(1));
        scoped.store("collision_test", val_node);
    }

    // replace: false throws ConfigSyntaxException
    auto col_node2 = std::make_shared<ConfigObjectNode>();
    col_node2->set_child("v", std::make_shared<ConfigValueNode>(2));
    CHECK_THROWS_AS(
        cs.store("collision_test.yaml", col_node2, "", std::nullopt, "provider_b", false),
        ConfigSyntaxException
    );
    CHECK(cs.load("collision_test")->node()->select<int>("v") == 1);
    CHECK(cs.load("collision_test")->provider() == "provider_a");

    // replace: true overwrites silently
    auto col_node4 = std::make_shared<ConfigObjectNode>();
    col_node4->set_child("v", std::make_shared<ConfigValueNode>(4));
    cs.store("collision_test", col_node4, "", std::nullopt, "provider_d", true);
    CHECK(cs.load("collision_test")->node()->select<int>("v") == 4);
    CHECK(cs.load("collision_test")->provider() == "provider_d");
}

TEST_CASE("ConfigStore end-to-end pure in-memory composition matches Hydra", "[hydra_advanced]") {
    AdvancedTempDirFixture fixture;
    auto& cs = ConfigStore::instance();
    cs.clear();

    cs.store("app_config", OmegaConfig::parse_yaml(R"(
defaults:
  - db: postgres
  - _self_
app_name: PureMemoryService
)"));

    cs.store("postgres", OmegaConfig::parse_yaml("host: pg.mem\nport: 5432"), "db");
    cs.store("mysql", OmegaConfig::parse_yaml("host: mysql.mem\nport: 3306"), "db");

    auto node = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .set_entry_file("app_config")
        .with_config_store(cs)
        .add_cli_args("db=mysql", "db.port=3307")
        .build_node();

    REQUIRE(node != nullptr);
    CHECK(node->select<std::string>("app_name") == "PureMemoryService");
    CHECK(node->select<std::string>("db.host") == "mysql.mem");
    CHECK(node->select<int>("db.port") == 3307);
}

// =========================================================================
// 4. Lifecycle Callbacks
// =========================================================================

TEST_CASE("LifecycleCallbacks forward start, reverse end, and multi-run hooks", "[hydra_advanced]") {
    AdvancedTempDirFixture fixture;
    fixture.write_file("config.yaml", "lr: 0.01");

    std::vector<std::string> shared_log;
    auto cb1 = std::make_shared<DelegatingLogCallback>("cb1", shared_log);
    auto cb2 = std::make_shared<DelegatingLogCallback>("cb2", shared_log);

    // 1. Single run
    auto builder = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .add_callback(cb1)
        .add_callback(cb2);

    auto node = builder.build_node();
    REQUIRE(node != nullptr);
    CHECK(node->select<double>("lr") == 0.01);

    std::vector<std::string> expected_single = {
        "cb1:on_run_start",
        "cb1:on_job_start:0",
        "cb2:on_run_start",
        "cb2:on_job_start:0",
        "cb1:on_compose_config",
        "cb2:on_compose_config",
        "cb2:on_job_end:0",
        "cb2:on_run_end",
        "cb1:on_job_end:0",
        "cb1:on_run_end"
    };
    CHECK(shared_log.size() == 10);
    CHECK(shared_log.front() == "cb1:on_run_start");
    CHECK(shared_log.back() == "cb1:on_run_end");

    // 2. Multi-run sweep
    shared_log.clear();
    auto sweep_cb1 = std::make_shared<DelegatingLogCallback>("cb1", shared_log);
    auto sweep_cb2 = std::make_shared<DelegatingLogCallback>("cb2", shared_log);

    auto sweeps = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .add_callback(sweep_cb1)
        .add_callback(sweep_cb2)
        .add_cli_args("-m", "lr=0.01,0.1")
        .build_sweep_nodes();

    REQUIRE(sweeps.size() == 2);
    CHECK(shared_log.front() == "cb1:on_multirun_start");
    CHECK(shared_log[1] == "cb2:on_multirun_start");
    CHECK(shared_log[shared_log.size() - 2] == "cb2:on_multirun_end:2");
    CHECK(shared_log.back() == "cb1:on_multirun_end:2");
}

// =========================================================================
// 5. CLI Inspection & Help Generator (--cfg, --info, --help)
// =========================================================================

TEST_CASE("CliInspection info aliases, cfg, and help match official Hydra", "[hydra_advanced]") {
    AdvancedTempDirFixture fixture;

    fixture.write_file("db/postgres.yaml", "driver: pg");
    fixture.write_file("db/mysql.yaml", "driver: mysql");
    fixture.write_file("config.yaml", R"(
defaults:
  - db: postgres
  - _self_
trainer:
  batch_size: 8
hydra:
  aliases:
    "batch_size, bs": trainer.batch_size
    check: action=check
)");

    // 1. --info aliases
    std::string aliases_output;
    auto info_aliases_builder = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .add_cli_args("bs=not_an_int", "--info", "aliases");
    CHECK(info_aliases_builder.try_handle_cli_inspection(aliases_output));
    CHECK(aliases_output.find("== Override aliases ==") != std::string::npos);
    CHECK(aliases_output.find("batch_size, bs -> trainer.batch_size") != std::string::npos);
    CHECK(aliases_output.find("check -> action=check") != std::string::npos);
    CHECK(aliases_output.find("Expanded overrides:") != std::string::npos);
    CHECK(aliases_output.find("bs=not_an_int -> trainer.batch_size=not_an_int") != std::string::npos);

    // 2. --info aliases when no aliases defined
    fixture.write_file("no_aliases.yaml", "x: 1");
    std::string empty_aliases_output;
    auto no_aliases_builder = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .set_entry_file("no_aliases.yaml")
        .add_cli_args("--info", "aliases");
    CHECK(no_aliases_builder.try_handle_cli_inspection(empty_aliases_output));
    CHECK(empty_aliases_output == "No override aliases defined");

    // 3. --cfg prints composed and interpolated YAML
    std::string cfg_output;
    auto cfg_builder = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .add_cli_args("bs=32", "--cfg", "job");
    CHECK(cfg_builder.try_handle_cli_inspection(cfg_output));
    CHECK(cfg_output.find("batch_size: 32") != std::string::npos);
    CHECK(cfg_output.find("driver: pg") != std::string::npos);

    // 4. --help prints discovered config groups & choices and override aliases
    std::string help_output;
    auto help_builder = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .add_cli_args("--help");
    CHECK(help_builder.try_handle_cli_inspection(help_output));
    CHECK(help_output.find("== Configuration Groups ==") != std::string::npos);
    CHECK(help_output.find("db: mysql, postgres") != std::string::npos);
    CHECK(help_output.find("== Override aliases ==") != std::string::npos);
    CHECK(help_output.find("batch_size, bs -> trainer.batch_size") != std::string::npos);
}
