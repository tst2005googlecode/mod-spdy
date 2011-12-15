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

#ifndef MOD_SPDY_APACHE_APR_THREAD_POOL_EXECUTOR_H_
#define MOD_SPDY_APACHE_APR_THREAD_POOL_EXECUTOR_H_

#include <set>

#include "apr_thread_pool.h"

#include "base/basictypes.h"
#include "base/synchronization/lock.h"
#include "mod_spdy/common/executor.h"
#include "net/spdy/spdy_protocol.h"

namespace net_instaweb { class Function; }

namespace mod_spdy {

// An executor that uses an APR thread pool to execute tasks.  The same APR
// thread pool can be shared by many executors, and the Stop() method will only
// cancel the tasks pushed onto that particular executor.  This class is
// thread-safe.
class AprThreadPoolExecutor : public Executor {
 public:
  // The executor does _not_ gain ownership of the thread pool.
  explicit AprThreadPoolExecutor(apr_thread_pool_t* thread_pool);
  virtual ~AprThreadPoolExecutor();

  // Executor methods:
  virtual void AddTask(net_instaweb::Function* task,
                       spdy::SpdyPriority priority);
  // Once Stop() has been called, AddTask() will reject new tasks.  It is safe
  // to call Stop() more than once.
  virtual void Stop();

 private:
  // A helper struct, passed as the parameter object to apr_thread_pool_push().
  struct TaskData {
   public:
    TaskData(net_instaweb::Function* t, AprThreadPoolExecutor* own)
        : task(t), owner(own) {}
    net_instaweb::Function* const task;
    AprThreadPoolExecutor* const owner;
   private:
    DISALLOW_COPY_AND_ASSIGN(TaskData);
  };

  // If the executor has not been stopped, move the given TaskData object from
  // pending_tasks_ to running_tasks_ and return true; otherwise, return false,
  // indicating that this task should not start.
  bool StartTask(const TaskData* task_data);

  // Remove the given TaskData object from running_tasks_ and delete it.
  void TaskCompleted(const TaskData* task_data);

  // Function pointer to be passed to apr_thread_pool_push().
  static void* ThreadTask(apr_thread_t* thread, void* param);

  apr_thread_pool_t* const thread_pool_;
  base::Lock lock_;  // protects pending_tasks_, running_tasks_, and stopped_
  std::set<const TaskData*> pending_tasks_;
  std::set<const TaskData*> running_tasks_;
  bool stopped_;

  DISALLOW_COPY_AND_ASSIGN(AprThreadPoolExecutor);
};

}  // namespace mod_spdy

#endif  // MOD_SPDY_APACHE_APR_THREAD_POOL_EXECUTOR_H_
