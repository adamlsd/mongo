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

#include "mongo/stdx/trusted_hasher.h"

#include <absl/container/node_hash_set.h>

namespace mongo::stdx {

template <typename Key, typename Hasher = DefaultHasher<Key>, typename... Args>
class unordered_set : absl::node_hash_set<Key, EnsureTrustedHasher<Hasher, Key>, Args...> {
private:
    using Parent = absl::node_hash_set<Key, EnsureTrustedHasher<Hasher, Key>, Args...>;

public:
    template <typename T>
    unordered_set& operator=(const std::initializer_list<T>& list) {
        return *this = unordered_set(list);
    }

    using typename Parent::allocator_type;
    using typename Parent::const_iterator;
    using typename Parent::const_pointer;
    using typename Parent::const_reference;
    using typename Parent::difference_type;
    using typename Parent::hasher;
    using typename Parent::iterator;
    using typename Parent::key_equal;
    using typename Parent::key_type;
    using typename Parent::node_type;
    using typename Parent::pointer;
    using typename Parent::reference;
    using typename Parent::size_type;
    using typename Parent::value_type;

    // using typename Parent::insert_return_type; // Absent in abseil
    // using typename Parent::local_iterator; // Absent in abseil
    // using typename Parent::const_local_iterator; // Absent in abseil

    using Parent::Parent;
    using Parent::operator=;
    using Parent::get_allocator;

    using Parent::begin;
    using Parent::cbegin;

    using Parent::cend;
    using Parent::end;

    using Parent::empty;
    using Parent::max_size;
    using Parent::size;

    using Parent::clear;
    using Parent::emplace;
    using Parent::emplace_hint;
    using Parent::erase;
    using Parent::extract;
    using Parent::insert;
    using Parent::merge;

    using Parent::contains;
    using Parent::count;
    using Parent::equal_range;
    using Parent::find;

    using Parent::bucket_count;
    // using Parent::max_bucket_count; // Absent in abseil
    // using Parent::bucket_size; // Absent in abseil
    // using Parent::bucket; // Absent in abseil

    using Parent::load_factor;
    using Parent::max_load_factor;
    using Parent::rehash;
    using Parent::reserve;

    using Parent::hash_function;
    using Parent::key_eq;

    void swap(unordered_set& that) noexcept(
        noexcept(std::declval<Parent&>().swap(std::declval<Parent&>()))) {
        auto& a = static_cast<Parent&>(*this);
        auto& b = static_cast<Parent&>(that);

        a.swap(b);
    }


    friend bool operator==(const unordered_set& lhs, const unordered_set& rhs) noexcept(
        noexcept(static_cast<const Parent&>(lhs) == static_cast<const Parent&>(rhs))) {
        return static_cast<const Parent&>(lhs) == static_cast<const Parent&>(rhs);
    }

    friend bool operator!=(const unordered_set& lhs, const unordered_set& rhs) noexcept(
        noexcept(static_cast<const Parent&>(lhs) != static_cast<const Parent&>(rhs))) {
        return static_cast<const Parent&>(lhs) != static_cast<const Parent&>(rhs);
    }
};

using std::begin;
using std::end;
}  // namespace mongo::stdx

namespace std {
template <typename Key, typename Hasher, typename... Args>
void swap(::mongo::stdx::unordered_set<Key, Hasher, Args...>& a_,
          ::mongo::stdx::unordered_set<Key, Hasher, Args...>& b_) {
    a_.swap(b_);
}
}  // namespace std
