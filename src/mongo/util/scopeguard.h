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

#include "mongo/base/unique_raii.h"

namespace mongo {
using ScopeGuard = const mongo::unique_raii_detail::UniqueRAIIScopeGuardBase&;

template <typename F, typename... Args>
auto MakeGuard(F dtor, const Args... args) {
    return make_unique_raii([] {}, [ dtor = std::move(dtor), args... ] { dtor(args...); });
}

template <typename O, typename Rv, typename... Args>
auto MakeGuard(Rv (O::*dtor)(Args...), O* const obj, const Args... args) {
    return make_unique_raii([] {},
                            [ dtor = std::move(dtor), obj, args... ] { (obj->*dtor)(args...); });
}


template <typename O, typename Rv, typename... Args>
auto MakeObjGuard(O* const obj, Rv (O::*dtor)(Args...), const Args... args) {
    return make_unique_raii([] {},
                            [ dtor = std::move(dtor), obj, args... ] { (obj->*dtor)(args...); });
}

template <typename O, typename Rv, typename... Args>
auto MakeObjGuard(O& objRef, Rv (O::*dtor)(Args...), const Args... args) {
    return make_unique_raii(
        [] {}, [ dtor = std::move(dtor), obj = &objRef, args... ] { (obj->*dtor)(args...); });
}


#define LOKI_CONCATENATE_DIRECT(s1, s2) s1##s2
#define LOKI_CONCATENATE(s1, s2) LOKI_CONCATENATE_DIRECT(s1, s2)
#define LOKI_ANONYMOUS_VARIABLE(str) LOKI_CONCATENATE(str, __LINE__)

#define ON_BLOCK_EXIT \
    MONGO_COMPILER_VARIABLE_UNUSED ScopeGuard LOKI_ANONYMOUS_VARIABLE(scopeGuard) = MakeGuard
#define ON_BLOCK_EXIT_OBJ \
    MONGO_COMPILER_VARIABLE_UNUSED ScopeGuard LOKI_ANONYMOUS_VARIABLE(scopeGuard) = MakeObjGuard
}  // namespace mongo
