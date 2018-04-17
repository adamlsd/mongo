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


namespace mongo
{
	template< typename T >
	struct PrivateCall
	{
		private:
			friend T;
			PrivateCall() {}
	};

	template< typename T >
	using PrivateTo= const PrivateCall< T >&;
}	// namespace mongo

namespace shim_detail
{
	struct get_type {};
	template< typename Function >
	struct function_decompose;

	template< typename ReturnType, typename ... Args >
	struct function_decompose< ReturnType( Args... ) >
	{
		static const std::size_t function_args_count= sizeof...( Args );
		using return_type= ReturnType;
		using args_tuple_type= std::tuple< Args... >;
	};

	template< typename Function >
	struct return_type { using type= typename function_decompose< Function >::return_type; };

	template< typename Function >
	using return_type_t= typename return_type< Function >::type;

	template< typename T, typename tag= void >
	struct storage
	{
		static T data;
	};

	template< typename T, typename tag >
	T storage< T, tag >::data= {};
}//namespace shim_detail

#define MONGO_SHIM_DEPENDENTS ( "ShimHooks" )

namespace mongo
{
#ifdef MONGO_CONFIG_CHECK_SHIM_DEPENDENCIES
const bool checkShimsViaTuHook= true;
#define MONGO_SHIM_TU_HOOK( name ) name= {}
//const bool check_shims_via_tu_hook= false;
//#define MONGO_SHIM_TU_HOOK( name )
#else
const bool checkShimsViaTuHook= false;
#define MONGO_SHIM_TU_HOOK( name )
#endif
}//namespace mongo

/**
 * Declare a shimmable function with name `SHIM_NAME`, returning a value of type `RETURN_TYPE`, with
 * any arguments.  Declare such constructs in a C++ header.
 */
#define MONGO_DECLARE_SHIM( ... )  MONGO_DECLARE_SHIM_1(__LINE__,__VA_ARGS__)
#define MONGO_DECLARE_SHIM_1( LN, ... ) MONGO_DECLARE_SHIM_2(LN,__VA_ARGS__)
#define MONGO_DECLARE_SHIM_2( LN, ... ) \
const struct \
{ \
	template< bool required= mongo::checkShimsViaTuHook > struct AbiCheckType {}; \
	using AbiCheck= AbiCheckType<>; \
	template< bool required= mongo::checkShimsViaTuHook > struct LibTUHookType { LibTUHookType(); }; \
	using LibTUHook= LibTUHookType<>; \
	template< bool required= mongo::checkShimsViaTuHook > struct ImplTUHookType { ImplTUHookType(); }; \
	using ImplTUHookType= ImplTUHookType<>; \
 \
	struct ShimImpl \
	{ \
		static auto functionTypeHelper __VA_ARGS__; \
		using function_type= decltype( functionTypeHelper ); \
		using return_type= shim_detail::return_type_t< function_type >; \
		ShimImpl *abi( AbiCheck= {} ) { return this; }\
		ShimImpl *lib( MONGO_SHIM_TU_HOOK( LibTUHook ) ) { return this; } \
		ShimImpl *impl( MONGO_SHIM_TU_HOOK( ImplTUHook ) ) { return this; } \
		virtual auto implementation __VA_ARGS__= 0; \
	}; \
 \
	using tag= std::tuple< ShimImpl::function_type, AbiCheck, LibTUHook, ImplTUHook >; \
 \
	using storage= shim_detail::storage< ShimImpl *, tag >; \
	\
	template< typename... Args > \
	auto operator()( Args &&... args ) const \
	noexcept( noexcept( storage::data->abi()->lib()->implementation( std::forward< Args >( args )... ) ) ) \
				-> ShimImpl::return_type \
	/* TODO: When the dependency graph is fixed, add the `impl()->` call to this chain */ \
	{ return storage::data->abi()->lib()->implementation( std::forward< Args >( args )... ); } \
}

/**
 * Define a shimmable function with name `SHIM_NAME`, returning a value of type `RETURN_TYPE`, with
 * any arguments.  This shim definition macro should go in the associated C++ file to the header
 * where a SHIM was defined.  This macro does not emit a function definition, only the customization point's machinery.
 */
#define MONGO_DEFINE_SHIM( ... ) MONGO_DEFINE_SHIM_1( __LINE__, __VA_ARGS__ )
#define MONGO_DEFINE_SHIM_1( LN,... ) MONGO_DEFINE_SHIM_2(LN, __VA_ARGS__ )
#define MONGO_DEFINE_SHIM_2( LN,...) \
namespace \
{  \
	namespace shim_namespace##LN \
	{ \
		using ShimType= decltype( __VA_ARGS__ ); \
	}/*namespace shim_namespace*/ \
}/*namespace*/ \
template<> \
shim_namespace##LN::ShimType::LibTUHookType< ::mongo::checkShimsViaTUHook >::LibTUHookType()= default; \
shim_namespace##LN::ShimType __VA_ARGS__;


/**
 * Define an implementation of a shimmable function with name `SHIM_NAME`.  The compiler will check
 * supplied parameters for correctness.  This shim registration macro should go in the associated C++
 * implementation file to the header where a SHIM was defined.   Such a file would be a mock implementation
 * or a real implementation, for example
 */
#define MONGO_REGISTER_SHIM(...) MONGO_REGISTER_SHIM_1(__LINE__, __VA_ARGS__)
#define MONGO_REGISTER_SHIM_1(LN, ...) MONGO_REGISTER_SHIM_2(LN,__VA_ARGS__)
#define MONGO_REGISTER_SHIM_2(LN, ...) \
namespace \
{\
namespace shim_namespace##LN \
{ \
	using ShimType= decltype( SHIM_NAME ); \
 \
	class Implementation final : public ShimType::ShimImpl \
	{ \
		ShimType::ShimImpl::function_type implementation; /* override */\
	}; \
 \
	MONGO_INITIALIZER( __VA_ARGS__ )( const InitializerContext * ) \
	{ \
		static Implementation impl; \
		ShimType::storage::data= &impl; \
		return Status::OK(); \
	} \
}/*namespace shim_namespace*/ \
}/*namespace*/\
\
template<> \
shim_namespace##LN::ShimType::implTUHook_t< ::mongo::check_shims_via_tu_hook >::implTUHook_t()= default; \
\
auto \
shim_namespace##LN::impl::implementation /* After this point someone just writes the signature's arguments and return value (using arrow notation).  Then they write the body. */
