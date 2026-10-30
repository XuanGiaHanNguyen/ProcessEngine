// Integration tests: a real pf::server::Server on a free local port, driven over HTTP and
// WebSocket (PROJECT_PLAN.md milestone 7 acceptance).
#include "server.hpp"

#include "pf/generator.hpp"
#include "pf/json_io.hpp"
#include "pf/validator.hpp"

#include <atomic>
#include <thread>

#include <gtest/gtest.h>

#include "logging.hpp"
#include "test_client.hpp"

namespace pf::server {
namespace {

using Json = nlohmann::json;
using namespace test;

std::uint16_t freePort() {
  asio::io_context io;
  asio::ip::tcp::acceptor acceptor(io, {asio::ip::make_address("127.0.0.1"), 0});
  return acceptor.local_endpoint().port();
}

/// A fake clock the tests can move forward, for session expiry.
std::atomic<std::int64_t> g_clock_offset_s{0};

class ServerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    Log::setLevel(Log::Level::Off);
    g_clock_offset_s = 0;
    Config config;
    config.bind = "127.0.0.1";
    config.port = freePort();
    config.http_threads = 4;
    config.solver_threads = 3;
    config.max_sessions = 3;
    config.session_ttl = std::chrono::seconds(60);
    config.max_body_bytes = std::size_t{64} * 1024;
    config.instances_dir = PF_INSTANCES_DIR;
    config.job_limits.max_time_limit_ms = 30'000;
    config.clock = [] {
      return std::chrono::steady_clock::now() + std::chrono::seconds(g_clock_offset_s.load());
    };
    port_ = config.port;
    server_ = std::make_unique<Server>(config);
    server_->start();
  }
  void TearDown() override { server_->stop(); }

