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

#include "mod_spdy/common/queued_worker_pool_executor.h"

#include "base/basictypes.h"
#include "net/instaweb/util/public/function.h"
#include "net/instaweb/util/public/queued_worker_pool.h"
#include "net/spdy/spdy_protocol.h"

namespace mod_spdy {

QueuedWorkerPoolExecutor::QueuedWorkerPoolExecutor(
    net_instaweb::QueuedWorkerPool* worker_pool)
    : worker_pool_(worker_pool) {}

QueuedWorkerPoolExecutor::~QueuedWorkerPoolExecutor() {}

void QueuedWorkerPoolExecutor::AddTask(net_instaweb::Function* task,
                                       spdy::SpdyPriority priority) {
  net_instaweb::QueuedWorkerPool::Sequence* sequence =
      worker_pool_->NewSequence();
  if (sequence == NULL) {
    task->CallCancel();
  } else {
    sequence->Add(task);
  }
}

void QueuedWorkerPoolExecutor::Stop() {
  worker_pool_->ShutDown();
}

}  // namespace mod_spdy
