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

#include "mongo/base/clonable_ptr.h"
#include "mongo/stdx/type_traits.h"

namespace mongo {
template <typename Function>
class disposable_function;

template <typename Function>
class unique_function;

template <typename Function>
class shared_function;

template <typename Function>
class clonable_function;

/**
 * A `disposable_function` is a move-only, type-erased functor object similar to `std::function`.
 * It is useful in situations where a functor cannot be wrapped in `std::function` objects because
 * it is incapable of being copied.  Often this happens with C++14 or later lambdas which capture a
 * `std::unique_ptr` by move.  The interface of `disposable_function` is nearly identical to
 * `std::function`, except that it is not copyable, and provides no support for allocators.
 * Additionally, a `disposable_function` can only be called once.
 */
template <typename RetType, typename... Args>
class disposable_function<RetType(Args...)> {
public:
    using result_type = RetType;

    ~disposable_function() = default;
    disposable_function() noexcept = default;

    disposable_function(const disposable_function&) = delete;
    disposable_function& operator=(const disposable_function&) = delete;

    disposable_function(disposable_function&&) noexcept = default;
    disposable_function& operator=(disposable_function&&) noexcept = default;

    template <
        typename Functor,
        typename = typename std::enable_if<stdx::is_invokable_r<RetType, Functor, Args...>::value,
                                           void>::type,
        typename =
            typename std::enable_if<!std::is_same<disposable_function, Functor>::value, void>::type>
    disposable_function(Functor functor) : impl(makeImpl(std::move(functor))) {}

    template <typename FuncRetType, typename... FuncArgs>
    disposable_function(std::function<FuncRetType(FuncArgs...)> functor)
        : impl(makeImpl(std::move(functor))) {}

    disposable_function(std::nullptr_t) noexcept {}

    disposable_function(unique_function<RetType(Args...)>&& func);

    disposable_function(shared_function<RetType(Args...)>&& func);

    disposable_function(clonable_function<RetType(Args...)>&& func);

    RetType operator()(Args... args)  // && //(TODO!)
    {
        if (!*this)
            throw std::bad_function_call();
        disposable_function tmp = std::move(*this);
        return tmp.impl->call(std::forward<Args>(args)...);
    }

    explicit operator bool() const {
        return static_cast<bool>(this->impl);
    }

    template <typename Any>
    operator std::function<Any>() = delete;

    friend bool operator==(const disposable_function& lhs, std::nullptr_t) noexcept {
        return !lhs;
    }

    friend bool operator!=(const disposable_function& lhs, std::nullptr_t) noexcept {
        return static_cast<bool>(lhs);
    }

    friend bool operator==(std::nullptr_t, const disposable_function& rhs) noexcept {
        return !rhs;
    }

    friend bool operator!=(std::nullptr_t, const disposable_function& rhs) noexcept {
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
    friend class unique_function;

    template <typename Function>
    friend class shared_function;

    template <typename Function>
    friend class clonable_function;

    std::unique_ptr<Impl> impl;
};

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
                                           void>::type,
        typename =
            typename std::enable_if<!std::is_same<unique_function, Functor>::value, void>::type>
    unique_function(Functor functor) : impl(makeImpl(std::move(functor))) {}

    template <typename FuncRetType, typename... FuncArgs>
    unique_function(std::function<FuncRetType(FuncArgs...)> functor)
        : impl(makeImpl(std::move(functor))) {}

    unique_function(std::nullptr_t) noexcept {}

    // One should not be able to move or copy a shared function into a unique function -- this
    // probably indicates an error.  Wrapping the shared function in a lambda is an easy way to get
    // around this.
    unique_function(shared_function<RetType(Args...)>&& func) = delete;
    unique_function(const shared_function<RetType(Args...)>& func) = delete;

    unique_function(clonable_function<RetType(Args...)>&& func);

    RetType operator()(Args... args) const {
        if (!*this)
            throw std::bad_function_call();
        return this->impl->call(std::forward<Args>(args)...);
    }

    explicit operator bool() const {
        return static_cast<bool>(this->impl);
    }

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
    struct Impl : disposable_function<RetType(Args...)>::Impl {};

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
    friend class disposable_function;

    template <typename Function>
    friend class shared_function;

    template <typename Function>
    friend class clonable_function;

    std::unique_ptr<Impl> impl;
};

/**
 * A `shared_function` is a copyable, type-erased functor object similar to `std::function`.
 * It is useful in situations where a functor cannot be wrapped in `std::function` objects because
 * it is incapable of being copied.  Often this happens with C++14 or later lambdas which capture a
 * `std::unique_ptr` by move.  The interface of `shared_function` is nearly identical to
 * `std::function`, except that it provides no support for allocators.  Unlike `unique_function`,
 * `shared_function` objects are copyable, but all copies refer to the exact same underlying functor
 * object instance, just like a `std::shared_ptr`.  `unique_function` objects can be moved into
 * `shared_function` objects, in the same way that `std::unique_ptr` objects can be moved by
 * construction or assignment into `std::shared_ptr` objects.
 */
template <typename RetType, typename... Args>
class shared_function<RetType(Args...)> {
public:
    using result_type = RetType;

    ~shared_function() = default;
    shared_function() noexcept = default;

    shared_function(const shared_function&) = default;
    shared_function& operator=(const shared_function&) = default;

    shared_function(shared_function&&) noexcept = default;
    shared_function& operator=(shared_function&&) noexcept = default;

    template <
        typename Functor,
        typename = typename std::enable_if<stdx::is_invokable_r<RetType, Functor, Args...>::value,
                                           void>::type,
        typename =
            typename std::enable_if<!std::is_same<shared_function, Functor>::value, void>::type>
    shared_function(Functor functor) : impl(makeImpl(std::move(functor))) {}

