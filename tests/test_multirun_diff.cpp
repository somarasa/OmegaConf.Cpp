#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include <omegaconf/omegaconf.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace omegaconf;

namespace {

struct TempDirFixture {
    fs::path temp_dir;

    TempDirFixture() {
        temp_dir = fs::temp_directory_path() / ("omegaconf_multirun_test_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
        fs::create_directories(temp_dir);
    }

    ~TempDirFixture() {
        std::error_code ec;
        fs::remove_all(temp_dir, ec);
    }

    void write_file(const fs::path& rel_path, std::string_view content) {
        fs::path p = temp_dir / rel_path;
        fs::create_directories(p.parent_path());
        std::ofstream ofs(p, std::ios::binary);
        ofs << content;
    }
};

} // namespace

TEST_CASE("MultiRun should generate Cartesian combinations", "[multirun]") {
    TempDirFixture fixture;

    // 2 db choices x 3 worker choices = 6 runs!
    std::vector<std::string> cli_args = {
        "-m",
        "db=sqlite,postgres",
        "workers=2,4,8",
        "log_level=Info"
    };

    auto [non_sweep, matrix, keys_order] = SweepEngine::parse_sweep_args(cli_args);
    (void)keys_order;
    REQUIRE(matrix.find("db") != matrix.end());
    CHECK_THAT(matrix["db"], Catch::Matchers::Equals(std::vector<std::string>{"sqlite", "postgres"}));

    REQUIRE(matrix.find("workers") != matrix.end());
    CHECK_THAT(matrix["workers"], Catch::Matchers::Equals(std::vector<std::string>{"2", "4", "8"}));

    REQUIRE(non_sweep.size() == 1);
    CHECK(non_sweep[0] == "log_level=Info");

    auto combinations = SweepEngine::generate_cartesian_product(matrix);
    CHECK(combinations.size() == 6);

    // Verify with builder
    fixture.write_file("config.yaml", R"(
app_name: MatrixApp
db: sqlite
workers: 1
log_level: Debug
)");

    auto sweep_nodes = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .add_cli_args(cli_args)
        .build_sweep_nodes();

    CHECK(sweep_nodes.size() == 6);
}

TEST_CASE("ConfigDiffer should identify additions, removals, and modifications", "[diff]") {
    auto base_node = std::make_shared<ConfigObjectNode>();
    base_node->set_child("host", std::make_shared<ConfigValueNode>("localhost"));
    base_node->set_child("port", std::make_shared<ConfigValueNode>(5432LL));
    base_node->set_child("old_flag", std::make_shared<ConfigValueNode>(true));

    auto target_node = std::make_shared<ConfigObjectNode>();
    target_node->set_child("host", std::make_shared<ConfigValueNode>("prod-db.internal")); // modified
    target_node->set_child("port", std::make_shared<ConfigValueNode>(5432LL)); // unchanged
    target_node->set_child("ssl", std::make_shared<ConfigValueNode>(true)); // added
    // old_flag removed

    auto diff = ConfigDiffer::compare(base_node, target_node);
    CHECK(diff.has_changes());
    CHECK(diff.entries().size() == 3);

    std::string report = diff.to_string();
    CHECK(report.find("~ host: \"localhost\" -> \"prod-db.internal\"") != std::string::npos);
    CHECK(report.find("+ ssl: True") != std::string::npos);
    CHECK(report.find("- old_flag: True") != std::string::npos);
}
