/**
 *    Copyright (C) 2014 10gen Inc.
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

#include <functional>
#include <type_traits>
#include <utility>

namespace mongo {
namespace stdx {

using ::std::bind;                             // NOLINT
using ::std::cref;                             // NOLINT
using ::std::function;                         // NOLINT
using ::std::ref;                              // NOLINT
namespace placeholders = ::std::placeholders;  // NOLINT

template<typename Fn, typename... Args>
constexpr decltype(auto) invoke(Fn&& f, Args&&... args)
    noexcept(noexcept(std::bind(std::forward<Fn>(f), std::forward<Args>(args)...)()))
{
    return std::bind(std::forward<Fn>(f), std::forward<Args>(args)...)();
}

namespace apply_detail {
template <class F, class Tuple, std::size_t... I>
constexpr decltype(auto) apply_impl(F&& f, Tuple&& t, std::index_sequence<I...>)
{
    return invoke(std::forward<F>(f), std::get<I>(std::forward<Tuple>(t))...);
}
}  // namespace detail
 
template <class F, class Tuple>
constexpr decltype(auto) apply(F&& f, Tuple&& t)
{
    return apply_detail::apply_impl(
        std::forward<F>(f), std::forward<Tuple>(t),
        std::make_index_sequence<std::tuple_size<typename std::remove_reference<Tuple>::type>::value>{});
}

}  // namespace stdx
}  // namespace mongo
