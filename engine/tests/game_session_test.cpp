#include "pf/game_session.hpp"

#include "pf/json_io.hpp"
#include "pf/registry.hpp"
#include "pf/validator.hpp"

#include <random>

#include <gtest/gtest.h>

#include "instance_files.hpp"
#include "test_util.hpp"

namespace pf {
namespace {

// wood x5 -saw(2)-> plank; kiln (max 1 run); 2 workers, horizon 6.
Instance workshop() {
  return test::InstanceBuilder()
      .resource("wood", 1, 5)
      .resource("plank", 4)
      .resource("charcoal", 2)
      .process("saw", {{"wood", 2}}, {{"plank", 1}}, 2)
      .process("kiln", {{"wood", 1}}, {{"charcoal", 1}}, 1, /*max_runs=*/1)
      .workers(2)
      .horizon(6)
      .startMoney(3)
      .build();
}

constexpr ProcessIndex kSaw = 0;
constexpr ProcessIndex kKiln = 1;

void expectRejected(GameSession& session, const Action& action, const std::string& code,
                    const std::string& message_part = "") {
  const SessionState before = session.state();
  const auto error = session.apply(action);
  ASSERT_TRUE(error.has_value()) << "expected '" << code << "'";
  EXPECT_EQ(error->code, code);
  if (!message_part.empty()) {
    EXPECT_NE(error->message.find(message_part), std::string::npos) << error->message;
  }
  EXPECT_EQ(session.state(), before) << "illegal action changed state";
}

TEST(GameSession, StartsAtTickZero) {
  const GameSession session(workshop());
  const SessionState s = session.state();
  EXPECT_EQ(s.tick, 0);
  EXPECT_EQ(s.horizon, 6);
  EXPECT_EQ(s.inventory, (std::vector<Quantity>{5, 0, 0}));
  EXPECT_EQ(s.money, 3);
  EXPECT_EQ(s.projected_earnings, 3 + 5);
  EXPECT_EQ(s.free_workers, 2);
  EXPECT_TRUE(s.runs_in_progress.empty());
  EXPECT_TRUE(s.history.empty());
  EXPECT_FALSE(s.finished);
}

TEST(GameSession, AppliesActionsAndReportsProgress) {
  GameSession session(workshop());
  ASSERT_FALSE(session.apply(Action::startRun(kSaw)));
  ASSERT_FALSE(session.apply(Action::wait()));
  SessionState s = session.state();
  EXPECT_EQ(s.tick, 1);
  ASSERT_EQ(s.runs_in_progress.size(), 1U);
  EXPECT_EQ(s.runs_in_progress[0].remaining, 1);
  EXPECT_EQ(s.free_workers, 1);
  EXPECT_EQ(s.projected_earnings, 3 + 3 + 4);  // the saw run counts once it completes

  ASSERT_FALSE(session.apply(Action::advanceTo(2)));
  s = session.state();
  EXPECT_EQ(s.inventory[1], 1);
  EXPECT_TRUE(s.runs_in_progress.empty());
}

TEST(GameSession, RejectsInsufficientInventory) {
  GameSession session(workshop());
  ASSERT_FALSE(session.apply(Action::startRun(kSaw)));
  ASSERT_FALSE(session.apply(Action::advanceTo(2)));
  ASSERT_FALSE(session.apply(Action::startRun(kSaw)));
  ASSERT_FALSE(session.apply(Action::startRun(kKiln)));  // wood: 5 - 2 - 2 - 1 = 0
  ASSERT_FALSE(session.apply(Action::advanceTo(4)));
  expectRejected(session, Action::startRun(kSaw), "insufficient_inventory",
                 "not enough wood (need 2, have 0)");
}

TEST(GameSession, RejectsNoFreeWorker) {
  GameSession session(workshop());
  ASSERT_FALSE(session.apply(Action::startRun(kSaw)));
  ASSERT_FALSE(session.apply(Action::startRun(kSaw)));
  expectRejected(session, Action::startRun(kKiln), "no_free_worker", "no free worker");
}

TEST(GameSession, RejectsRunPastHorizon) {
  GameSession session(workshop());
  ASSERT_FALSE(session.apply(Action::advanceTo(5)));
  expectRejected(session, Action::startRun(kSaw), "past_horizon", "after the horizon 6");
}

TEST(GameSession, RejectsExceededMaxRuns) {
  GameSession session(workshop());
  ASSERT_FALSE(session.apply(Action::startRun(kKiln)));
  ASSERT_FALSE(session.apply(Action::wait()));
  expectRejected(session, Action::startRun(kKiln), "max_runs_exceeded", "max_runs 1");
}

TEST(GameSession, RejectsUnknownProcess) {
  GameSession session(workshop());
  expectRejected(session, Action::startRun(99), "unknown_process");
}

TEST(GameSession, RejectsBadTimeMoves) {
  GameSession session(workshop());
  expectRejected(session, Action::advanceTo(0), "invalid_tick");
  expectRejected(session, Action::advanceTo(7), "invalid_tick");
  ASSERT_FALSE(session.apply(Action::advanceTo(6)));
  expectRejected(session, Action::wait(), "at_horizon");
}

TEST(GameSession, RejectsActionsAfterFinish) {
  GameSession session(workshop());
  (void)session.finish();
  expectRejected(session, Action::wait(), "game_finished");
  expectRejected(session, Action::startRun(kKiln), "game_finished");
  EXPECT_TRUE(session.legalActions().empty());
}

TEST(GameSession, RejectsUndoRedoWithNothingToDo) {
  GameSession session(workshop());
  const SessionState before = session.state();
  EXPECT_EQ(session.undo()->code, "nothing_to_undo");
  EXPECT_EQ(session.redo()->code, "nothing_to_redo");
  EXPECT_EQ(session.state(), before);
}

TEST(GameSession, LegalActionsMatchCheck) {
  GameSession session(workshop());
  ASSERT_FALSE(session.apply(Action::startRun(kSaw)));
  const auto legal = session.legalActions();
  EXPECT_EQ(legal, (std::vector<Action>{Action::startRun(kSaw), Action::startRun(kKiln),
                                        Action::wait(), Action::advanceTo(2)}));
  for (const Action& a : legal) EXPECT_FALSE(session.check(a).has_value());
}

TEST(GameSession, AdvanceOffersHorizonWhenIdle) {
  const GameSession session(workshop());
  const auto legal = session.legalActions();
  EXPECT_EQ(legal.back(), Action::advanceTo(6));
}

TEST(GameSession, FinishScoresWithValidator) {
  GameSession session(workshop());
  ASSERT_FALSE(session.apply(Action::startRun(kSaw)));
  ASSERT_FALSE(session.apply(Action::startRun(kKiln)));
  ASSERT_FALSE(session.apply(Action::advanceTo(2)));
  ASSERT_FALSE(session.apply(Action::startRun(kSaw)));
  const Quantity projected = session.state().projected_earnings;
  const Solution s = session.finish();
  ASSERT_TRUE(s.valid);
  EXPECT_EQ(s.solver_name, "manual");
  EXPECT_EQ(s.runs, (Schedule{{kSaw, 0}, {kKiln, 0}, {kSaw, 2}}));
  EXPECT_EQ(s.earnings, 3 + 0 + 2 * 4 + 2);
  EXPECT_EQ(s.earnings, projected);
  EXPECT_EQ(session.state().tick, 6);
  EXPECT_TRUE(session.finished());
  // Undo reopens the game.
  ASSERT_FALSE(session.undo());
  EXPECT_FALSE(session.finished());
  EXPECT_EQ(session.state().tick, 2);
}

TEST(GameSession, NewActionClearsRedo) {
  GameSession session(workshop());
  ASSERT_FALSE(session.apply(Action::startRun(kSaw)));
  ASSERT_FALSE(session.undo());
  EXPECT_EQ(session.state().redo_available, 1U);
  ASSERT_FALSE(session.apply(Action::wait()));
  EXPECT_EQ(session.state().redo_available, 0U);
}

TEST(GameSession, RestartClearsEverything) {
  GameSession session(workshop());
  ASSERT_FALSE(session.apply(Action::startRun(kSaw)));
  ASSERT_FALSE(session.apply(Action::wait()));
  session.restart();
  EXPECT_EQ(session.state(), GameSession(workshop()).state());
}

TEST(GameSession, HintIsLegal) {
  GameSession session(workshop());
  for (int i = 0; i < 20 && !session.finished(); ++i) {
    const auto hint = session.hint();
    ASSERT_TRUE(hint.has_value());
    ASSERT_FALSE(session.apply(*hint)) << "hint was illegal";
  }
  EXPECT_TRUE(session.finished());
  EXPECT_GT(session.finish().earnings, GameSession(workshop()).state().projected_earnings);
}

TEST(GameSession, CopiesAreIndependent) {
  GameSession a(workshop());
  ASSERT_FALSE(a.apply(Action::startRun(kSaw)));
  GameSession b = a;
  ASSERT_FALSE(b.apply(Action::wait()));
  EXPECT_EQ(a.state().tick, 0);
  EXPECT_EQ(b.state().tick, 1);
  EXPECT_EQ(b.sim().inventory()[0], 3);
}

TEST(GameSession, ActionsForSchedule) {
  EXPECT_EQ(GameSession::actionsFor({{1, 3}, {0, 0}, {0, 3}}),
            (std::vector<Action>{Action::startRun(0), Action::advanceTo(3), Action::startRun(1),
                                 Action::startRun(0)}));
}

TEST(GameSession, LoadScheduleRejectsInvalidSchedule) {
  GameSession session(workshop());
  ASSERT_FALSE(session.apply(Action::wait()));
  const SessionState before = session.state();
  const auto error = session.loadSchedule({{kSaw, 5}});
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code, "past_horizon");
  EXPECT_EQ(session.state(), before);
}

// --- Property tests ----------------------------------------------------------------------

// Plays random legal actions until the game ends; returns the earnings reported by finish().
Solution playRandomly(GameSession& session, std::mt19937_64& rng) {
  while (!session.finished()) {
    const auto legal = session.legalActions();
    std::vector<Action> choices = legal;
    // Occasionally finish early; otherwise favor starting runs so games are interesting.
    if (choices.empty() || std::uniform_int_distribution<int>(0, 20)(rng) == 0) {
      choices = {Action::finish()};
    }
    const Action pick =
        choices[std::uniform_int_distribution<std::size_t>(0, choices.size() - 1)(rng)];
    EXPECT_FALSE(session.apply(pick).has_value());
  }
  return session.finish();
}

TEST(GameSessionProperty, ExportedScheduleReplaysToSameEarnings) {
  std::mt19937_64 rng(2024);
  for (int i = 0; i < 1000; ++i) {
    const Instance inst = test::tinyRandom(rng);
    GameSession session(inst);
    const Solution s = playRandomly(session, rng);
    ASSERT_TRUE(s.valid) << "instance " << i << ": " << s.error.value_or("");
    const ValidationReport replay = validate(inst, session.exportSchedule());
    ASSERT_TRUE(replay.valid) << "instance " << i;
    ASSERT_EQ(replay.earnings, s.earnings) << "instance " << i;
    ASSERT_EQ(session.state().projected_earnings, s.earnings) << "instance " << i;
  }
}

TEST(GameSessionProperty, UndoRestoresExactEarlierStates) {
  std::mt19937_64 rng(99);
  for (int i = 0; i < 300; ++i) {
    GameSession session(test::tinyRandom(rng));
    std::vector<SessionState> states{session.state()};
    for (int step = 0; step < 40; ++step) {
      const bool do_undo = session.state().history.empty()
                               ? false
                               : std::uniform_int_distribution<int>(0, 3)(rng) == 0;
      if (do_undo) {
        ASSERT_FALSE(session.undo());
        states.pop_back();
        // Compare everything except the redo counter, which undo legitimately changes.
        SessionState now = session.state();
        SessionState expected = states.back();
        now.redo_available = expected.redo_available = 0;
        ASSERT_EQ(now, expected) << "instance " << i << " step " << step;
        continue;
      }
      const auto legal = session.legalActions();
      if (legal.empty()) break;
      const Action a = legal[std::uniform_int_distribution<std::size_t>(0, legal.size() - 1)(rng)];
      ASSERT_FALSE(session.apply(a));
      states.push_back(session.state());
    }
    // Undo everything, then redo everything: back to the final state.
    const SessionState final_state = session.state();
    const std::size_t n = final_state.history.size();
    for (std::size_t k = 0; k < n; ++k) ASSERT_FALSE(session.undo());
    EXPECT_EQ(session.state().tick, 0);
    for (std::size_t k = 0; k < n; ++k) ASSERT_FALSE(session.redo());
    EXPECT_EQ(session.state(), final_state) << "instance " << i;
  }
}

TEST(GameSessionProperty, DeterministicReplayOfActions) {
  std::mt19937_64 rng(5);
  for (int i = 0; i < 200; ++i) {
    const Instance inst = test::tinyRandom(rng);
    GameSession a(inst);
    (void)playRandomly(a, rng);
    GameSession b(inst);
    for (const Action& action : a.history()) ASSERT_FALSE(b.apply(action));
    EXPECT_EQ(a.state(), b.state());
  }
}

TEST(GameSessionProperty, SolverSchedulesReplayToSolverEarnings) {
  std::mt19937_64 rng(31);
  const auto& registry = SolverRegistry::builtin();
  for (int i = 0; i < 200; ++i) {
    const Instance inst = test::tinyRandom(rng);
    for (const char* name : {"greedy", "greedy_potential", "brute_force"}) {
      const Solution solved = registry.create(name)->solve(inst, {}, nullptr);
      ASSERT_TRUE(solved.valid);
      GameSession session(inst);
      ASSERT_FALSE(session.loadSchedule(solved.runs));
      while (session.state().redo_available > 0) ASSERT_FALSE(session.redo());
      const Solution replayed = session.finish();
      ASSERT_TRUE(replayed.valid);
      EXPECT_EQ(replayed.earnings, solved.earnings) << name << " instance " << i;
    }
  }
}

TEST(GameSessionHandMade, SolverSchedulesReplayToSolverEarnings) {
  for (const auto& file : test::instanceFiles()) {
    const Instance inst = *json::parseInstance(test::loadJson(file)).value;
    for (const char* name : {"greedy", "greedy_potential"}) {
      const Solution solved = SolverRegistry::builtin().create(name)->solve(inst, {}, nullptr);
      GameSession session(inst);
      ASSERT_FALSE(session.loadSchedule(solved.runs));
      while (session.state().redo_available > 0) ASSERT_FALSE(session.redo());
      EXPECT_EQ(session.finish().earnings, solved.earnings) << name << " " << file.stem();
    }
  }
}

// --- JSON --------------------------------------------------------------------------------

TEST(GameSessionJson, RoundTripsIncludingRedo) {
  GameSession session(workshop());
  ASSERT_FALSE(session.apply(Action::startRun(kSaw)));
  ASSERT_FALSE(session.apply(Action::startRun(kKiln)));
  ASSERT_FALSE(session.apply(Action::advanceTo(2)));
  ASSERT_FALSE(session.apply(Action::startRun(kSaw)));
  ASSERT_FALSE(session.undo());

  const auto restored = json::parseSession(json::sessionToJson(session));
  ASSERT_TRUE(restored.ok()) << restored.errors.front();
  EXPECT_EQ(restored.value->state(), session.state());
  EXPECT_EQ(restored.value->redoStack(), session.redoStack());
}

TEST(GameSessionJson, RejectsTamperedHistory) {
  GameSession session(workshop());
  ASSERT_FALSE(session.apply(Action::startRun(kKiln)));
  nlohmann::json doc = json::sessionToJson(session);
  doc["history"].push_back({{"type", "start_run"}, {"process_id", "kiln"}});  // max_runs 1
  const auto restored = json::parseSession(doc);
  ASSERT_FALSE(restored.ok());
  EXPECT_NE(restored.errors.front().find("history[1]: illegal action"), std::string::npos)
      << restored.errors.front();
}

TEST(GameSessionJson, ParsesActions) {
  const Instance inst = workshop();
  EXPECT_EQ(*json::parseAction({{"type", "start_run"}, {"processId", "kiln"}}, inst).value,
            Action::startRun(kKiln));
  EXPECT_EQ(*json::parseAction({{"type", "advance_to"}, {"tick", 4}}, inst).value,
            Action::advanceTo(4));
  EXPECT_EQ(*json::parseAction({{"type", "wait"}}, inst).value, Action::wait());
  EXPECT_FALSE(json::parseAction({{"type", "jump"}}, inst).ok());
  EXPECT_FALSE(json::parseAction({{"type", "start_run"}, {"process_id", "nope"}}, inst).ok());
  EXPECT_FALSE(json::parseAction({{"type", "advance_to"}}, inst).ok());
}

TEST(GameSessionJson, StateUsesIds) {
  GameSession session(workshop());
  ASSERT_FALSE(session.apply(Action::startRun(kSaw)));
  const nlohmann::json j = json::toJson(session.state(), session.instance());
  EXPECT_EQ(j["inventory"]["wood"], 3);
  EXPECT_EQ(j["runs_in_progress"][0]["process_id"], "saw");
  EXPECT_EQ(j["history"][0]["type"], "start_run");
  EXPECT_EQ(j["run_counts"]["saw"], 1);
  EXPECT_EQ(j["workers"], 2);
}

}  // namespace
}  // namespace pf
