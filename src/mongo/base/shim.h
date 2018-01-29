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

#ifdef MONGO_CONFIG_CHECK_SHIM_DEPENDENCIES
#define MONGO_SHIM_EMIT_TU_HOOK(SHIM_NAME) SHIM_NAME##_base::tuHook()
#else
#define MONGO_SHIM_EMIT_TU_HOOK(SHIM_NAME) [] {}()
#endif

/**
 * Declare a shimmable function with name `SHIM_NAME`, returning a value of type `RV`, with any
 * arguments.  Declare such constructs in a C++ header.
 */
#define MONGO_DECLARE_SHIM(RV, SHIM_NAME, ...)                                          \
    struct SHIM_NAME##_base {                                                           \
        SHIM_NAME##_base();                                                             \
        static void tuHook();                                                           \
    };                                                                                  \
                                                                                        \
    struct SHIM_NAME##_impl : SHIM_NAME##_base {                                        \
    public:                                                                             \
        virtual RV operator()(__VA_ARGS__) = 0;                                         \
                                                                                        \
    public:                                                                             \
        static RV hook_check(__VA_ARGS__);                                              \
        using hook_arg_type = std::function<RV(SHIM_NAME##_impl*)>;                     \
        using return_type = RV;                                                         \
                                                                                        \
        static RV hook(hook_arg_type arg);                                              \
                                                                                        \
        static void registerImpl(SHIM_NAME##_impl* p);                                  \
    };                                                                                  \
                                                                                        \
    template <typename... Args>                                                         \
    RV SHIM_NAME(Args... args) {                                                        \
        MONGO_SHIM_EMIT_TU_HOOK(SHIM_NAME); /* a TU Hook to know provider is needed. */ \
        return SHIM_NAME##_impl::hook(                                                  \
            [&](SHIM_NAME##_impl* impl) -> RV { return (*impl)(args...); });            \
    }

#define MONGO_DECLARE_STATIC_SHIM(RV, SHIM_NAME, ...)                                   \
    struct SHIM_NAME##_base {                                                           \
        SHIM_NAME##_base();                                                             \
        static void tuHook();                                                           \
    };                                                                                  \
                                                                                        \
    struct SHIM_NAME##_impl : SHIM_NAME##_base {                                        \
    public:                                                                             \
        virtual RV operator()(__VA_ARGS__) = 0;                                         \
                                                                                        \
    public:                                                                             \
        static RV hook_check(__VA_ARGS__);                                              \
        using hook_arg_type = std::function<RV(SHIM_NAME##_impl*)>;                     \
        using return_type = RV;                                                         \
                                                                                        \
        static RV hook(hook_arg_type arg);                                              \
                                                                                        \
        static void registerImpl(SHIM_NAME##_impl* p);                                  \
    };                                                                                  \
                                                                                        \
    template <typename... Args>                                                         \
    static RV SHIM_NAME(Args... args) {                                                 \
        MONGO_SHIM_EMIT_TU_HOOK(SHIM_NAME); /* a TU Hook to know provider is needed. */ \
        return SHIM_NAME##_impl::hook(                                                  \
            [&](SHIM_NAME##_impl* impl) -> RV { return (*impl)(args...); });            \
    }


/**
 * Define a shimmable function with name `SHIM_NAME`, returning a value of type `RV`, with any
 * arguments.  This shim definition macro should go in the associated C++ file to the header
 * where a SHIM was defined.
 */
#define MONGO_DEFINE_SHIM(SHIM_NAME)                                             \
    namespace {                                                                  \
    SHIM_NAME##_impl* impl_for_##SHIM_NAME = nullptr;                            \
    }                                                                            \
                                                                                 \
    SHIM_NAME##_base::SHIM_NAME##_base() {}                                      \
                                                                                 \
    void SHIM_NAME##_impl::registerImpl(SHIM_NAME##_impl* p) {                   \
        impl_for_##SHIM_NAME = p;                                                \
    }                                                                            \
                                                                                 \
    SHIM_NAME##_impl::return_type SHIM_NAME##_impl::hook(hook_arg_type caller) { \
        return caller(impl_for_##SHIM_NAME);                                     \
    }

/**
 * Define a shimmable function with name `SHIM_NAME`, within class CLASS_NAME, returning a value of
 * type `RV`, with any arguments.  This shim definition macro should go in the associated C++ file
 * to the header where a SHIM was defined.
 */
#define MONGO_DEFINE_STATIC_SHIM(CLASS_NAME, SHIM_NAME) \
    MONGO_DEFINE_STATIC_SHIM_IMPL(CLASS_NAME, SHIM_NAME, __LINE__)

#define MONGO_DEFINE_STATIC_SHIM_IMPL(CLASS_NAME, SHIM_NAME, LN) \
    MONGO_DEFINE_STATIC_SHIM_IMPL2(CLASS_NAME, SHIM_NAME, LN)

#define MONGO_DEFINE_STATIC_SHIM_IMPL2(CLASS_NAME, SHIM_NAME, LN)                      \
    namespace {                                                                        \
    CLASS_NAME::SHIM_NAME##_impl* impl_for_##SHIM_NAME##LN = nullptr;                  \
    }                                                                                  \
                                                                                       \
    CLASS_NAME::SHIM_NAME##_base::SHIM_NAME##_base() {}                                \
                                                                                       \
    void CLASS_NAME::SHIM_NAME##_impl::registerImpl(CLASS_NAME::SHIM_NAME##_impl* p) { \
        impl_for_##SHIM_NAME##LN = p;                                                  \
    }                                                                                  \
                                                                                       \
    CLASS_NAME::SHIM_NAME##_impl::return_type CLASS_NAME::SHIM_NAME##_impl::hook(      \
        hook_arg_type caller) {                                                        \
        return caller(impl_for_##SHIM_NAME##LN);                                       \
    }


/**
 * Define an implementation of a shimmable function with name `SHIM_NAME`.  The compiler will check
 * supplied parameters for correctness.  This shim definition macro should go in the associated C++
 * file to the header where a SHIM was defined.
 */
#define MONGO_REGISTER_SHIM(SHIM_NAME) MONGO_REGISTER_SHIM_IMPL(SHIM_NAME, __LINE__)
#define MONGO_REGISTER_SHIM_IMPL(SHIM_NAME, LN) MONGO_REGISTER_SHIM_IMPL2(SHIM_NAME, LN)
#define MONGO_REGISTER_SHIM_IMPL2(SHIM_NAME, LN)                                                  \
    /* verifies that someone linked a single instance in, since multiple registered shim          \
     * implementations would conflict on this symbol. */                                          \
    void SHIM_NAME##_base::tuHook() {}                                                            \
                                                                                                  \
    namespace {                                                                                   \
    class SHIM_NAME##_specialization_##LN : public SHIM_NAME##_impl {                             \
    public:                                                                                       \
        decltype(SHIM_NAME##_impl::hook_check) operator() override;                               \
    };                                                                                            \
                                                                                                  \
    MONGO_INITIALIZER(Register##SHIM_NAME##LN)(InitializerContext * const) try {                  \
        SHIM_NAME##_impl::registerImpl(new SHIM_NAME##_specialization_##LN);                      \
        return Status::OK();                                                                      \
    } catch (...) {                                                                               \
        return exceptionToStatus();                                                               \
    }                                                                                             \
    }                                                                                             \
                                                                                                  \
    auto SHIM_NAME##_specialization_##LN::operator() /* After this point someone just writes the  \
                                                   signature's arguments. and return value (using \
                                                   arrow notation).  Then they write the body. */

#define MONGO_REGISTER_STATIC_SHIM(CLASS_NAME, SHIM_NAME) \
    MONGO_REGISTER_STATIC_SHIM_IMPL(CLASS_NAME, SHIM_NAME, __LINE__)
#define MONGO_REGISTER_STATIC_SHIM_IMPL(CLASS_NAME, SHIM_NAME, LN) \
    MONGO_REGISTER_STATIC_SHIM_IMPL2(CLASS_NAME, SHIM_NAME, LN)
#define MONGO_REGISTER_STATIC_SHIM_IMPL2(CLASS_NAME, SHIM_NAME, LN)                               \
    /* verifies that someone linked a single instance in, since multiple registered shim          \
     * implementations would conflict on this symbol. */                                          \
    void CLASS_NAME::SHIM_NAME##_base::tuHook() {}                                                \
                                                                                                  \
    namespace {                                                                                   \
    class SHIM_NAME##_specialization_##LN : public CLASS_NAME::SHIM_NAME##_impl {                 \
    public:                                                                                       \
        decltype(CLASS_NAME::SHIM_NAME##_impl::hook_check) operator() override;                   \
    };                                                                                            \
                                                                                                  \
    MONGO_INITIALIZER(Register##SHIM_NAME##LN)(InitializerContext * const) try {                  \
        CLASS_NAME::SHIM_NAME##_impl::registerImpl(new SHIM_NAME##_specialization_##LN);          \
        return Status::OK();                                                                      \
    } catch (...) {                                                                               \
        return exceptionToStatus();                                                               \
    }                                                                                             \
    }                                                                                             \
                                                                                                  \
    auto SHIM_NAME##_specialization_##LN::operator() /* After this point someone just writes the  \
                                                   signature's arguments. and return value (using \
                                                   arrow notation).  Then they write the body. */
