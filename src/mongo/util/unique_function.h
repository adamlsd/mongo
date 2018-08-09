/**
 *    Copyright 2018 MongoDB, Inc.
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

#include <memory>

#if 0

#include "third_party/function2-3.0.0/function2.hpp"

namespace mongo {
using ::fu2::unique_function;

template< typename Function >
class shared_function;

template< typename RetType, typename ... Args >
class shared_function< RetType( Args... ) >
{
	private:
		std::shared_ptr< unique_function< RetType( Args... ) > > impl;

	public:
		shared_function() = default;

		template< typename Functor >
		shared_function( Functor functor )
				: impl( std::make_unique< unique_function< RetType( Args... ) > >(
						std::move( functor ) ) )
		{}


		template< typename Functor>
		shared_function( unique_function< RetType( Args... ) > functor )
				: impl( std::make_unique< unique_function< RetType( Args... ) > >(
						std::move( functor ) ) )
		{}

		RetType
		operator()( Args... args ) const
		{
			if( !this->impl.get() ) throw std::bad_function_call();
			return ( *this->impl )( std::forward< Args >( args )... );
		}

		explicit operator bool() const { return this->impl.get(); }
};


}  // namespace mongo

#else

#include <functional>

namespace mongo {
template <typename Function>
class unique_function;

template< typename Function >
class shared_function;

template <typename RetType, typename... Args>
class unique_function<RetType(Args...)> {
private:
    struct Impl {
        virtual ~Impl() = default;
        virtual RetType call(Args&&...) = 0;
    };

	template< typename F > friend class shared_function;

public:
    unique_function() = default;

	~unique_function() noexcept= default;

	unique_function( const unique_function & )= delete;
	unique_function &operator= ( const unique_function & )= delete;

	unique_function( unique_function && ) noexcept= default;
	unique_function &operator= ( unique_function && ) noexcept= default;

    template <typename Functor>
    unique_function(Functor functor) : impl(makeImpl(std::move(functor))) {}

    RetType operator()(Args... args) const {
        if (!this->impl.get())
            throw std::bad_function_call();
        return this->impl->call(std::forward<Args>(args)...);
    }

    explicit operator bool() const {
        return this->impl.get();
    }

private:
    template <typename Functor>
    static std::unique_ptr<Impl> makeImpl(Functor functor) {
        class SpecificImpl : public Impl {
        private:
            Functor f;

        public:
            explicit SpecificImpl(Functor f) : f(std::move(f)) {}

            RetType call(Args&&... args) override {
                return f(std::forward<Args>(args)...);
            }
        };

        return std::make_unique<SpecificImpl>(std::move(functor));
    }

    std::unique_ptr<Impl> impl;
};

template< typename Function >
class shared_function;

template< typename RetType, typename ... Args >
class shared_function< RetType( Args... ) >
{
	private:
		using Impl= typename unique_function< RetType( Args... ) >::Impl;
		std::shared_ptr< Impl > impl;

	public:
		shared_function() = default;

		template< typename Functor >
		shared_function( Functor functor )
				: impl( unique_function< RetType( Args... ) >( std::move( functor ) ) ) {}

		template< typename Functor >
		shared_function( unique_function< Functor > functor )
				: impl( std::move( functor.impl ) ) {}

		RetType
		operator()( Args... args ) const
		{
			if( !this->impl.get() ) throw std::bad_function_call();
			return this->impl->call(std::forward<Args>(args)...);
		}

		explicit operator bool() const { return this->impl.get(); }
};

}//namespace mongo
#endif

namespace mongo
{
	template< typename RetVal, typename ... Args >
	auto
	shareFunction( unique_function< RetVal( Args... ) > function )
	{
		return shared_function< RetVal( Args... ) >( std::move( function ) );
	}

	// This code shall not compile.  If it does, we have a problem:

#if 0
	inline void
	mustFail()
	{
		std::function< void () > func;
		unique_function< void () > uf;

		func= std::move( uf );
	}

	inline void
	mustFail2()
	{
		std::vector< std::function< void () > > funcs;
		unique_function< void () > uf;

		funcs.push_back( std::move( uf ) );
	}

	inline void
	mustFail3()
	{
		struct Evil
		{
			unique_function< void () > uf;
		};

		Evil e1;
		Evil e2= e1;
	}
#endif
} //namespace mongo
