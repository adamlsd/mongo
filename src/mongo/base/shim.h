/**
 *    Copyright (C) 2018 MongoDB Inc.
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

#include "mongo/base/init.h"


namespace mongo {
template <typename T>
struct PrivateCall {
private:
    friend T;
    PrivateCall() {}
};

template <typename T>
using PrivateTo = const PrivateCall<T>&;
}  // namespace mongo

namespace shim_detail {
struct get_type {};
template <typename Function>
struct function_decompose;

template <typename ReturnType, typename... Args>
struct function_decompose<ReturnType(Args...)> {
    static const std::size_t function_args_count = sizeof...(Args);
    using return_type = ReturnType;
    using args_tuple_type = std::tuple<Args...>;
};

template <typename Function>
struct return_type {
    using type = typename function_decompose<Function>::return_type;
};

template <typename Function>
using return_type_t = typename return_type<Function>::type;

template <typename T, typename tag = void>
struct storage {
    static T data;
};

template <typename T, typename tag>
T storage<T, tag>::data = {};
}

#define MONGO_SHIM_DEPENDENTS ("ShimHooks")

namespace mongo {
#ifdef MONGO_CONFIG_CHECK_SHIM_DEPENDENCIES
const bool check_shims_via_tu_hook = true;
#define MONGO_SHIM_TU_HOOK(name) name = {}
// const bool check_shims_via_tu_hook= false;
//#define MONGO_SHIM_TU_HOOK( name )
#else
const bool check_shims_via_tu_hook = false;
#define MONGO_SHIM_TU_HOOK(name)
#endif
}

/**
 * Declare a shimmable function with name `SHIM_NAME`, returning a value of type `RETURN_TYPE`, with
 * any arguments.  Declare such constructs in a C++ header.
 */
#define MONGO_DECLARE_SHIM(FUNCTION_SIGNATURE) MONGO_DECLARE_SHIM_1(FUNCTION_SIGNATURE, __LINE__)
#define MONGO_DECLARE_SHIM_1(FUNCTION_SIGNATURE, LN) MONGO_DECLARE_SHIM_2(FUNCTION_SIGNATURE, LN)
#define MONGO_DECLARE_SHIM_2(FUNCTION_SIGNATURE, LN)                                         \
    const struct ShimBasis_##LN {                                                            \
        template <bool required = mongo::check_shims_via_tu_hook>                            \
        struct abi_check_t {};                                                               \
        using abi_check = abi_check_t<>;                                                     \
        template <bool required = mongo::check_shims_via_tu_hook>                            \
        struct libTUHook_t {                                                                 \
            libTUHook_t();                                                                   \
        };                                                                                   \
        using libTUHook = libTUHook_t<>;                                                     \
        template <bool required = mongo::check_shims_via_tu_hook>                            \
        struct implTUHook_t {                                                                \
            implTUHook_t();                                                                  \
        };                                                                                   \
        using implTUHook = implTUHook_t<>;                                                   \
                                                                                             \
        struct shim_impl {                                                                   \
            static auto function_type_helper FUNCTION_SIGNATURE;                             \
            using function_type = decltype(function_type_helper);                            \
            using return_type = shim_detail::return_type_t<function_type>;                   \
            shim_impl* abi(abi_check = {}) {                                                 \
                return this;                                                                 \
            }                                                                                \
            shim_impl* lib(MONGO_SHIM_TU_HOOK(libTUHook)) {                                  \
                return this;                                                                 \
            }                                                                                \
            shim_impl* impl(MONGO_SHIM_TU_HOOK(implTUHook)) {                                \
                return this;                                                                 \
            }                                                                                \
            virtual auto implementation FUNCTION_SIGNATURE = 0;                              \
        };                                                                                   \
                                                                                             \
        using tag = std::tuple<shim_impl::function_type, abi_check, libTUHook, implTUHook>;  \
                                                                                             \
        using storage = shim_detail::storage<shim_impl*, tag>;                               \
                                                                                             \
        template <typename... Args>                                                          \
        auto operator()(Args&&... args) const                                                \
            noexcept(noexcept(storage::data->implementation(std::forward<Args>(args)...)))   \
                -> shim_impl::return_type {                                                  \
            return storage::data->abi()->lib()->implementation(std::forward<Args>(args)...); \
        }                                                                                    \
    }

/**
 * Define a shimmable function with name `SHIM_NAME`, returning a value of type `RETURN_TYPE`, with
 * any arguments.  This shim definition macro should go in the associated C++ file to the header
 * where a SHIM was defined.  This macro does not emit a function definition, only the customization
 * point's machinery.
 */
#define MONGO_DEFINE_SHIM(SHIM_NAME) MONGO_DEFINE_SHIM_1(SHIM_NAME, __LINE__)
#define MONGO_DEFINE_SHIM_1(SHIM_NAME, LN) MONGO_DEFINE_SHIM_2(SHIM_NAME, LN)
#define MONGO_DEFINE_SHIM_2(SHIM_NAME, LN)                                                       \
    namespace {                                                                                  \
    namespace shim_namespace##LN {                                                               \
        using ShimType = decltype(SHIM_NAME);                                                    \
    } /*namespace shim_namespace*/                                                               \
    } /*namespace*/                                                                              \
    template <>                                                                                  \
    shim_namespace##LN::ShimType::libTUHook_t<::mongo::check_shims_via_tu_hook>::libTUHook_t() = \
        default;                                                                                 \
    shim_namespace##LN::ShimType SHIM_NAME;


/**
 * Define an implementation of a shimmable function with name `SHIM_NAME`.  The compiler will check
 * supplied parameters for correctness.  This shim registration macro should go in the associated
 * C++
 * implementation file to the header where a SHIM was defined.   Such a file would be a mock
 * implementation
 * or a real implementation, for example
 */
#define MONGO_REGISTER_SHIM(SHIM_NAME) MONGO_REGISTER_SHIM_1(SHIM_NAME, __LINE__)
#define MONGO_REGISTER_SHIM_1(SHIM_NAME, LN) MONGO_REGISTER_SHIM_2(SHIM_NAME, LN)
#define MONGO_REGISTER_SHIM_2(SHIM_NAME, LN)                                                       \
    namespace {                                                                                    \
    namespace shim_namespace##LN {                                                                 \
        using ShimType = decltype(SHIM_NAME);                                                      \
                                                                                                   \
        class impl : public ShimType::shim_impl {                                                  \
            ShimType::shim_impl::function_type implementation;                                     \
        };                                                                                         \
                                                                                                   \
        struct registration_of_impl {                                                              \
            registration_of_impl() {                                                               \
                static impl impl;                                                                  \
                ShimType::storage::data = &impl;                                                   \
            }                                                                                      \
        } registerImpl;                                                                            \
    } /*namespace shim_namespace*/                                                                 \
    } /*namespace*/                                                                                \
    template <>                                                                                    \
    shim_namespace##LN::ShimType::implTUHook_t<::mongo::check_shims_via_tu_hook>::implTUHook_t() = \
        default;                                                                                   \
                                                                                                   \
    auto shim_namespace##LN::impl::                                                                \
        implementation /* After this point someone just writes the signature's arguments and       \
                          return value (using arrow notation).  Then they write the body. */
