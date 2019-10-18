/**
 *    Copyright (C) 2019-present MongoDB, Inc.
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

#include <signal.h>

#include <cstddef>
#include <cstdint>
#include <set>
#include <thread>

namespace mongo::stdx {
namespace support {
/**
 * Describes a location of an alternate stack for use by threads, via `sigaltstack`.
 */
struct AltStackDescription {
    void* base = nullptr;
    std::size_t size = 0;
};

}  // namespace support

// Despite the fact that this is in the `testing` namespace, its intimate relationship
// with the `SignalStack` construct requires that it be defined here.
namespace testing {
/**
 * Information about a running thread for use in test programs.
 * It comes with an installable listener interface to permit tests to monitor thread information as
 * they need.
 */
struct ThreadInformation {
    support::AltStackDescription altStack;
	struct InfoGuard;

    class Listener {
    private:
        static inline std::set<Listener*> listeners;
		friend InfoGuard;

        /**
         * Notify all testing listeners that a new thread named by `id` has been created that is
         * described by `information`.
         */
        static void notifyNew(
                              const std::thread::id& id,
                              const ThreadInformation& information) {
            for (auto* const listener : listeners) {
                listener->born(id, information);
            }
        }

        /**
         * Notify all testing listeners that a thread with `id` has been retired.
         */
        static void notifyDelete( const std::thread::id& id) {
            for (auto* const listener : listeners) {
                listener->died(id);
            }
        }


    public:
        virtual ~Listener() = default;

        /**
         * A listener may perform any action it desires when notified that a new thread has been
         * created.  The `id` of the new thread and the `information` may be used in any way
         * desired.  This function is called in the context of the newly created thread.  The
         * `override` must provide its own thread safety, if necessary.
         */
        virtual void born(const std::thread::id& id, const ThreadInformation& information) = 0;

        /**
         * A listener may perform any action it desires when notified that a thread is being
         * retired.  The `id` of the expired thread may be used in any way desired.  This function
         * is called in the context of the dying thread.  The `override` must provide its own thread
         * safety, if necessary.  The none of the resources directly owned by the thread's
         * initialization code have been released yet, when this function is called.
         */
        virtual void died(const std::thread::id&) = 0;


        /**
         * Remove the `deadListener` from the set of testing listeners for thread events.  This
         * function is not threadsafe.
         */
        static void remove(Listener& deadListener) {
            listeners.erase(&deadListener);
        }

        /**
         * Add the `newListener` to the set of testing listeners for thread events.  This function
         * is not threadsafe.
         */
        static void add(Listener& newListener) {
            listeners.insert(&newListener);
        }
    };

    /**
     * An RAII type to automatically register, with any listeners, and deregister a thread's
     * `SignalStack` information on creation and retire it on expiry.
     */
    struct InfoGuard {
        InfoGuard(const InfoGuard&) = delete;

        explicit InfoGuard(const testing::ThreadInformation& info) {
            testing::ThreadInformation::Listener::notifyNew(
                 std::this_thread::get_id(), info);
        }

        ~InfoGuard() {
            testing::ThreadInformation::Listener::notifyDelete(
                                                               std::this_thread::get_id());
        }
    };


    /**
     * A default `Registrar` implementation for thread events, intended for testing.
     * The definition is in `src/mongo/stdx/testing/thread_helpers.h`, as it is not used unless
     * explicitly installed.
     */
    class Registrar;
};
}  // namespace testing

namespace support {
/**
 * Represents an alternate stack to be installed for handling signals.  On platforms which do not
 * support `sigaltstack`, this class has a dummy implementation.
 */
class SignalStack {
private:
#if defined(__linux__) || defined(__FreeBSD__)  // Support `sigaltstack` on the specified platforms.
private:
    static constexpr auto kSize = std::max(std::size_t{65536}, std::size_t{MINSIGSTKSZ});
    std::unique_ptr<std::byte[]> _stack = std::make_unique<std::byte[]>(kSize);

    const void* _allocation() const {
        return this->_stack.get();
    }

    std::size_t _size() const {
        return kSize;
    }

public:
    static constexpr bool kEnabled = true;

    /**
     * Install this stack as a `sigaltstack`, and return a management object to revert back to no
     * `sigaltstack`, when that object expires.
     */
    [[nodiscard]] auto installStack() const {
        /**
         * An RAII type to register and deregister a `sigaltstack`, as specified to its constructor.
         */
        struct StackGuard {
            StackGuard(const StackGuard&) = delete;

            explicit StackGuard(const support::AltStackDescription& altStack) {
                stack_t stack;
                stack.ss_sp = altStack.base;
                stack.ss_size = altStack.size;
                stack.ss_flags = 0;
                const int result = sigaltstack(&stack, nullptr);
                if (result != 0) {
                    abort();  // Can't invoke the logging system here -- too low in the
                              // implementation stack.
                }
            }

            ~StackGuard() {
                stack_t stack;
                stack.ss_flags = SS_DISABLE;
                const int result = sigaltstack(&stack, nullptr);
                if (result != 0) {
                    abort();  // Can't invoke the logging system here -- too low in the
                              // implementation stack.
                }
            }
        };

        // When installing a new thread, if using `sigaltstack`s we must RAII guard both the testing
        // information for listeners and the actual stack itself.  Combining these into a single
        // type makes this installation function simpler.
        return StackGuard({this->_stack.get(), this->_size()});
    }

#else   // No `sigaltstack` support

public:
    static constexpr bool kEnabled = false;

    /**
     * This function is the non-`sigaltstack` form of installing a stack.  The thread creation and
     * destruction events will be broadcast to listeners; however, no actual stacks will be
     * installed.  A nullptr and 0 size for `AltStack` information indicates this to listeners.
     */
    [[nodiscard]] auto installStack() const {
		struct Guard {
			Guard( const Guard & )= delete;
			~Guard() {}
		};
        return Guard{};
    }
#endif  // End of conditional support for `sigaltstack`
};

}  // namespace support
}  // namespace mongo::stdx