    template <typename FuncRetType, typename... FuncArgs>
    shared_function(std::function<FuncRetType(FuncArgs...)>&& functor)
        : impl(makeImpl(std::move(functor))) {}

    shared_function(unique_function<RetType(Args...)>&& functor) : impl(std::move(functor.impl)) {}

    shared_function(disposable_function<RetType(Args...)>&& functor) = delete;
    shared_function(const disposable_function<RetType(Args...)>& functor) = delete;


    shared_function(clonable_function<RetType(Args...)>&& functor);

    shared_function(std::nullptr_t) noexcept {}

    RetType operator()(Args... args) const {
        if (!*this)
            throw std::bad_function_call();
        return this->impl->call(std::forward<Args>(args)...);
    }

    explicit operator bool() const {
        return static_cast<bool>(this->impl);
    }

    friend bool operator==(const shared_function& lhs, std::nullptr_t) noexcept {
        return !lhs;
    }

    friend bool operator!=(const shared_function& lhs, std::nullptr_t) noexcept {
        return static_cast<bool>(lhs);
    }

    friend bool operator==(std::nullptr_t, const shared_function& rhs) noexcept {
        return !rhs;
    }

    friend bool operator!=(std::nullptr_t, const shared_function& rhs) noexcept {
        return static_cast<bool>(rhs);
    }

private:
    using companion = unique_function<RetType(Args...)>;
    using Impl = typename companion::Impl;

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

        // We use `make_shared` for `shared_function` Impl creation, to avoid the extra allocation
        // for the ref-counting data.
        return std::make_shared<SpecificImpl>(std::move(functor));
    }

    template <typename Function>
    friend class clonable_function;

    std::shared_ptr<Impl> impl;
};

template <typename RetType, typename... Args>
class clonable_function<RetType(Args...)> {
private:
    using companion = unique_function<RetType(Args...)>;
    class Impl : public companion::Impl {
    public:
        virtual std::unique_ptr<Impl> clone() const = 0;
    };

    template <typename Functor>
    static auto makeImpl(Functor functor) {
        class SpecificImpl : public Impl {
        private:
            Functor f;

        public:
            explicit SpecificImpl(Functor f) : f(std::move(f)) {}

            std::unique_ptr<Impl> clone() const override {
                return std::make_unique<SpecificImpl>(*this);
            }

            RetType call(Args&&... args) override {
                return (RetType)f(std::forward<Args>(args)...);
            }
        };

        return std::make_unique<SpecificImpl>(std::move(functor));
    }

    template <typename Function>
    friend class unique_function;

    template <typename Function>
    friend class shared_function;

    clonable_ptr<Impl> impl;

public:
    using result_type = RetType;

    ~clonable_function() = default;
    clonable_function() noexcept = default;

    clonable_function(const clonable_function&) = default;
    clonable_function& operator=(const clonable_function&) = default;

    clonable_function(clonable_function&&) noexcept = default;
    clonable_function& operator=(clonable_function&&) noexcept = default;

    template <typename Functor,
              typename = typename std::
                  enable_if<stdx::is_invokable_r<result_type, Functor, Args...>::value, void>::type,
              typename = typename std::enable_if<!std::is_same<clonable_function, Functor>::value,
                                                 void>::type>
    clonable_function(Functor functor) : impl(makeImpl(std::move(functor))) {}

    template <typename FuncRetType, typename... FuncArgs>
    clonable_function(std::function<FuncRetType(FuncArgs...)> functor)
        : impl(makeImpl(std::move(functor))) {}

    clonable_function(unique_function<RetType(Args...)>&&) = delete;
    clonable_function(const unique_function<RetType(Args...)>&) = delete;

    clonable_function(shared_function<RetType(Args...)>&&) = delete;
    clonable_function(const shared_function<RetType(Args...)>&) = delete;

    clonable_function(std::nullptr_t) noexcept {}

    RetType operator()(Args... args) const {
        if (!*this)
            throw std::bad_function_call();
        return this->impl->call(std::forward<Args>(args)...);
    }

    explicit operator bool() const {
        return static_cast<bool>(this->impl);
    }

    friend bool operator==(const clonable_function& lhs, std::nullptr_t) noexcept {
        return !lhs;
    }

    friend bool operator!=(const clonable_function& lhs, std::nullptr_t) noexcept {
        return static_cast<bool>(lhs);
    }

    friend bool operator==(std::nullptr_t, const clonable_function& rhs) noexcept {
        return !rhs;
    }

    friend bool operator!=(std::nullptr_t, const clonable_function& rhs) noexcept {
        return static_cast<bool>(rhs);
    }
};

template <typename RetType, typename... Args, template <typename> class ErasedFunctor>
auto wrapShared(ErasedFunctor<RetType(Args...)>&& f) {
    return shared_function<RetType(Args...)>(std::move(f));
}


template <typename RetType, typename... Args>
disposable_function<RetType(Args...)>::disposable_function(unique_function<RetType(Args...)>&& func)
    : impl(std::move(func.impl)) {}

template <typename RetType, typename... Args>
disposable_function<RetType(Args...)>::disposable_function(shared_function<RetType(Args...)>&& func)
    : impl(std::move(func.impl)) {}

template <typename RetType, typename... Args>
disposable_function<RetType(Args...)>::disposable_function(
    clonable_function<RetType(Args...)>&& func)
    : impl(std::move(func.impl)) {}

template <typename RetType, typename... Args>
unique_function<RetType(Args...)>::unique_function(clonable_function<RetType(Args...)>&& func)
    : impl(std::move(func.impl)) {}

template <typename RetType, typename... Args>
shared_function<RetType(Args...)>::shared_function(clonable_function<RetType(Args...)>&& func)
    : impl(std::move(func.impl)) {}
}  // namespace mongo
