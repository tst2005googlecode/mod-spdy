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
#include <set>
#include <vector>

#include "base/basictypes.h"
#include "base/scoped_ptr.h"
#include "base/synchronization/condition_variable.h"
#include "base/synchronization/lock.h"
#include "base/threading/platform_thread.h"
#include "base/time.h"
#include "mod_spdy/common/executor.h"
#include "net/instaweb/util/public/function.h"
#include "net/spdy/spdy_protocol.h"

namespace {

// Shut down a worker thread after it has been idle for this many seconds:
const int64 kDefaultMaxWorkerIdleSeconds = 60;

}  // namespace

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
      master_->StartNewWorkerIfNeeded();
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
  // Return true if the worker should delete itself before terminating.
  bool ThreadMainImpl();

  ThreadPool* const master_;
  base::PlatformThreadHandle thread_;

  DISALLOW_COPY_AND_ASSIGN(WorkerThread);
};

// This is the code executed by the thread; when this method returns, the
// thread will terminate.
void ThreadPool::WorkerThread::ThreadMain() {
  const bool delete_ourselves = ThreadMainImpl();
  if (delete_ourselves) {
    // We are safe to delete ourselves here because:
    //   1) If ThreadMainImpl() returns true, the worker has already been
    //      removed from the master, so there is no one else pointing to us.
    //   2) The thread will terminate as soon as we return from this method, so
    //      obviously we won't be touching our instance fields ever again.
    //   3) The thread won't be touching us or our instance fields either.  It
    //      doesn't know about fields defined in this class, of course, and our
    //      superclass has no instance fields.
    //   4) Note that PlatformThreadHandle is just an integer (thread ID).
    //      It's not some object that the thread needs, or any such thing.
    delete this;
  }
}

