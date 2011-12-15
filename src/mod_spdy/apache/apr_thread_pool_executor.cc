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

#include <set>

#include "apr_thread_pool.h"

#include "base/basictypes.h"
#include "base/logging.h"
#include "base/stl_util-inl.h"
#include "base/synchronization/lock.h"
#include "net/instaweb/util/public/function.h"
#include "net/spdy/spdy_protocol.h"

namespace {

// Given a SpdyPriority value, pick an appropriate priority value to pass into
// apr_thread_pool_push().
apr_byte_t SpdyPriorityToAprPriority(spdy::SpdyPriority priority) {
  COMPILE_ASSERT(SPDY_PRIORITY_HIGHEST == 0, zero_is_highest_priority);
  COMPILE_ASSERT(SPDY_PRIORITY_LOWEST == 3, three_is_lowest_priority);
  switch (priority) {
    case 0:
      return APR_THREAD_TASK_PRIORITY_HIGHEST;
    case 1:
      return APR_THREAD_TASK_PRIORITY_HIGH;
    case 2:
      return APR_THREAD_TASK_PRIORITY_NORMAL;
    case 3:
      return APR_THREAD_TASK_PRIORITY_LOW;
    default:
      LOG(DFATAL) << "Invalid priority value: " << priority;
      return APR_THREAD_TASK_PRIORITY_LOWEST;
  }
}

}  // namespace

namespace mod_spdy {

AprThreadPoolExecutor::AprThreadPoolExecutor(apr_thread_pool_t* thread_pool)
    : thread_pool_(thread_pool), stopped_(false) {}

AprThreadPoolExecutor::~AprThreadPoolExecutor() {
  // Make sure all tasks are finished and deleted.  If Stop() has already been
  // called, this will be a (fairly quick) no-op.
  Stop();
}

void AprThreadPoolExecutor::AddTask(net_instaweb::Function* task,
                                    spdy::SpdyPriority priority) {
  base::AutoLock autolock(lock_);

  // If we have stopped this executor, then cancel the task immediately and
  // return.
  if (stopped_) {
    task->CallCancel();  // Cancel and delete the Function object.
    return;
  }

  // If we haven't stopped this executor yet, create a new TaskData object.
  TaskData* task_data = new TaskData(task, this);

  // Push the task onto the thread pool, with this Executor object as the
  // "owner".  We can later call apr_thread_pool_tasks_cancel to cancel all
  // tasks pushed with the same owner pointer.
  const apr_status_t status = apr_thread_pool_push(
      thread_pool_, ThreadTask, task_data,
      SpdyPriorityToAprPriority(priority), this);

  // If the call to apr_thread_pool_push() failed for some reason (which
  // shouldn't happen), then we should kill the task at this point so we don't
  // leak memory.
  if (status != APR_SUCCESS) {
    LOG(DFATAL) << "apr_thread_pool_push failed (status=" << status << ")";
    task->CallCancel();  // Cancel and delete the Function object.
    delete task_data;
    return;
  }

  // Now that we know that apr_thread_pool_push() succeeded, go ahead and add
  // the TaskData to pending_tasks_.  We must do this before the task starts
  // running; however, even though we already called apr_thread_pool_push(),
  // the task can't start running (via StartTask) until we release the lock at
  // the end of this method, so it's okay that we don't insert task_data into
  // pending_tasks_ until now.
  pending_tasks_.insert(task_data);
}

void AprThreadPoolExecutor::Stop() {
  // Mark this executor as stopped so that 1) new tasks will be rejected by
  // AddTask() and 2) none of the not-yet-started tasks will start running.
  {
    base::AutoLock autolock(lock_);
    stopped_ = true;

    // If there are no tasks, then we're done.
    if (pending_tasks_.empty() && running_tasks_.empty()) {
      return;
    }
  }

  // Tell the APR thread pool to cancel all tasks that were pushed by this
  // executor (tasks pushed by other owners will be left alone).  Note that we
  // mustn't be holding the lock for this call, because this call will block
  // until all currently running tasks complete.
  apr_thread_pool_tasks_cancel(thread_pool_, this);

  // At this point, all tasks that had been running have completed, removed
  // themselves from running_tasks_, and deleted themselves.  All that remains
  // is to cancel, delete, and remove all the previously pending tasks.
  {
    base::AutoLock autolock(lock_);
    DCHECK(running_tasks_.empty());

    // Cancel and delete all unstarted tasks.
    for (std::set<const TaskData*>::const_iterator iter =
             pending_tasks_.begin(); iter != pending_tasks_.end(); ++iter) {
      const TaskData* task_data = *iter;
      task_data->task->CallCancel();
      delete task_data;
    }
    pending_tasks_.clear();
  }
}

bool AprThreadPoolExecutor::StartTask(const TaskData* task_data) {
  base::AutoLock autolock(lock_);
  DCHECK(running_tasks_.count(task_data) == 0);
  // If the executor has been stopped, then we shouldn't start any new tasks.
  if (stopped_) {
    return false;
  }
  // Move the task from pending_tasks_ to running_tasks_.
  DCHECK(pending_tasks_.count(task_data) == 1);
  pending_tasks_.erase(task_data);
  running_tasks_.insert(task_data);
  return true;
}

// This should be called only from ThreadTask().
void AprThreadPoolExecutor::TaskCompleted(const TaskData* task_data) {
  base::AutoLock autolock(lock_);
  DCHECK(pending_tasks_.count(task_data) == 0);
  DCHECK(running_tasks_.count(task_data) == 1);
  running_tasks_.erase(task_data);
  delete task_data;
}

// static
void* AprThreadPoolExecutor::ThreadTask(apr_thread_t* thread, void* param) {
  const TaskData* task_data = static_cast<TaskData*>(param);
  if (task_data->owner->StartTask(task_data)) {
    // The executor hasn't been stopped yet, so start this task.
    task_data->task->CallRun();
    // At this point, task_data->task has been deleted by CallRun(), so we just
    // need to delete task_data (and remove it from running_tasks_).
    task_data->owner->TaskCompleted(task_data);
  } else {
    // Oops, looks like the executor has already been stopped, so just don't do
    // anything.
  }
  return NULL;
}

}  // namespace mod_spdy
