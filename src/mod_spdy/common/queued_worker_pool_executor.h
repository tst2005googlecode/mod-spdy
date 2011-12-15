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

#ifndef MOD_SPDY_COMMON_QUEUED_WORKER_POOL_EXECUTOR_H_
#define MOD_SPDY_COMMON_QUEUED_WORKER_POOL_EXECUTOR_H_

#include "base/basictypes.h"
#include "base/scoped_ptr.h"
#include "mod_spdy/common/executor.h"
#include "net/spdy/spdy_protocol.h"

namespace net_instaweb {
class Function;
class QueuedWorkerPool;
}  // namespace net_instaweb

namespace mod_spdy {

// An executor that uses a net_instaweb::QueuedWorkerPool to run tasks on
// worker threads.  This executor ignores the priority argument to AddTask, as
// QueuedWorkerPool has no prioritization mechanism.  This class is
// thread-safe.
class QueuedWorkerPoolExecutor : public Executor {
 public:
  // The executor gains ownership of the worker pool.
  explicit QueuedWorkerPoolExecutor(
      net_instaweb::QueuedWorkerPool* worker_pool);
  virtual ~QueuedWorkerPoolExecutor();

  // Executor methods:
  virtual void AddTask(net_instaweb::Function* task,
                       spdy::SpdyPriority priority);
  virtual void Stop();

 private:
  const scoped_ptr<net_instaweb::QueuedWorkerPool> worker_pool_;

  DISALLOW_COPY_AND_ASSIGN(QueuedWorkerPoolExecutor);
};

}  // namespace mod_spdy

#endif  // MOD_SPDY_COMMON_QUEUED_WORKER_POOL_EXECUTOR_H_