bool ThreadPool::WorkerThread::ThreadMainImpl() {
  // We start by grabbing the master lock, but we release it below whenever we
  // are 1) waiting for a new task or 2) executing a task.  So in fact most of
  // the time we are not holding the lock.
  base::AutoLock autolock(master_->lock_);
  while (true) {
    // Wait until there's a task available (or we're shutting down), but don't
    // stay idle for more than kMaxWorkerIdleSeconds seconds.
    base::TimeDelta time_remaining = master_->max_thread_idle_time_;
    while (!master_->shutting_down_ && master_->task_queue_.empty() &&
           time_remaining.InSecondsF() > 0.0) {
      // Note that TimedWait can wake up spuriously before the time runs out,
      // so we need to measure how long we actually waited for.
      const base::Time start = base::Time::Now();
      master_->worker_condvar_.TimedWait(time_remaining);
      const base::Time end = base::Time::Now();
      // Note that the system clock can go backwards if it is reset, so make
      // sure we never _increase_ time_remaining.
      if (end > start) {
        time_remaining -= end - start;
      }
    }

    // If we're shutting down, exit the thread.  The master will delete us.
    if (master_->shutting_down_) {
      return false;  // false = the worker should not delete itself
    }

    // If we ran out of time without getting a task, maybe we should shut this
    // thread down.
    if (master_->task_queue_.empty()) {
      DCHECK_LE(time_remaining.InSecondsF(), 0.0);
      DCHECK_GE(master_->workers_.size(), master_->min_threads_);
      // Don't shut down the thread if we're already at the minimum number of
      // threads; just go back to the top of the while (true) loop.
      if (master_->workers_.size() <= master_->min_threads_) {
        continue;
      }
      // Remove this thread from the pool and exit.
      DCHECK_EQ(1, master_->workers_.count(this));
      master_->workers_.erase(this);
      return true;  // true = the worker should delete itself
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
    ++(master_->num_busy_workers_);
    DCHECK_LE(master_->num_busy_workers_, master_->workers_.size());
    {
      base::AutoUnlock autounlock(master_->lock_);
      task.function->CallRun();
    }
    --(master_->num_busy_workers_);
    DCHECK_GE(master_->num_busy_workers_, 0);

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

ThreadPool::ThreadPool(int min_threads, int max_threads)
    : min_threads_(min_threads),
      max_threads_(max_threads),
      max_thread_idle_time_(
          base::TimeDelta::FromSeconds(kDefaultMaxWorkerIdleSeconds)),
      worker_condvar_(&lock_),
      num_busy_workers_(0),
      shutting_down_(false) {
  DCHECK_GE(max_thread_idle_time_.InSecondsF(), 0.0);
  DCHECK_LE(min_threads_, max_threads_);
}

ThreadPool::ThreadPool(int min_threads, int max_threads,
                       base::TimeDelta max_thread_idle_time)
    : min_threads_(min_threads),
      max_threads_(max_threads),
      max_thread_idle_time_(max_thread_idle_time),
      worker_condvar_(&lock_),
      num_busy_workers_(0),
      shutting_down_(false) {
  DCHECK_GE(max_thread_idle_time_.InSecondsF(), 0.0);
  DCHECK_LE(min_threads_, max_threads_);
}

ThreadPool::~ThreadPool() {
  std::vector<WorkerThread*> workers;
  {
    base::AutoLock autolock(lock_);
    // If we're doing things right, all the Executors should have been
    // destroyed before the ThreadPool is destroyed, so there should be no
    // pending or active tasks.
    DCHECK(task_queue_.empty());
    DCHECK(active_task_counts_.empty());
    // Copy over the list of workers to shut down (so that we don't touch
    // workers_ after releasing the lock and while the worker threads are
    // still shutting down).
    workers.assign(workers_.begin(), workers_.end());
    workers_.clear();
    // Wake up all the worker threads and tell them to shut down.
    shutting_down_ = true;
    worker_condvar_.Broadcast();
  }

  // Stop all the worker threads and delete the WorkerThread objects.
  for (std::vector<WorkerThread*>::const_iterator iter = workers.begin();
       iter != workers.end(); ++iter) {
    WorkerThread* worker = *iter;
    worker->Join();
    delete worker;
  }
}

bool ThreadPool::Start() {
  base::AutoLock autolock(lock_);
  DCHECK(task_queue_.empty());
  DCHECK(workers_.empty());
  // Start up min_threads_ workers; if any of the worker threads fail to start,
  // then this method fails and the ThreadPool should be deleted.
  for (int i = 0; i < min_threads_; ++i) {
    scoped_ptr<WorkerThread> worker(new WorkerThread(this));
    if (!worker->Start()) {
      return false;
    }
    workers_.insert(worker.release());
  }
  DCHECK_EQ(min_threads_, workers_.size());
  return true;
}

Executor* ThreadPool::NewExecutor() {
  return new ThreadPoolExecutor(this);
}

int ThreadPool::GetNumWorkersForTest() {
  base::AutoLock autolock(lock_);
  return workers_.size();
}

int ThreadPool::GetNumIdleWorkersForTest() {
  base::AutoLock autolock(lock_);
  DCHECK_GE(num_busy_workers_, 0);
  DCHECK_LE(num_busy_workers_, workers_.size());
  return workers_.size() - num_busy_workers_;
}

// This method is called each time we add a new task to the thread pool.
void ThreadPool::StartNewWorkerIfNeeded() {
  lock_.AssertAcquired();
  DCHECK_GE(num_busy_workers_, 0);
  DCHECK_LE(num_busy_workers_, workers_.size());
  DCHECK_GE(workers_.size(), min_threads_);
  DCHECK_LE(workers_.size(), max_threads_);

  // We create a new worker to handle the task _unless_ either 1) we're already
  // at the maximum number of threads, or 2) there are already enough idle
  // workers sitting around to take on this task (and all other pending tasks
  // that the idle workers haven't yet had a chance to pick up).
  if (workers_.size() >= max_threads_ ||
      task_queue_.size() <= workers_.size() - num_busy_workers_) {
    return;
  }

  scoped_ptr<WorkerThread> worker(new WorkerThread(this));
  if (worker->Start()) {
    workers_.insert(worker.release());
  } else {
    LOG(ERROR) << "Failed to start new worker thread.";
  }
}

}  // namespace mod_spdy
