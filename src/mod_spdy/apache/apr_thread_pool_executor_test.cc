// Copyright 2011 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "mod_spdy/apache/apr_thread_pool_executor.h"

#include "apr_thread_pool.h"

#include "base/basictypes.h"
#include "base/logging.h"
#include "base/synchronization/condition_variable.h"
#include "base/synchronization/lock.h"
#include "base/time.h"
#include "mod_spdy/apache/pool_util.h"
#include "net/instaweb/util/public/function.h"
#include "net/spdy/spdy_protocol.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

class CounterFunction : public net_instaweb::Function {
 public:
  CounterFunction(base::Lock* lock, base::ConditionVariable* condvar,
                  int* run_count, int* cancel_count)
      : lock_(lock), condvar_(condvar),
        run_count_(run_count), cancel_count_(cancel_count) {}
  virtual ~CounterFunction() {}
 protected:
  // net_instaweb::Function methods:
  virtual void Run() {
    base::AutoLock autolock(*lock_);
    condvar_->Broadcast();
    ++(*run_count_);
  }
  virtual void Cancel() {
    base::AutoLock autolock(*lock_);
    ++(*cancel_count_);
  }
 private:
  base::Lock* const lock_;
  base::ConditionVariable* const condvar_;
  int* const run_count_;
  int* const cancel_count_;
  DISALLOW_COPY_AND_ASSIGN(CounterFunction);
};

TEST(AprThreadPoolExecutorTest, Simple) {
  // Create an APR thread pool with 2 threads.
  mod_spdy::LocalPool local;
  apr_thread_pool_t* thread_pool = NULL;
  apr_thread_pool_create(&thread_pool, 2, 2, local.pool());
  ASSERT_TRUE(thread_pool != NULL);

  // Create an executor.
  mod_spdy::AprThreadPoolExecutor executor(thread_pool);
  base::Lock lock;
  base::ConditionVariable condvar(&lock);
  int run_count = 0;
  int cancel_count = 0;

  // Add 1000 tasks.
  const int num_tasks = 1000;
  for (int i = 0; i < num_tasks; ++i) {
    executor.AddTask(
        new CounterFunction(&lock, &condvar, &run_count, &cancel_count),
        SPDY_PRIORITY_HIGHEST);
  }

  // Wait until at least one task finishes (probably, one has already finished
  // by the time we finish the for-loop, but we do this anyway to help avoid
  // test flakiness).
  {
    base::AutoLock autolock(lock);
    if (run_count <= 0) {
      condvar.TimedWait(base::TimeDelta::FromMilliseconds(500));
    }
  }

  // Stop the executor as soon as at least one task finishes.  We expect that
  // we'll get here long before all tasks finished, so at least some of them
  // should get cancelled.  At time of this writing, with a debug build on my
  // (mdsteele) workstation, usually about 90-ish tasks will finish before we
  // stop the executor.
  //
  // TODO(mdsteele): This is still flaky, though, and when I run this test
  //   under Valgrind it tends to fail (due to all tasks completing before we
  //   manage to stop the executor).  It'd be nice to fix it.
  executor.Stop();

  {
    base::AutoLock autolock(lock);
    LOG(INFO) << "run_count=" << run_count << " cancel_count=" << cancel_count;
    EXPECT_EQ(num_tasks, run_count + cancel_count)
        << "run_count=" << run_count << " cancel_count=" << cancel_count;
    EXPECT_GT(run_count, 0);
    EXPECT_LT(run_count, num_tasks);
    EXPECT_GT(cancel_count, 0);
    EXPECT_LT(cancel_count, num_tasks);
  }

  apr_thread_pool_destroy(thread_pool);
}

}  // namespace
