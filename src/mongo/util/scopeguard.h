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

#include <exception>
#include <type_traits>
#include <utility>

#include "mongo/platform/compiler.h"

namespace mongo {

template <typename F>
class[[nodiscard]] ScopeGuard {
public:
    template <typename FuncArg>
    explicit ScopeGuard(FuncArg && f) : _func(std::forward<FuncArg>(f)) {}

	// Remove all move and copy, MCE covers us here.
    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard(ScopeGuard && o) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;
    ScopeGuard& operator=(ScopeGuard&&) = delete;

    ~ScopeGuard() noexcept {
		_func();
    }

private:
    F _func;
};

template <typename F>
auto makeGuard(F&& fun) {
    return ScopeGuard<std::decay_t<F>>(std::forward<F>(fun));
}

template< typename F >
class[[nodiscard]] FailureGuard
{
	public:
		template <typename FuncArg>
		explicit FailureGuard(FuncArg && f) : _func(std::forward<FuncArg>(f)) {}

		// Remove all move and copy, MCE covers us here.
		FailureGuard(const FailureGuard&) = delete;
		FailureGuard(FailureGuard && o) = delete;
		FailureGuard& operator=(const FailureGuard&) = delete;
		FailureGuard& operator=(FailureGuard&&) = delete;

		~FailureGuard() noexcept {
			if(std::uncaught_exception() > _exceptionDepth)_func();
		}
		

	private:
		const int _exceptionDepth= std::uncaught_exceptions();
		F _func;

};

template <typename F>
auto makeFailureGuard(F&& fun) {
    return FailureGuard<std::decay_t<F>>(std::forward<F>(fun));
}


template< typename F >
class[[nodiscard]] DismissibleGuard
{
	public:
		template <typename FuncArg>
		explicit DismissibleGuard(FuncArg && f) : _func(std::forward<FuncArg>(f)) {}

		// Remove all move and copy, MCE covers us here.
		DismissibleGuard(const DismissibleGuard&) = delete;
		DismissibleGuard(DismissibleGuard && o) = delete;
		DismissibleGuard& operator=(const DismissibleGuard&) = delete;
		DismissibleGuard& operator=(DismissibleGuard&&) = delete;

		~DismissibleGuard() noexcept {
			if(!_dismissed)_func();
		}

		void dismiss() noexcept { _dismissed=false;}

	private:
		bool _dismissed= false;
		F _func;

};

template <typename F>
auto makeDismissibleGuard(F&& fun) {
    return DismissibleGuard<std::decay_t<F>>(std::forward<F>(fun));
}

}  // namespace mongo

#define MONGO_SCOPEGUARD_CAT2(s1, s2) s1##s2
#define MONGO_SCOPEGUARD_CAT(s1, s2) MONGO_SCOPEGUARD_CAT2(s1, s2)
#define MONGO_SCOPEGUARD_ANON(str) MONGO_SCOPEGUARD_CAT(str, __LINE__)

#define ON_BLOCK_EXIT(...) auto MONGO_SCOPEGUARD_ANON(onBlockExit) = makeGuard(__VA_ARGS__)
