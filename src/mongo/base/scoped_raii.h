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
#include <boost/optional.hpp>

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

template <typename T = scoped_raii_detail::Na,
          typename Dtor = typename scoped_raii_detail::select_dtor<T>::type>
class ScopedRAII;

template <typename T, typename Dtor>
class ScopedRAII : boost::noncopyable {
private:
    Dtor dtor;
    T resource;

public:
    template <typename Ctor, typename Dtor_>
    explicit ScopedRAII(Ctor c, Dtor_ d) : dtor(std::move(d)), resource(c()) {}

    ~ScopedRAII() noexcept {
        this->dtor(this->resource);
    }

    inline operator const T&() const {
        return this->resource;
    }
};

template <typename T, typename Dtor>
class ScopedRAII<T*, Dtor> : boost::noncopyable {
private:
    Dtor dtor;
    T* resource;

public:
    template <typename Ctor, typename Dtor_>
    explicit ScopedRAII(Ctor c, Dtor_ d) : dtor(std::move(d)), resource(c()) {}

    ~ScopedRAII() noexcept {
        this->dtor(this->resource);
    }

    inline operator const T*() const {
        return this->resource;
    }

    inline T& operator*() {
        return *this->resource;
    }
    inline const T& operator*() const {
        return *this->resource;
    }

    inline T* operator->() {
        return this->resource;
    }
    inline const T* operator->() const {
        return this->resource;
    }
};

template <>
class ScopedRAII<scoped_raii_detail::Na> : boost::noncopyable {
private:
    std::function<void()> dtor;
    friend class DismissableRAII;

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

class DismissableRAII : ScopedRAII<scoped_raii_detail::Na> {
public:
    template <typename Ctor, typename Dtor>
    explicit DismissableRAII(Ctor c, Dtor d) : ScopedRAII<scoped_raii_detail::Na>(c, d) {}

    void dismiss() {
        this->dtor = nullptr;
    }
};
}  // namespace mongo
