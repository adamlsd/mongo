/**
 *    Copyright (C) 2017 10gen Inc.
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
#include <memory>
#include <utility>

#include <boost/noncopyable.hpp>

#include "mongo/base/scoped_raii.h"

namespace mongo {
namespace unique_raii_detail {
class UniqueRAIIScopeGuardBase : boost::noncopyable {
protected:
    mutable bool active_ = false;

    inline bool active() const noexcept {
        return this->active_;
    }
    inline void disable() noexcept {
        this->active_ = false;
    }

    explicit UniqueRAIIScopeGuardBase(const bool a) : active_(a) {}

public:
    void Dismiss() const noexcept {
        this->active_ = false;
    }
};
}  // namespace unique_raii_detail

/**
 * The `UniqueRAII` is a facility to create ad-hoc classes for scoped resource management using
 * RAII.  The `UniqueRAII` type internally stores a user-specified function to be called in its
 * destructor.  The `UniqueRAII` type is capable of impersonating either a pointer or a value type.
 * A `UniqueRAII` is constructed from two C++ callable function-like entities (functions, bind
 * expressions, or lambdas will all do).  The first callable will be invoked by the constructor to
 * create a new instance of the specified type.  The second callable will be captured by the
 * constructor to be invoked later by the destructor to free the resources associated with the
 * specified type.  Unlike the `ScopedRAII` type, a `UniqueRAII` requires specification of the
 * destructor function's type.  This facilitates faster invocation of the destructor and potential
 * inlining benefits.
 *
 * This set of facilities makes `UniqueRAII` useful for quickly adapting C++ "wrappers" around C
 * libraries which give out resources to be managed.  The `makeUniqueRAII` function should be used
 * to create `UniqueRAII` objects.  For example:
 *
 * ~~~
 * void stdioExample() {
 *     auto file = makeUniqueRAII([]{ return fopen("datafile.txt", "wt"); }, fclose);
 *     fprintf(file, "Hello World!\n");
 * }
 * ~~~
 * In the above example, the file represented by `file` will be automatically closed when it goes
 * out of scope.  The `UniqueRAII` type permits no reassignment to a `file`, but the object can be
 * moved to another new instance.
 *
 * `UniqueRAII` types can represent any type.  Unix file descriptors are raw integers.  `UniqueRAII`
 * can adapt integers to wrap Unix file IO.
 *
 * ~~~
 * void unixExample() {
 *     auto file = makeUniqueRAII([]{ return open("datafile.txt", O_RDWR); }, close);
 *     const std::string message = "Hello World!\n";
 *     write(file, message.c_str(), message.size());
 * }
 * ~~~
 * In the above example, the Unix file descriptor represented by `file` will be automatically closed
 * when it goes out of scope.
 *
 * `UniqueRAII` is assignable, using similar semantics to `std::unique_ptr`as long as the
 * destruction functioni of the source and destination are of identical type.
 *
 * `UniqueRAII` is intended as a better replacement for many use cases of `ScopeGuard`.
 * `ScopeGuard` is not a resource owning object, it is merely a hook to indicate a particular piece
 * of code should run on exiting a scope.  Nearly all use cases for `ScopeGuard` are resource
 * management idioms which would benefit from more explicit grouping between the resource being
 * managed and its retirement scheme.
 */
template <typename T, typename Dtor>
class UniqueRAII : boost::noncopyable {
private:
    Dtor dtor;
    T resource;

    bool active_;

    inline bool active() const noexcept {
        return this->active_;
    }
    inline void disable() noexcept {
        this->active_ = false;
    }


public:
    ~UniqueRAII() try {
        if (this->active_)
            this->dtor(this->resource);
    } catch (...) {
        return;
    }

    UniqueRAII(UniqueRAII&& copy)
        : dtor(std::move(copy.dtor)), resource(std::move(copy.resource)), active_() {
        using std::swap;
        swap(this->active_, copy.active_);
    }

    template <typename Ctor, typename D>
    explicit UniqueRAII(Ctor c, D d) : dtor(std::move(d)), resource(c()), active_(true) {}

    inline operator const T&() const {
        return this->resource;
    }
};

template <typename Dtor>
class UniqueRAII<void, Dtor> : public unique_raii_detail::UniqueRAIIScopeGuardBase {
private:
    Dtor dtor;

public:
    ~UniqueRAII() try {
        if (this->active())
            this->dtor();
    } catch (...) {
        return;
    }

    UniqueRAII(UniqueRAII&& copy) : UniqueRAIIScopeGuardBase(false), dtor(std::move(copy.dtor)) {
        using std::swap;
        swap(this->active_, copy.active_);
    }

    template <typename Ctor, typename D>
    explicit UniqueRAII(Ctor c, D d)
        : unique_raii_detail::UniqueRAIIScopeGuardBase(true), dtor(std::move(d)) {
        c();
    }
};

template <typename T, typename Dtor>
class UniqueRAII<T*, Dtor> : boost::noncopyable {
private:
    Dtor dtor;
    T* resource;

    bool active_;

    inline bool active() const noexcept {
        return this->active_;
    }
    inline void disable() noexcept {
        this->active_ = false;
    }


public:
    ~UniqueRAII() try {
        if (this->active_)
            this->dtor(this->resource);
    } catch (...) {
        return;
    }

    UniqueRAII(UniqueRAII&& copy)
        : dtor(std::move(copy.dtor)), resource(std::move(copy.resource)), active_() {
        using std::swap;
        swap(this->active_, copy.active_);
    }

    template <typename Ctor, typename D>
    explicit UniqueRAII(Ctor c, D d) : dtor(std::move(d)), resource(c()), active_(true) {}

    inline operator T*() const {
        return this->resource;
    }

    inline T& operator*() const {
        return *this->resource;
    }

    inline T* operator->() const {
        return this->resource;
    }
};

/**
 * Returns a new instance of a `UniqueRAII`.  It is constructed from the specified `Ctor` and `Dtor`
 * parameters.
 */
template <typename Ctor, typename Dtor>
MONGO_FUNCTION_NODISCARD inline auto makeUniqueRAII(Ctor c, Dtor d) {
    return UniqueRAII<decltype(c()), Dtor>(std::move(c), std::move(d));
}
}  // namespace mongo
