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

#include <map>
#include <vector>

#include "base/basictypes.h"
#include "base/scoped_ptr.h"
#include "base/synchronization/condition_variable.h"
#include "base/synchronization/lock.h"
#include "base/threading/platform_thread.h"
#include "mod_spdy/common/executor.h"
#include "net/instaweb/util/public/function.h"
#include "net/spdy/spdy_protocol.h"

namespace mod_spdy {

// An executor that uses the ThreadPool to execute tasks.  Returned by
// ThreadPool::NewExecutor.
class ThreadPool::ThreadPoolExecutor : public Executor {
 public:
  explicit ThreadPoolExecutor(ThreadPool* master)
      : master_(master),
        stopping_condvar_(&master_->lock_),
        stopped_(false) {}
  virtual ~ThreadPoolExecutor() { Stop(); }

  // Executor methods:
  virtual void AddTask(net_instaweb::Function* task,
                       spdy::SpdyPriority priority);
  virtual void Stop();

 private:
  friend class WorkerThread;
  ThreadPool* const master_;
  base::ConditionVariable stopping_condvar_;
  bool stopped_;  // protected by master_->lock_

  DISALLOW_COPY_AND_ASSIGN(ThreadPoolExecutor);
};

// Add a task to the executor; if the executor has already been stopped, just
// cancel the task immediately.
void ThreadPool::ThreadPoolExecutor::AddTask(net_instaweb::Function* task,
                                             spdy::SpdyPriority priority) {
  {
    base::AutoLock autolock(master_->lock_);
    // If the executor hasn't been stopped, add the task to the queue and
    // notify a worker that there's a new task ready to be taken.
    if (!stopped_) {
      master_->task_queue_.insert(std::make_pair(priority, Task(task, this)));
      master_->worker_condvar_.Signal();
      return;
    }
  }
  // If we've already stopped, just cancel the task (after releasing the lock).
  task->CallCancel();
}

// Stop the executor.  Cancel all pending tasks in the thread pool owned by
// this executor, and then block until all active tasks owned by this executor
// complete.  Stopping the executor more than once has no effect.
void ThreadPool::ThreadPoolExecutor::Stop() {
  std::vector<net_instaweb::Function*> functions_to_cancel;
  {
    base::AutoLock autolock(master_->lock_);
    if (stopped_) {
      return;
    }
    stopped_ = true;

    // Remove all tasks owned by this executor from the queue, and collect up
    // the function objects to be cancelled.
    TaskQueue::iterator next_iter = master_->task_queue_.begin();
    while (next_iter != master_->task_queue_.end()) {
      TaskQueue::iterator iter = next_iter;
      const Task& task = iter->second;
      ++next_iter;  // Increment next_iter _before_ we might erase iter.
      if (task.owner == this) {
        functions_to_cancel.push_back(task.function);
        master_->task_queue_.erase(iter);
      }
    }
  }

  // Unlock while we cancel the functions, so we're not hogging the lock for
  // too long, and to avoid potential deadlock if the cancel method tries to do
  // anything with the thread pool.
  for (std::vector<net_instaweb::Function*>::const_iterator iter =
           functions_to_cancel.begin();
       iter != functions_to_cancel.end(); ++iter) {
    (*iter)->CallCancel();
  }
  // CallCancel deletes the Function objects, invalidating the pointers in this
  // list, so let's go ahead and clear it (which also saves a little memory
  // while we're blocked below).
  functions_to_cancel.clear();

  // Block until all our active tasks are completed.
  {
    base::AutoLock autolock(master_->lock_);
    while (master_->active_task_counts_.count(this) > 0) {
      stopping_condvar_.Wait();
    }
  }
}

// A WorkerThread object wraps a platform-specific thread handle, and provides
// the method run by that thread (ThreadMain).
class ThreadPool::WorkerThread : public base::PlatformThread::Delegate {
 public:
  explicit WorkerThread(ThreadPool* master) : master_(master) {}
  virtual ~WorkerThread() {}

  // Start the thread running.  Return false on failure.
  bool Start() { return base::PlatformThread::Create(0, this, &thread_); }

  // Block until the thread completes.  You must set master_->shutting_down_ to
  // true before calling this method, or the thread will never terminate.
  void Join() { base::PlatformThread::Join(thread_); }