  /// Polls a job until it is no longer queued/running.
  Json waitForJob(const std::string& id, int timeout_ms = 30'000) const {
    for (int waited = 0; waited < timeout_ms; waited += 20) {
      Json job = get(port_, "/api/solve/" + id).json();
      if (job["status"] == "done" || job["status"] == "cancelled") return job;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    ADD_FAILURE() << "job " << id << " did not finish";
    return {};
  }

  Json sampleInstance(const std::string& id) const {
    return get(port_, "/api/instances/samples/" + id).json();
  }

  std::uint16_t port_ = 0;
  std::unique_ptr<Server> server_;
};

// --- Basics ----------------------------------------------------------------------------------

TEST_F(ServerTest, HealthAndSolverList) {
  EXPECT_EQ(get(port_, "/api/health").json()["status"], "ok");
  const Json solvers = get(port_, "/api/solvers").json();
  ASSERT_TRUE(solvers.is_array());
  std::set<std::string> names;
  for (const Json& s : solvers) {
    names.insert(s["name"]);
    EXPECT_TRUE(s.contains("description"));
    EXPECT_TRUE(s["exact"].is_boolean());
    EXPECT_TRUE(s["options"].is_array());
  }
  for (const char* n : {"greedy", "dp", "branch_and_bound", "beam_search", "lp_relaxation"}) {
    EXPECT_TRUE(names.contains(n)) << n;
  }
}

TEST_F(ServerTest, FactoryLevels) {
  const Json levels = get(port_, "/api/factory/levels").json();
  ASSERT_EQ(levels.size(), 3U);
  EXPECT_EQ(levels[0]["id"], "first-stitch");
  const Json level = get(port_, "/api/factory/levels/first-stitch").json();
  EXPECT_EQ(level["format"], "processforge-factory/1");
  EXPECT_EQ(level["slots"].size(), 5U);
  EXPECT_EQ(get(port_, "/api/factory/levels/nope").status, 404);
  EXPECT_EQ(get(port_, "/api/factory/solvers").json().size(), 3U);
  // Factory levels are not scheduling samples.
  for (const Json& s : get(port_, "/api/instances/samples").json()) {
    EXPECT_NE(s["id"], "1-first-stitch");
  }
}

TEST_F(ServerTest, FactoryValidateAndSimulate) {
  const Json level = get(port_, "/api/factory/levels/first-stitch").json();
  const Json& initial = level["reference_designs"][0];

  const Json ok = post(port_, "/api/factory/validate",
                       {{"level", "first-stitch"}, {"design", initial["design"]}})
                      .json();
  EXPECT_TRUE(ok["valid"]);
  EXPECT_EQ(ok["build"], 2250000);
  Json bad_design = initial["design"];
  bad_design["orders"]["cotton-supply"] = 999;
  const Json bad =
      post(port_, "/api/factory/validate", {{"level", "first-stitch"}, {"design", bad_design}})
          .json();
  EXPECT_FALSE(bad["valid"]);
  EXPECT_EQ(bad["errors"][0]["code"], "order_out_of_range");

  const auto sim = post(port_, "/api/factory/simulate",
                        {{"level", "first-stitch"}, {"design", initial["design"]}});
  ASSERT_EQ(sim.status, 200);
  const Json result = sim.json()["result"];
  EXPECT_EQ(result["profit"], initial["profit"]);
  EXPECT_EQ(result["hours"].size(), 24U);
  EXPECT_EQ(result["hours"][6]["sold"]["stuffed-animal"], 2);
  EXPECT_EQ(result["hours"][1]["machines"][0]["status"], "busy");

  const auto rejected =
      post(port_, "/api/factory/simulate", {{"level", "first-stitch"}, {"design", bad_design}});
  EXPECT_EQ(rejected.status, 422);
  EXPECT_EQ(rejected.json()["error"], "invalid_design");
  EXPECT_EQ(post(port_, "/api/factory/simulate", {{"level", "nope"}}).status, 404);
  EXPECT_EQ(post(port_, "/api/factory/simulate", {{"design", Json::object()}}).status, 400);
}

TEST_F(ServerTest, FactoryHint) {
  const Json level = get(port_, "/api/factory/levels/first-stitch").json();
  const Json& initial = level["reference_designs"][0];
  const auto res =
      post(port_, "/api/factory/hint", {{"level", "first-stitch"}, {"design", initial["design"]}});
  ASSERT_EQ(res.status, 200);
  const Json hint = res.json();
  ASSERT_TRUE(hint["found"]);
  EXPECT_GT(hint["gain"].get<std::int64_t>(), 0);
  EXPECT_FALSE(hint["description"].get<std::string>().empty());
  // The hinted design really earns what the hint says.
  const Json sim =
      post(port_, "/api/factory/simulate", {{"level", "first-stitch"}, {"design", hint["design"]}})
          .json();
  EXPECT_EQ(sim["result"]["profit"], hint["profit"]);

  Json bad_design = initial["design"];
  bad_design["orders"]["cotton-supply"] = 999;
  EXPECT_EQ(
      post(port_, "/api/factory/hint", {{"level", "first-stitch"}, {"design", bad_design}}).status,
      422);
}

TEST_F(ServerTest, FactorySolve) {
  const auto res = post(
      port_, "/api/factory/solve",
      {{"level", "first-stitch"}, {"solvers", {"greedy", "local_search"}}, {"timeLimitMs", 300}});
  ASSERT_EQ(res.status, 200);
  const Json body = res.json();
  ASSERT_EQ(body["results"].size(), 2U);
  for (const Json& r : body["results"]) {
    EXPECT_TRUE(r["valid"]);
    EXPECT_GT(r["profit"].get<std::int64_t>(), 810000);
    EXPECT_LE(r["profit"].get<std::int64_t>(), body["bound"].get<std::int64_t>());
    // The returned design scores the same when simulated.
    const Json sim =
        post(port_, "/api/factory/simulate", {{"level", "first-stitch"}, {"design", r["design"]}})
            .json();
    EXPECT_EQ(sim["result"]["profit"], r["profit"]);
  }
  EXPECT_EQ(
      post(port_, "/api/factory/solve", {{"level", "first-stitch"}, {"solvers", {"nope"}}}).status,
      400);
  EXPECT_EQ(post(port_, "/api/factory/solve",
                 {{"level", "first-stitch"}, {"solvers", {"greedy"}}, {"timeLimitMs", -1}})
                .status,
            400);
}

TEST_F(ServerTest, SamplesValidateAndGenerate) {
  const Json samples = get(port_, "/api/instances/samples").json();
  EXPECT_GE(samples.size(), 6U);
  const Json sawmill = sampleInstance("sawmill");
  EXPECT_EQ(sawmill["name"], "Sawmill");
  EXPECT_EQ(get(port_, "/api/instances/samples/nope").status, 404);

  const Json ok = post(port_, "/api/instances/validate", sawmill).json();
  EXPECT_TRUE(ok["valid"].get<bool>());
  Json broken = sawmill;
  broken["workers"] = 0;
  const Json bad = post(port_, "/api/instances/validate", broken).json();
  EXPECT_FALSE(bad["valid"].get<bool>());
  EXPECT_FALSE(bad["errors"].empty());

  const HttpResponse gen =
      post(port_, "/api/instances/generate", {{"size", "medium"}, {"seed", 3}});
  ASSERT_EQ(gen.status, 200);
  EXPECT_TRUE(json::parseInstance(gen.json()).ok());
  EXPECT_EQ(post(port_, "/api/instances/generate", {{"size", "huge"}}).status, 400);
}

TEST_F(ServerTest, CorsHeadersIncludingPreflight) {
  const HttpResponse pre =
      request(port_, "OPTIONS", "/api/solve", "",
              {{"Origin", "http://localhost:4200"}, {"Access-Control-Request-Method", "POST"}});
  EXPECT_EQ(pre.status, 204);
  EXPECT_EQ(pre.headers.at("access-control-allow-origin"), "*");
  EXPECT_NE(pre.headers.at("access-control-allow-methods").find("POST"), std::string::npos);
  EXPECT_EQ(get(port_, "/api/health").headers.at("access-control-allow-origin"), "*");
}

// --- Solving ---------------------------------------------------------------------------------

TEST_F(ServerTest, SolveReturnsValidatedSolutions) {
  const Json instance = sampleInstance("factory");
  const HttpResponse submit = post(port_, "/api/solve",
                                   {{"instance", instance},
                                    {"solvers", {"greedy", "branch_and_bound", "beam_search"}},
                                    {"options", {{"timeLimitMs", 5000}, {"seed", 7}}}});
  ASSERT_EQ(submit.status, 202);
  const std::string id = submit.json()["jobId"];
  const Json job = waitForJob(id);
  EXPECT_EQ(job["status"], "done");
  EXPECT_EQ(job["timeLimitMs"], 5000);
  const Instance inst = *json::parseInstance(instance).value;
  for (const Json& r : job["results"]) {
    ASSERT_EQ(r["status"], "done") << r["solver"];
    const Json& sol = r["solution"];
    ASSERT_TRUE(sol["valid"].get<bool>()) << r["solver"];
    // Independently re-validate what the API returned.
    const auto schedule = json::parseSchedule(sol["runs"], inst);
    ASSERT_TRUE(schedule.ok());
    EXPECT_EQ(validate(inst, *schedule.value).earnings, sol["earnings"].get<Quantity>());
    if (r["solver"] == "branch_and_bound") {
      EXPECT_TRUE(sol["proven_optimal"].get<bool>());
      EXPECT_EQ(sol["earnings"], 45);
      EXPECT_GT(sol["stats"]["nodes"].get<std::int64_t>(), 0);
    }
  }
  EXPECT_EQ(get(port_, "/api/solve/does-not-exist").status, 404);
}

TEST_F(ServerTest, CancelLongSolve) {
  // A large instance that no exact solver can finish within the test.
  const Json instance = json::toJson(generate(GeneratorParams::large(1)).instance);
  const std::string id = post(port_, "/api/solve",
                              {{"instance", instance},
                               {"solvers", {"branch_and_bound", "dp"}},
                               {"options", {{"timeLimitMs", 30'000}}}})
                             .json()["jobId"];
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  const auto start = std::chrono::steady_clock::now();
  EXPECT_EQ(request(port_, "DELETE", "/api/solve/" + id).status, 200);
  const Json job = waitForJob(id, 5000);
  const double ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  EXPECT_EQ(job["status"], "cancelled");
  EXPECT_LT(ms, 3000) << "cancel took too long";
  for (const Json& r : job["results"]) {
    if (r["solution"].is_null()) continue;
    EXPECT_FALSE(r["solution"]["proven_optimal"].get<bool>());
  }
}

TEST_F(ServerTest, TimeLimitIsClamped) {
  const std::string id = post(port_, "/api/solve",
                              {{"instance", sampleInstance("sawmill")},
                               {"solvers", {"greedy"}},
                               {"options", {{"timeLimitMs", 1e9}}}})
                             .json()["jobId"];
  EXPECT_EQ(waitForJob(id)["timeLimitMs"], 30'000);
}

TEST_F(ServerTest, InvalidInputGivesClear4xx) {
  auto expectError = [&](const HttpResponse& r, int status, const std::string& error) {
    EXPECT_EQ(r.status, status) << r.body;
    const Json body = r.json();
    EXPECT_EQ(body["error"], error) << r.body;
    EXPECT_TRUE(body["reason"].is_string() && !body["reason"].get<std::string>().empty());
  };
  expectError(request(port_, "POST", "/api/solve", "{not json"), 400, "invalid_json");
  expectError(post(port_, "/api/solve", {{"solvers", {"greedy"}}}), 400, "invalid_request");
  expectError(
      post(port_, "/api/solve", {{"instance", {{"resources", 3}}}, {"solvers", {"greedy"}}}), 400,
      "invalid_instance");
  const Json sawmill = sampleInstance("sawmill");
  expectError(post(port_, "/api/solve", {{"instance", sawmill}, {"solvers", {"magic"}}}), 400,
              "unknown_solver");
  expectError(post(port_, "/api/solve", {{"instance", sawmill}, {"solvers", Json::array()}}), 400,
              "invalid_solvers");
  expectError(
      post(port_, "/api/solve",
           {{"instance", sawmill}, {"solvers", {"greedy"}}, {"options", {{"timeLimit", 5}}}}),
      400, "invalid_options");
  expectError(request(port_, "POST", "/api/solve", std::string(std::size_t{100} * 1024, ' ')), 413,
              "payload_too_large");
}

TEST_F(ServerTest, WebSocketStreamsIntermediateBestSoFar) {
  const Json instance = json::toJson(generate(GeneratorParams::medium(8)).instance);
  const std::string id =
      post(port_, "/api/solve",
           {{"instance", instance},
            {"solvers", {"simulated_annealing", "beam_search", "branch_and_bound"}},
            {"options", {{"timeLimitMs", 2000}}}})
          .json()["jobId"];
  const auto messages =
      readWebSocket(port_, "/ws/solve/" + id, [](const Json& m) { return m["event"] == "done"; });
  ASSERT_FALSE(messages.empty());
  EXPECT_EQ(messages.back()["event"], "done");
  int improved = 0;
  std::int64_t expected_seq = 0;
  for (const Json& m : messages) {
    EXPECT_EQ(m["seq"], expected_seq++) << "events must arrive in order without gaps";
    for (const char* k : {"solver", "elapsedMs", "bestEarnings", "nodes", "event"}) {
      EXPECT_TRUE(m.contains(k)) << k;
    }
    if (m["event"] == "improved") ++improved;
  }
  EXPECT_GE(improved, 2) << "need at least two intermediate best-so-far updates";
  // Connecting after the job finished replays the whole stream.
  const auto replay =
      readWebSocket(port_, "/ws/solve/" + id, [](const Json& m) { return m["event"] == "done"; });
  EXPECT_EQ(replay.size(), messages.size());
}

// --- Play mode -------------------------------------------------------------------------------

TEST_F(ServerTest, PlaySessionFullFlow) {
  const HttpResponse created = post(port_, "/api/sessions", {{"instanceId", "iron_chain"}});
  ASSERT_EQ(created.status, 201);
  const Json view = created.json();
  const std::string id = view["sessionId"];
  EXPECT_EQ(view["state"]["tick"], 0);
  EXPECT_EQ(view["instance"]["name"], "Iron chain");
  EXPECT_FALSE(view["legalActions"].empty());
  const std::string base = "/api/sessions/" + id;

  // Legal moves: smelt, smelt at tick 0.
  for (int i = 0; i < 2; ++i) {
    const HttpResponse r =
        post(port_, base + "/actions", {{"type", "start_run"}, {"processId", "smelt"}});
    ASSERT_EQ(r.status, 200) << r.body;
  }
  // Illegal: forge needs iron (none yet) -> 409 with a reason, state unchanged.
  const Json before = get(port_, base).json()["state"];
  const HttpResponse illegal =
      post(port_, base + "/actions", {{"type", "start_run"}, {"processId", "forge"}});
  EXPECT_EQ(illegal.status, 409);
  EXPECT_EQ(illegal.json()["error"], "no_free_worker");
  EXPECT_FALSE(illegal.json()["reason"].get<std::string>().empty());
  EXPECT_EQ(get(port_, base).json()["state"], before);
  // The disabled-button reasons are available without trying.
  const Json status_view = get(port_, base).json();
  for (const Json& p : status_view["processStatus"]) {
    EXPECT_FALSE(p["legal"].get<bool>());
    EXPECT_TRUE(p["reason"].is_string());
  }
  EXPECT_EQ(post(port_, base + "/actions", {{"type", "teleport"}}).status, 400);

  // Undo / redo.
  ASSERT_EQ(post(port_, base + "/undo").status, 200);
  EXPECT_EQ(get(port_, base).json()["state"]["history"].size(), 1U);
  ASSERT_EQ(post(port_, base + "/redo").status, 200);
  EXPECT_EQ(get(port_, base).json()["state"], before);
  EXPECT_EQ(post(port_, base + "/redo").json()["error"], "nothing_to_redo");

  // Continue to the optimum: advance to 2, forge + smelt.
  ASSERT_EQ(post(port_, base + "/actions", {{"type", "advance_to"}, {"tick", 2}}).status, 200);
  ASSERT_EQ(post(port_, base + "/actions", {{"type", "start_run"}, {"processId", "forge"}}).status,
            200);
  ASSERT_EQ(post(port_, base + "/actions", {{"type", "start_run"}, {"processId", "smelt"}}).status,
            200);

  const Json hint = post(port_, base + "/hint").json();
  EXPECT_TRUE(hint.contains("suggestedAction"));
  EXPECT_EQ(hint["projectedEarnings"], 13);

  const HttpResponse finished = post(port_, base + "/finish");
  ASSERT_EQ(finished.status, 200);
  const Json fin = finished.json();
  EXPECT_TRUE(fin["validated"].get<bool>());
  EXPECT_EQ(fin["solution"]["earnings"], 13);
  // The finished score equals the validator's score for the exported schedule.
  const Instance inst = *json::parseInstance(sampleInstance("iron_chain")).value;
  const auto schedule = json::parseSchedule(fin["solution"]["runs"], inst);
  ASSERT_TRUE(schedule.ok());
  EXPECT_EQ(validate(inst, *schedule.value).earnings, 13);
  EXPECT_TRUE(fin["state"]["finished"].get<bool>());
  EXPECT_EQ(post(port_, base + "/actions", {{"type", "wait"}}).status, 409);
}

TEST_F(ServerTest, CompareAndReplay) {
  const std::string id =
      post(port_, "/api/sessions", {{"instanceId", "pottery"}}).json()["sessionId"];
  const std::string base = "/api/sessions/" + id;
  const Json cmp =
      post(port_, base + "/compare",
           {{"solvers", {"greedy", "branch_and_bound"}}, {"options", {{"timeLimitMs", 3000}}}})
          .json();
  EXPECT_EQ(cmp["playerEarnings"], 15);  // untouched start inventory + start money
  const Json job = waitForJob(cmp["jobId"]);
  Json best;
  for (const Json& r : job["results"]) {
    if (r["solver"] == "branch_and_bound") best = r["solution"];
  }
  ASSERT_FALSE(best.is_null());
  EXPECT_EQ(best["earnings"], 27);

  // Replay the solver's schedule step by step: load it, redo until done, finish.
  const HttpResponse loaded = post(port_, base + "/load", {{"schedule", best["runs"]}});
  ASSERT_EQ(loaded.status, 200) << loaded.body;
  int steps = 0;
  while (get(port_, base).json()["state"]["redo_available"].get<int>() > 0) {
    ASSERT_EQ(post(port_, base + "/redo").status, 200);
    ASSERT_LT(++steps, 100);
  }
  EXPECT_EQ(post(port_, base + "/finish").json()["solution"]["earnings"], 27);

  EXPECT_EQ(
      post(port_, base + "/load", {{"schedule", {{{"process_id", "kiln"}, {"start_tick", 99}}}}})
          .status,
      422);
  ASSERT_EQ(post(port_, base + "/restart").status, 200);
  EXPECT_EQ(get(port_, base).json()["state"]["tick"], 0);
}

TEST_F(ServerTest, SessionsExpireAndCapIsEnforced) {
  std::vector<std::string> ids;
  for (int i = 0; i < 3; ++i) {
    const HttpResponse r = post(port_, "/api/sessions", {{"instanceId", "sawmill"}});
    ASSERT_EQ(r.status, 201);
    ids.push_back(r.json()["sessionId"]);
  }
  const HttpResponse full = post(port_, "/api/sessions", {{"instanceId", "sawmill"}});
  EXPECT_EQ(full.status, 503);
  EXPECT_EQ(full.json()["error"], "session_limit");

  // Keep one session active, let the others go idle past the TTL.
  g_clock_offset_s = 40;
  EXPECT_EQ(get(port_, "/api/sessions/" + ids[0]).status, 200);
  g_clock_offset_s = 90;  // ids[1], ids[2] idle for 90 s > 60 s; ids[0] for 50 s
  EXPECT_EQ(get(port_, "/api/sessions/" + ids[1]).status, 404);
  EXPECT_EQ(get(port_, "/api/sessions/" + ids[0]).status, 200);
  // Expired sessions free their slots.
  EXPECT_EQ(post(port_, "/api/sessions", {{"instanceId", "sawmill"}}).status, 201);
  EXPECT_EQ(post(port_, "/api/sessions", {{"instanceId", "sawmill"}}).status, 201);
  EXPECT_EQ(post(port_, "/api/sessions", {{"instanceId", "sawmill"}}).status, 503);
  EXPECT_EQ(post(port_, "/api/sessions", {{"instanceId", "nope"}}).status, 404);
}

}  // namespace
}  // namespace pf::server
