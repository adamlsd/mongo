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

namespace mongo {
namespace scoped_raii_detail {
struct Na;

template <typename T>
struct select_dtor {
    using type = std::function<void(T)>;
};

template <>
struct select_dtor<Na> {
    using type = std::function<void()>;
};
}  // namespace raii_detail

/**
 * The `ScopedRAII` is a facility to create ad-hoc classes for scoped resource management using
 * RAII.  The `ScopedRAII` type internally stores a user-specified function to be called in its
 * destructor.  The `ScopedRAII` type is capable of impersonating either a pointer or a value type.
 * A `ScopedRAII` is constructed from two C++ callable function-like entities (functions, bind
 * expressions, or lambdas will all do).  The first callable will be invoked by the constructor to
 * create a new instance of the specified type.  The second callable will be captured by the
 * constructor to be invoked later by the destructor to free the resources associated with the
 * specified type.
 *
 * This set of facilities makes `ScopedRAII` useful for quickly adapting C++ "wrappers" around C
 * libraries which give out resources to be managed.  For example:
 *
 * ~~~
 * void stdioExample() {
 *     ScopedRAII<FILE*> file([]{ return fopen("datafile.txt", "wt"); }, fclose);
 *     fprintf(file, "Hello World!\n");
 * }
 * ~~~
 * In the above example, the file represented by `file` will be automatically closed when it goes
 * out of scope.  The `ScopedRAII` type prevents accidental reassignment to a `file`, to avoid
 * resource leakage.  `ScopedRAII` types are intended to be fire-and-forget.
 *
 * `ScopedRAII` types can represent any type.  Unix file descriptors are raw integers.  `ScopedRAII`
 * can adapt integers to wrap Unix file IO.
 *
 * ~~~
 * void unixExample() {
 *     ScopedRAII<int> file([]{ return open("datafile.txt", O_RDWR); }, close);
 *     const std::string message = "Hello World!\n";
 *     write(file, message.c_str(), message.size());
 * }
 * ~~~
 * In the above example, the Unix file descriptor represented by `file` will be automatically closed
 * when it goes out of scope.
 *
 * `ScopedRAII` is not assignable, as exact semantics of lifetime management during assignment can
 * vary -- shared resource, unique resource, etc.  `ScopedRAII` managed objects have their lifetime
 * permanently bound to the scope in which their owner lives.
 *
 * `ScopedRAII` is intended as a better replacement for many use cases of `ScopeGuard`.
 * `ScopeGuard` is not a resource owning object, it is merely a hook to indicate a particular piece
 * of code should run on exiting a scope.  Nearly all use cases for `ScopeGuard` are resource
 * management idioms which would benefit from more explicit grouping between the resource being
 * managed and its retirement scheme.
 */
template <typename T = scoped_raii_detail::Na,
          typename Dtor = typename scoped_raii_detail::select_dtor<T>::type>
class ScopedRAII;

template <typename T, typename Dtor>
class ScopedRAII {
private:
    ScopedRAII(const ScopedRAII&) = delete;
    ScopedRAII& operator=(const ScopedRAII&) = delete;

    Dtor dtor;
    T resource;

public:
    /**
     * Constructs a `ScopedRAII`.  This is done by saving the specified `Dtor_`, `d`, for calling in
     * the destructor and then invoking the specified `Ctor`, `c`, to construct the new object.
     * This ordering prevents resource leakage due to exceptions -- if the constructor accepted a
     * fully constructed object then any expressions on that line or failures within the
     * construction could cause resource leakage.
     */
    template <typename Ctor, typename Dtor_>
    explicit ScopedRAII(Ctor c, Dtor_ d) : dtor(std::move(d)), resource(c()) {}

    /**
     * Destroy a `ScopedRAII`.  This is done by invoking the specified `Dtor_`, `d`, on the
     * internally stored object.
     */
    ~ScopedRAII() noexcept {
        this->dtor(this->resource);
    }

    /**
     * Returns an immutable reference to the object being managed.  The reference is immutable, as
     * mutability would imply a state change which might require lifecycle tracking.
     */
    operator const T&() const {
        return this->resource;
    }
};

template <typename T, typename Dtor>
class ScopedRAII<T*, Dtor> {
private:
    ScopedRAII(const ScopedRAII&) = delete;
    ScopedRAII& operator=(const ScopedRAII&) = delete;

    Dtor dtor;
    T* resource;

public:
    template <typename Ctor, typename Dtor_>
    explicit ScopedRAII(Ctor c, Dtor_ d) : dtor(std::move(d)), resource(c()) {}

    ~ScopedRAII() noexcept {
        this->dtor(this->resource);
    }

    operator const T*() const {
        return this->resource;
    }

    T& operator*() {
        return *this->resource;
    }
    const T& operator*() const {
        return *this->resource;
    }

    T* operator->() {
        return this->resource;
    }
    const T* operator->() const {
        return this->resource;
    }
};

template <>
class ScopedRAII<scoped_raii_detail::Na> {
private:
    ScopedRAII(const ScopedRAII&) = delete;
    ScopedRAII& operator=(const ScopedRAII&) = delete;

    std::function<void()> dtor;

public:
    template <typename Ctor, typename Dtor>
    explicit ScopedRAII(Ctor c, Dtor d) : dtor(d) {
        c();
    }

    ~ScopedRAII() noexcept {
        if (this->dtor)
            this->dtor();
    }
};
}  // namespace mongo
