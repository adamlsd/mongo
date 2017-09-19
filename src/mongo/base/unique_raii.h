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

    UniqueRAII& operator=(UniqueRAII copy) {
        using std::swap;
        swap(this->dtor, copy.dtor);
        swap(this->resource, copy.resource);
        swap(this->active_, copy.active_);
        return *this;
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

    UniqueRAII& operator=(UniqueRAII copy) {
        using std::swap;
        swap(this->dtor, copy.dtor);
        swap(this->active_, copy.active_);
        return *this;
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

    UniqueRAII& operator=(UniqueRAII&& copy) {
        using std::swap;
        swap(this->dtor, copy.dtor);
        swap(this->resource, copy.resource);
        swap(this->active_, copy.active_);
        return *this;
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

template <typename Ctor, typename Dtor>
inline auto make_unique_raii(Ctor c, Dtor d) {
    return UniqueRAII<decltype(c()), Dtor>(std::move(c), std::move(d));
}
}  // namespace mongo
