/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <ctime>
#include <exception>
#include <set>
#include <thread>
#include <type_traits>

#include "mongo/stdx/exception.h"
#include "mongo/stdx/support/signal_stack.h"

namespace mongo::stdx {

/**
 * We're wrapping std::thread here, rather than aliasing it, because we'd like to augment some
 * implicit non-observable behviours into std::thread.  The resulting type should be identical in
 * all observable ways to the original, but it should terminate if a new thread cannot be allocated,
 * it should handle `set_terminate` semantics correctly, and it should implicitly allocate a
 * `sigaltstack` when starting.  We'd like this behavior because we rarely if ever try/catch thread
 * creation, and don't have a strategy for retrying.  Therefore, all throwing does is remove context
 * as to which part of the system failed thread creation (as the exception itself is caught at the
 * top of the stack).  The `sigaltstack` provides, among other things, the ability to attempt to run
 * stack symbolization code when a thread overflows its stack.
 *
 * We're putting this in stdx, rather than having it as some kind of mongo::Thread, because the
 * signature and use of the type is otherwise completely identical.
 *
 * We implement this with private inheritance to minimize the overhead of our wrapping and to
 * simplify the implementation.
 */
class thread : private ::std::thread {
private:
    /*
     * NOTE: The `Function f` parameter must be taken by value, not reference or forwarding
     * reference, as it is used on the far side of the thread launch, and this ctor has to properly
     * transfer ownership to the far side's thread.
     */
    template <typename Function, typename... Args>
    static ::std::thread createThread(Function&& f, Args&&... args) noexcept {
        return ::std::thread([
            signalStack = support::SignalStack{},
            f = std::decay_t<Function>{std::forward<Function>(f)},
            pack = std::make_tuple(std::forward<Args>(args)...)
        ]() mutable noexcept {
#if defined(_WIN32)
            // On Win32 we have to set the terminate handler per thread.
            // We set it to our universal terminate handler, which people can register via the
            // `stdx::set_terminate` hook.

            // Installation of the terminate handler support mechanisms should happen before
            // `sigaltstack` installation, as the terminate semantics are implemented at a lower
            // level.
            ::std::set_terminate(::mongo::stdx::TerminateHandlerDetailsInterface::dispatch);
#endif

            auto guard = signalStack.installStack();
            return std::apply(std::move(f), std::move(pack));
        });
    }

public:
    using ::std::thread::id;
    using ::std::thread::native_handle_type;

    thread() noexcept = default;

    ~thread() noexcept = default;
    thread(const thread&) = delete;
    thread(thread&& other) noexcept = default;
    thread& operator=(const thread&) = delete;
    thread& operator=(thread&& other) noexcept = default;

    /**
     * As of C++14, the Function overload for std::thread requires that this constructor only
     * participate in overload resolution if std::decay_t<Function> is not the same type as thread.
     * That prevents this overload from intercepting calls that might generate implicit conversions
     * before binding to other constructors (specifically move/copy constructors).
     */
    template <class Function,
              class... Args,
              std::enable_if_t<!std::is_same_v<thread, std::decay_t<Function>>, int> = 0>
    explicit thread(Function&& f, Args&&... args) noexcept
        : std::thread(createThread(std::forward<Function>(f), std::forward<Args>(args)...)) {}

    using ::std::thread::get_id;
    using ::std::thread::hardware_concurrency;
    using ::std::thread::joinable;
    using ::std::thread::native_handle;

    using ::std::thread::detach;
    using ::std::thread::join;

    void swap(thread& other) noexcept {
        this->::std::thread::swap(other);
    }
};


inline void swap(thread& lhs, thread& rhs) noexcept {
    lhs.swap(rhs);
}

namespace this_thread {
using std::this_thread::get_id;
using std::this_thread::yield;

#ifdef _WIN32
using std::this_thread::sleep_for;
using std::this_thread::sleep_until;
#else
template <class Rep, class Period>
inline void sleep_for(const std::chrono::duration<Rep, Period>& sleep_duration) {
    if (sleep_duration <= sleep_duration.zero())
        return;

    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(sleep_duration);
    const auto nanoseconds =
        std::chrono::duration_cast<std::chrono::nanoseconds>(sleep_duration - seconds);
    struct timespec sleepVal = {static_cast<std::time_t>(seconds.count()),
                                static_cast<long>(nanoseconds.count())};
    struct timespec remainVal;
    while (nanosleep(&sleepVal, &remainVal) == -1 && errno == EINTR) {
        sleepVal = remainVal;
    }
}

template <class Clock, class Duration>
void sleep_until(const std::chrono::time_point<Clock, Duration>& sleep_time) {
    const auto now = Clock::now();
    sleep_for(sleep_time - now);
}
#endif
}  // namespace this_thread

}  // namespace mongo::stdx

static_assert(std::is_move_constructible_v<mongo::stdx::thread>);
static_assert(std::is_move_assignable_v<mongo::stdx::thread>);
