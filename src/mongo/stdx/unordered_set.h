/**
 *    Copyright (C) 2016 MongoDB Inc.
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
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#if defined(_WIN32)
#include <boost/unordered_set.hpp>
#else
#include <unordered_set>
#endif

#include "stdx/functional.h"

namespace mongo {
namespace stdx {

namespace set_detail {
#if defined(_WIN32)
using ::boost::unordered_set;       // NOLINT
using ::boost::unordered_multiset;  // NOLINT
#else
using ::std::unordered_set;       // NOLINT
using ::std::unordered_multiset;  // NOLINT
#endif
}  // namespace set_detail

template <typename Key,
          typename Value,
          typename Hash = stdx::hash<Key>,
          typename KeyEqual = typename map_detail::unordered_set<Key, Value, Hash>::key_equal,
          typename Allocator =
              typename map_detail::unordered_set<Key, Value, Hash, KeyEqual>::allocator,
          typename... Args>
using unordered_set = set_detail::unordered_set<Key, Value, Hash, KeyEqual, Allocator, Args...>;

template <typename Key,
          typename Value,
          typename Hash = stdx::hash<Key>,
          typename KeyEqual = typename map_detail::unordered_multiset<Key, Value, Hash>::key_equal,
          typename Allocator =
              typename map_detail::unordered_multiset<Key, Value, Hash, KeyEqual>::allocator,
          typename... Args>
using unordered_multiset =
    set_detail::unordered_multiset<Key, Value, Hash, KeyEqual, Allocator, Args...>;

}  // namespace stdx
}  // namespace mongo
