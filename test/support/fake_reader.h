/**
 * @file
 * FakeReader, a scripted stand in for a Reader, shared by the bring up tests.
 *
 * Header only, so each test suite that includes it compiles its own copy.
 */

#pragma once

#include <cstdlib>
#include <deque>

#include "reader.h"

/**
 * Answers probe() from a scripted list and counts the calls. The getters are
 * never called by the code under test, so they always fail.
 */
class FakeReader : public Reader {
 public:
  int probes = 0;  ///< Times probe() was called.

  /**
   * Queues the result of a future probe().
   *
   * @param[in] status  EXIT_SUCCESS or EXIT_FAILURE.
   */
  void queueProbe(int status) { results.push_back(status); }

  /**
   * Attaches the bus.
   *
   * @param[in] module  Bus, stored and unused.
   * @retval EXIT_SUCCESS  Always.
   */
  int init(Module &module) override {
    this->module = &module;
    return EXIT_SUCCESS;
  }

  /**
   * Counts the call and returns the next scripted result.
   *
   * @retval EXIT_SUCCESS  The next scripted result was a pass.
   * @retval EXIT_FAILURE  It was a fail, or nothing is scripted.
   */
  int probe() override {
    probes++;
    if (results.empty()) {
      return EXIT_FAILURE;
    }
    int status = results.front();
    results.pop_front();
    return status;
  }

  /**
   * Unused.
   *
   * @param[out] val  Untouched.
   * @retval EXIT_FAILURE  Always.
   */
  int get_import(float *val) override {
    (void)val;
    return EXIT_FAILURE;
  }

  /**
   * Unused.
   *
   * @param[out] val  Untouched.
   * @retval EXIT_FAILURE  Always.
   */
  int get_export(float *val) override {
    (void)val;
    return EXIT_FAILURE;
  }

  /**
   * Unused.
   *
   * @param[out] val  Untouched.
   * @retval EXIT_FAILURE  Always.
   */
  int get_voltage(float *val) override {
    (void)val;
    return EXIT_FAILURE;
  }

 private:
  std::deque<int> results;  ///< Scripted probe() results, oldest first.
};