  // base::PlatformThread::Delegate method:
  virtual void ThreadMain();

 private:
  ThreadPool* const master_;
  base::PlatformThreadHandle thread_;

  DISALLOW_COPY_AND_ASSIGN(WorkerThread);
};

// This is the code executed by the thread; when this method returns, the
// thread will terminate.
void ThreadPool::WorkerThread::ThreadMain() {
  // We start by grabbing the master lock, but we release it below whenever we
  // are 1) waiting for a new task or 2) executing a task.  So in fact most of
  // the time we are not holding the lock.
  base::AutoLock autolock(master_->lock_);
  while (true) {
    // Wait until there's a task available (or we're shutting down).
    while (!master_->shutting_down_ && master_->task_queue_.empty()) {
      master_->worker_condvar_.Wait();
    }

    // If we're shutting down, exit the thread.
    if (master_->shutting_down_) {
      return;
    }

    // Otherwise, there must be at least one task available now, so pop the
    // highest-priority task from the queue.  Note that smaller values
    // correspond to higher priorities, so task_queue_.begin() gets us the
    // highest-priority pending task.
    DCHECK(!master_->task_queue_.empty());
    COMPILE_ASSERT(SPDY_PRIORITY_HIGHEST < SPDY_PRIORITY_LOWEST,
                   lower_numbers_are_higher_priority);
    TaskQueue::iterator task_iter = master_->task_queue_.begin();
    const Task task = task_iter->second;
    master_->task_queue_.erase(task_iter);

    // Increment the count of active tasks for the executor that owns this
    // task; we'll decrement it again when the task completes.
    ++(master_->active_task_counts_[task.owner]);

    // Release the lock while we execute the task.  Note that we use AutoUnlock
    // here rather than one AutoLock for the above code and another for the
    // below code, so that we don't have to release and reacquire the lock at
    // the edge of the while-loop.
    {
      base::AutoUnlock autounlock(master_->lock_);
      task.function->CallRun();
    }

    // We've completed the task and reaquired the lock, so decrement the count
    // of active tasks for this owner.
    OwnerMap::iterator count_iter =
        master_->active_task_counts_.find(task.owner);
    DCHECK(count_iter != master_->active_task_counts_.end());
    DCHECK(count_iter->second > 0);
    --(count_iter->second);
    // If this was the last active task for the owner, notify anyone who might
    // be waiting for the owner to stop.
    if (count_iter->second == 0) {
      master_->active_task_counts_.erase(count_iter);
      task.owner->stopping_condvar_.Broadcast();
    }
  }
}

ThreadPool::ThreadPool(int max_threads)
    : max_threads_(max_threads),
      worker_condvar_(&lock_),
      shutting_down_(false) {}

ThreadPool::~ThreadPool() {
  {
    base::AutoLock autolock(lock_);
    // If we're doing things right, all the Executors should have been
    // destroyed before the ThreadPool is destroyed, so there should be no
    // pending or active tasks.
    DCHECK(task_queue_.empty());
    DCHECK(active_task_counts_.empty());
    // Wake up all the worker threads and tell them to shut down.
    shutting_down_ = true;
    worker_condvar_.Broadcast();
  }

  // Stop all the worker threads and delete the WorkerThread objects.
  for (std::vector<WorkerThread*>::const_iterator iter = workers_.begin();
       iter != workers_.end(); ++iter) {
    WorkerThread* worker = *iter;
    worker->Join();
    delete worker;
  }
}

bool ThreadPool::Start() {
  // Start up max_threads_ workers; if any of the worker threads fail to start,
  // then this method fails and the ThreadPool should be deleted.
  for (int i = 0; i < max_threads_; ++i) {
    scoped_ptr<WorkerThread> worker(new WorkerThread(this));
    if (!worker->Start()) {
      return false;
    }
    workers_.push_back(worker.release());
  }
  // TODO(mdsteele): Consider switching to a scheme where we don't start all
  //   threads right away; instead we create new threads as necessary, and shut
  //   some of them down again if things quiet down for long enough.
  DCHECK(workers_.size() == max_threads_);
  return true;
}

Executor* ThreadPool::NewExecutor() {
  return new ThreadPoolExecutor(this);
}

}  // namespace mod_spdy
