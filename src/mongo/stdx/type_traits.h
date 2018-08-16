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

#include <type_traits>

#include "mongo/config.h"

#if defined(MONGO_CONFIG_HAVE_STD_ENABLE_IF_T)

namespace mongo {
namespace stdx {

using ::std::enable_if_t;

}  // namespace stdx
}  // namespace mongo

#else

namespace mongo {
namespace stdx {

template <bool B, class T = void>
using enable_if_t = typename std::enable_if<B, T>::type;

}  // namespace stdx
}  // namespace mongo
#endif

#if __cplusplus >= 201703

namespace mongo {
namespace stdx {

using std::void_t;

using std::is_invokable;

using std::is_invokable_r;

using std::disjunction;

using std::conjunction;

}  // namespace stdx
}  // namespace mongo

#else

namespace mongo {
namespace stdx {

namespace detail {
template <typename...>
struct make_void {
    using type = void;
};
}  // namespace detail

template <typename... Args>
using void_t = typename detail::make_void<Args...>::type;


template <typename... B>
struct disjunction : std::false_type {};
template <typename B>
struct disjunction<B> : B {};

template <typename B1, typename... B>
struct disjunction<B1, B...> : std::conditional_t<bool(B1::value), B1, disjunction<B...>> {};


template <typename...>
struct conjunction : std::true_type {};
template <typename B>
struct conjunction<B> : B {};

template <typename B1, typename... B>
struct conjunction<B1, B...> : std::conditional_t<bool(B1::value), conjunction<B...>, B1> {};

/**
 * This is a poor-man's implementation of c++17 std::is_invokable. We should replace it with the
 * stdlib one once we can make call() use std::invoke.
 */
namespace detail {
template <typename Func,
          typename... Args,
          typename = typename std::result_of<Func && (Args && ...)>::type>
auto is_invokable_impl(Func&& func, Args&&... args) -> std::true_type;
auto is_invokable_impl(...) -> std::false_type;
}  // namespace detail

template <typename Func, typename... Args>
struct is_invokable
    : decltype(detail::is_invokable_impl(std::declval<Func>(), std::declval<Args>()...)) {};

namespace detail {

// This helps solve the lack of regular void problem, when passing a 'conversion target' as a
// parameter.
template <typename T>
struct magic_carrier {};

template <typename R,
          typename Func,
          typename... Args,
          typename ComputedResult = typename std::result_of<Func && (Args && ...)>::type>
auto is_invokable_r_impl(magic_carrier<R>&&, Func&& func, Args&&... args) ->
    typename stdx::disjunction<std::is_void<R>,
                               std::is_same<ComputedResult, R>,
                               std::is_convertible<ComputedResult, R>>::type;
auto is_invokable_r_impl(...) -> std::false_type;
}  // namespace detail

template <typename R, typename Func, typename... Args>
struct is_invokable_r
    : decltype(detail::is_invokable_r_impl(
          detail::magic_carrier<R>(), std::declval<Func>(), std::declval<Args>()...)) {};

}  // namespace stdx
}  // namespace mongo
#endif
