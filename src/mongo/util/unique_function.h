/**
 *    Copyright 2018 MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#pragma once

#include <functional>
#include <type_traits>

#include "mongo/stdx/type_traits.h"

namespace mongo {
template <typename Function>
class unique_function;

/**
 * A `unique_function` is a move-only, type-erased functor object similar to `std::function`.
 * It is useful in situations where a functor cannot be wrapped in `std::function` objects because
 * it is incapable of being copied.  Often this happens with C++14 or later lambdas which capture a
 * `std::unique_ptr` by move.  The interface of `unique_function` is nearly identical to
 * `std::function`, except that it is not copyable, and provides no support for allocators.
 */
template <typename RetType, typename... Args>
class unique_function<RetType(Args...)> {
public:
    using result_type = RetType;

    ~unique_function() = default;
    unique_function() noexcept = default;

    unique_function(const unique_function&) = delete;
    unique_function& operator=(const unique_function&) = delete;

    unique_function(unique_function&&) noexcept = default;
    unique_function& operator=(unique_function&&) noexcept = default;

    template <
        typename Functor,
        typename = typename std::enable_if<stdx::is_invokable_r<RetType, Functor, Args...>::value,
                                           void>::type>
    unique_function(Functor &&functor) : impl(makeImpl(std::forward<Functor>(functor))) {}

    unique_function(std::nullptr_t) noexcept {}

    RetType operator()(Args... args) const {
        if (!*this)
            throw std::bad_function_call();
        return this->impl->call(std::forward<Args>(args)...);
    }

    explicit operator bool() const {
        return static_cast<bool>(this->impl);
    }

    // Needed to make `std::is_convertible<mongo::unique_function<...>, std::function<...>>`
    // be `std::false_type`.
    template <typename Any>
    operator std::function<Any>() = delete;

    friend bool operator==(const unique_function& lhs, std::nullptr_t) noexcept {
        return !lhs;
    }

    friend bool operator!=(const unique_function& lhs, std::nullptr_t) noexcept {
        return static_cast<bool>(lhs);
    }

    friend bool operator==(std::nullptr_t, const unique_function& rhs) noexcept {
        return !rhs;
    }

    friend bool operator!=(std::nullptr_t, const unique_function& rhs) noexcept {
        return static_cast<bool>(rhs);
    }

private:
    struct Impl {
        virtual ~Impl() = default;

        virtual RetType call(Args&&...) = 0;
    };

    template <typename Functor>
    static auto makeImpl(Functor functor) {
        class SpecificImpl : public Impl {
        private:
            Functor f;

        public:
            explicit SpecificImpl(Functor f) : f(std::move(f)) {}

            RetType call(Args&&... args) override {
                return f(std::forward<Args>(args)...);
            }
        };

        return std::make_unique<SpecificImpl>(std::move(functor));
    }

    template <typename Function>
    friend class shared_function;

    std::unique_ptr<Impl> impl;
};

}  // namespace mongo
