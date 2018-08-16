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

    ~unique_function() noexcept = default;
    unique_function() noexcept = default;

    unique_function(const unique_function&) = delete;
    unique_function& operator=(const unique_function&) = delete;

    unique_function(unique_function&&) noexcept = default;
    unique_function& operator=(unique_function&&) noexcept = default;


    // TODO: Look into creating a mechanism based upon a shared_ptr to `void *`-like state, and a
    // `void *` accepting function object.  This will permit reusing the core impl object when
    // converting between related function types, such as
    // `int (std::string)` -> `void (const char *)`
    template <typename Functor,
              typename = typename std::
                  enable_if<stdx::is_invokable_r<RetType, Functor, Args...>::value, void>::type,
              typename = typename std::enable_if<std::is_move_constructible<Functor>::value>::type>
    unique_function(Functor&& functor) noexcept(noexcept(makeImpl(std::forward<Functor>(functor))))
        : impl(makeImpl(std::forward<Functor>(functor))) {}

    unique_function(std::nullptr_t) noexcept {}

    RetType operator()(Args... args) const {
        class bad_unique_function_call : public std::bad_function_call {};
        if (!*this) {
            throw bad_unique_function_call();
        }
        return this->impl->call(std::forward<Args>(args)...);
    }

    explicit operator bool() const noexcept {
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
        virtual ~Impl() noexcept = default;

        virtual RetType call(Args&&...) = 0;
    };


    // We assume that allocations do not fail by throwing, so we have marked the `makeImpl` function
    // `noexcept`, as long as the move construction of the function object being passed is also
    // `noexcept`.  This ripples out to the accepting template constructor.
    template <typename Functor>
    static auto makeImpl_impl(Functor&& functor, std::false_type) noexcept(
        noexcept(typename std::remove_reference<Functor>::type{std::move(functor)})) {
        class SpecificImpl : public Impl {
        private:
            Functor f;

        public:
            explicit SpecificImpl(Functor&& func) noexcept(
                noexcept(typename std::remove_reference<Functor>::type{std::move(func)}))
                : f(std::move(func)) {}

            RetType call(Args&&... args) override {
                return f(std::forward<Args>(args)...);
            }
        };

        return std::make_unique<SpecificImpl>(std::move(functor));
    }


    // We assume that allocations do not fail by throwing, so we have marked the `makeImpl` function
    // `noexcept`, as long as the move construction of the function object being passed is also
    // `noexcept`.  This ripples out to the accepting template constructor.
    // This overload is needed to squelch problems in the `T ()` -> `void ()` case.
    template <typename Functor>
    static auto makeImpl_impl(Functor&& functor, std::true_type) noexcept(
        noexcept(typename std::remove_reference<Functor>::type{std::move(functor)})) {
        class SpecificImpl : public Impl {
        private:
            Functor f;

        public:
            explicit SpecificImpl(Functor&& func) noexcept(
                noexcept(typename std::remove_reference<Functor>::type{std::move(func)}))
                : f(std::move(func)) {}

            void call(Args&&... args) override {
                (void)f(std::forward<Args>(args)...);
            }
        };

        return std::make_unique<SpecificImpl>(std::move(functor));
    }

    template <typename Functor>
    static constexpr auto selectCase(Functor&& f) noexcept {
        constexpr bool kVoidCase = stdx::conjunction<
            std::is_void<RetType>,
            stdx::negation<std::is_void<typename std::result_of<Functor(Args...)>::type>>>::value;
        using selected_case = stdx::bool_constant<kVoidCase>;
        return selected_case{};
    }

    template <typename Functor>
    static auto makeImpl(Functor&& functor) noexcept(noexcept(makeImpl_impl(
        std::forward<Functor>(functor), selectCase(std::forward<Functor>(functor))))) {
        return makeImpl_impl(std::forward<Functor>(functor),
                             selectCase(std::forward<Functor>(functor)));
    }

    std::unique_ptr<Impl> impl;
};

}  // namespace mongo
