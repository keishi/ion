/**
Copyright 2016 Google Inc. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS-IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

*/

#include "ion/port/barrier.h"

#if defined(ION_PLATFORM_WINDOWS)
#  include <assert.h>  // For checking return values since port has no logging.
#  include <algorithm>  // for swap
#endif

namespace ion {
namespace port {

//-----------------------------------------------------------------------------
//
// Linux and QNX are the only platforms that support barriers in pthreads. Both
// the windows and non-Linux/QNX implementations below may seem rather complex.
// This is because they guard against two potential errors:
//   - Deadlock between a wait and a broadcast/set event, ensuring that all
//     threads have entered the wait branch before the broadcast.
//   - An issue where the Barrier destructor is called before all threads are
//     done using the synchronization objects, causing an intermittent crash.
//     For example, a mutex may be destroyed while it is still being held by
//     pthread_cond_wait(), or a handle can be reset after it has been closed
//     in the destructor.
//
//-----------------------------------------------------------------------------

#if defined(ION_PLATFORM_WINDOWS)
//-----------------------------------------------------------------------------
//
// Windows version.
//
// Windows 8 introduces real barrier synchronization functions
// (InitializeSynchronizationBarrier, EnterSynchronizationBarrier, and
// DeleteSynchronizationBarrier). Unfortunately, we're stuck with older APIs,
// which don't have great alternatives.
//
// Our solution is based on sections 3.6.5-3.6.7 of The Little Book of
// Semaphores by Allen B. Downey.
//   http://greenteapress.com/semaphores/downey08semaphores.pdf
//
// We use a turnstile to wait until all the threads have arrived, then we use a
// second turnstile to make sure they've all passed through and that the first
// is ready for re-use.
//
//-----------------------------------------------------------------------------

Barrier::Barrier(uint32 thread_count)
    : thread_count_(thread_count),
      wait_count_(0),
      turnstile1_(NULL),
      turnstile2_(NULL),
      is_valid_(thread_count_ > 0) {
  if (IsValid()) {
    turnstile1_ = CreateSemaphore(NULL, 0, thread_count_, NULL);
    turnstile2_ = CreateSemaphore(NULL, 0, thread_count_, NULL);
  }
}

Barrier::~Barrier() {
  if (IsValid()) {
    // After fairly intensive testing, it appears that this is safe to do
    // without ensuring that all threads have left WaitForSingleObject(). If
    // this proves not to be the case, then we may have to wait on an additional
    // semaphore here, similar to the non-barrier POSIX implementation, below.
    CloseHandle(turnstile2_);
    CloseHandle(turnstile1_);
  }
}

void Barrier::Wait() {
  if (IsValid() && thread_count_ > 1) {
    // Wait for all the threads to come in.
    WaitInternal(1, thread_count_, turnstile1_);

    // And then wait for them all to go out, which ensures that the barrier is
    // ready for another round.
    WaitInternal(-1, 0, turnstile2_);
  }
}

void Barrier::WaitInternal(int32 increment, int32 limit, HANDLE turnstile) {
  if ((wait_count_ += increment) == limit) {
    // Last thread is in.  Release the hounds.
    BOOL status = ReleaseSemaphore(turnstile, thread_count_ - 1, NULL);
    (void)status;  // Avoid warnings when the assertion is optimized out.
    assert(status != 0);
  } else {
    DWORD status = WaitForSingleObject(turnstile, INFINITE);
    (void)status;  // Avoid warnings when the assertion is optimized out.
    assert(status == WAIT_OBJECT_0);
  }
}

#elif defined(ION_PLATFORM_LINUX) || defined(ION_PLATFORM_QNX)
//-----------------------------------------------------------------------------
//
// Barrier pthreads version.
//
//-----------------------------------------------------------------------------

Barrier::Barrier(uint32 thread_count)
    : waiting_count_(0),
      is_valid_(!pthread_barrier_init(&barrier_, NULL, thread_count)) {}

Barrier::~Barrier() {
  if (IsValid()) {
    // Block until no thread is in the Wait() method, meaning we know that
    // pthread_barrier_wait() has returned.
    while (waiting_count_ != 0) {}
    pthread_barrier_destroy(&barrier_);
  }
}

void Barrier::Wait() {
  if (IsValid()) {
    ++waiting_count_;
    pthread_barrier_wait(&barrier_);
    --waiting_count_;
  }
}

#else
//-----------------------------------------------------------------------------
//
// Non-barrier pthreads version.
//
// Pthread barriers are an optional part of the Posix spec, and Mac, iOS, and
// Android do not support them, so this version is used on those platforms.
//
//-----------------------------------------------------------------------------

Barrier::Barrier(uint32 thread_count)
    : thread_count_(thread_count),
      wait_count_(0),
      exit_count_(1),
      is_valid_(thread_count_ > 0) {
  if (IsValid()) {
    pthread_cond_init(&condition_, NULL);
    pthread_cond_init(&exit_condition_, NULL);
    pthread_mutex_init(&mutex_, NULL);
  }
}

Barrier::~Barrier() {
  if (IsValid()) {
    pthread_mutex_lock(&mutex_);
    if (thread_count_ > 1 && --exit_count_) {
      // Wait until the last thread has exited Wait().
      pthread_cond_wait(&exit_condition_, &mutex_);
    }
    pthread_mutex_unlock(&mutex_);
    pthread_cond_destroy(&condition_);
    pthread_cond_destroy(&exit_condition_);
    pthread_mutex_destroy(&mutex_);
  }
}

void Barrier::Wait() {
  if (IsValid()) {
    pthread_mutex_lock(&mutex_);
    // Add 1 to the wait count and see if this reaches the barrier limit.
    if (++wait_count_ == thread_count_) {
      wait_count_ = 0;
      exit_count_ = thread_count_ + 1;
      pthread_cond_broadcast(&condition_);
    } else {
      // Wait for the last thread to wait.
      pthread_cond_wait(&condition_, &mutex_);
      // When the thread exits the wait it will have the mutex locked.
    }

    // If the destructor has already been entered and this is the last thread
    // then the return value of the decrement will be 0. Signal the condition to
    // allow the destructor to proceed.
    if (--exit_count_ == 0)
      pthread_cond_broadcast(&exit_condition_);
    pthread_mutex_unlock(&mutex_);
  }
}

#endif

}  // namespace port
}  // namespace ion
