// Copyright 2012 Google Inc.
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

#include "mod_spdy/common/thread_pool.h"

#include "base/basictypes.h"
#include "base/scoped_ptr.h"
#include "base/synchronization/lock.h"
#include "base/threading/platform_thread.h"
#include "mod_spdy/common/executor.h"
#include "net/instaweb/util/public/function.h"
#include "net/spdy/spdy_protocol.h"
#include "testing/gtest/include/gtest/gtest.h"

// When adding tests here, try to keep them robust against thread scheduling
// differences from run to run.  In particular, they shouldn't fail just
// because you're running under Valgrind.

namespace {

// When run, a TestFunction waits for `wait` millis, then sets `*result` to
// RAN.  When cancelled, it sets *result to CANCELLED.
class TestFunction : public net_instaweb::Function {
 public:
  enum Result { NOTHING, RAN, CANCELLED };
  TestFunction(int wait, base::Lock* lock, Result* result)
      : wait_(wait), lock_(lock), result_(result) {}
  virtual ~TestFunction() {}
 protected:
  // net_instaweb::Function methods:
  virtual void Run() {
    base::PlatformThread::Sleep(wait_);
    base::AutoLock autolock(*lock_);
    *result_ = RAN;
  }
  virtual void Cancel() {
    base::AutoLock autolock(*lock_);
    *result_ = CANCELLED;
  }
 private:
  const int wait_;
  base::Lock* const lock_;
  Result* const result_;
  DISALLOW_COPY_AND_ASSIGN(TestFunction);
};

// Test that we execute tasks concurrently, that that we respect priorities
// when pulling tasks from the queue.
TEST(ThreadPoolTest, ConcurrencyAndPrioritization) {
  // Create a thread pool with 2 threads, and an executor.
  mod_spdy::ThreadPool thread_pool(2);
  ASSERT_TRUE(thread_pool.Start());
  scoped_ptr<mod_spdy::Executor> executor(thread_pool.NewExecutor());

  base::Lock lock;
  TestFunction::Result result0 = TestFunction::NOTHING;
  TestFunction::Result result1 = TestFunction::NOTHING;
  TestFunction::Result result2 = TestFunction::NOTHING;
  TestFunction::Result result3 = TestFunction::NOTHING;

  // Create a high-priority TestFunction, which waits for 200 millis then
  // records that it ran.
  executor->AddTask(new TestFunction(200, &lock, &result0), 0);
  // Create several TestFunctions at different priorities.  Each waits 100
  // millis then records that it ran.
  executor->AddTask(new TestFunction(100, &lock, &result1), 1);
  executor->AddTask(new TestFunction(100, &lock, &result3), 3);
  executor->AddTask(new TestFunction(100, &lock, &result2), 2);

  // Wait 150 millis, then stop the executor.
  base::PlatformThread::Sleep(150);
  executor->Stop();

  // Only TestFunctions that _started_ within the first 150 millis should have
  // run; the others should have been cancelled.
  //   - The priority-0 function should have started first, on the first
  //     thread.  It finishes after 200 millis.
  //   - The priority-1 function should run on the second thread.  It finishes
  //     after 100 millis.
  //   - The priority-2 function should run on the second thread after the
  //     priority-1 function finishes, even though it was pushed last, because
  //     it's higher-priority than the priority-3 function.  It finishes at the
  //     200-milli mark.
  //   - The priority-3 function should not get a chance to run, because we
  //     stop the executor after 150 millis, and the soonest it could start is
  //     the 200-milli mark.
  base::AutoLock autolock(lock);
  EXPECT_EQ(TestFunction::RAN, result0);
  EXPECT_EQ(TestFunction::RAN, result1);
  EXPECT_EQ(TestFunction::RAN, result2);
  EXPECT_EQ(TestFunction::CANCELLED, result3);
}

// Test that stopping one executor doesn't affect tasks on another executor
// from the same ThreadPool.
TEST(ThreadPoolTest, MultipleExecutors) {
  // Create a thread pool with 3 threads, and two executors.
  mod_spdy::ThreadPool thread_pool(3);
  ASSERT_TRUE(thread_pool.Start());
  scoped_ptr<mod_spdy::Executor> executor1(thread_pool.NewExecutor());
  scoped_ptr<mod_spdy::Executor> executor2(thread_pool.NewExecutor());

  base::Lock lock;
  TestFunction::Result e1r1 = TestFunction::NOTHING;
  TestFunction::Result e1r2 = TestFunction::NOTHING;
  TestFunction::Result e1r3 = TestFunction::NOTHING;
  TestFunction::Result e2r1 = TestFunction::NOTHING;
  TestFunction::Result e2r2 = TestFunction::NOTHING;
  TestFunction::Result e2r3 = TestFunction::NOTHING;

  // Add some tasks to the executors.  Each one takes 50 millis to run.
  executor1->AddTask(new TestFunction(50, &lock, &e1r1), 0);
  executor2->AddTask(new TestFunction(50, &lock, &e2r1), 0);
  executor1->AddTask(new TestFunction(50, &lock, &e1r2), 0);
  executor2->AddTask(new TestFunction(50, &lock, &e2r2), 1);
  executor1->AddTask(new TestFunction(50, &lock, &e1r3), 3);
  executor2->AddTask(new TestFunction(50, &lock, &e2r3), 1);

  // Wait 20 millis (to make sure the first few tasks got picked up), then
  // destroy executor2, which should stop it.  Finally, sleep another 100
  // millis to give the remaining tasks a chance to finish.
  base::PlatformThread::Sleep(20);
  executor2.reset();
  base::PlatformThread::Sleep(100);

  // The three high priority tasks should have all run.  The other two tasks on
  // executor2 should have been cancelled when we stopped executor2, but the
  // low-priority task on executor1 should have been left untouched, and
  // allowed to finish.
  base::AutoLock autolock(lock);
  EXPECT_EQ(TestFunction::RAN, e1r1);
  EXPECT_EQ(TestFunction::RAN, e2r1);
  EXPECT_EQ(TestFunction::RAN, e1r2);
  EXPECT_EQ(TestFunction::CANCELLED, e2r2);
  EXPECT_EQ(TestFunction::RAN, e1r3);
  EXPECT_EQ(TestFunction::CANCELLED, e2r3);
}

}  // namespace
