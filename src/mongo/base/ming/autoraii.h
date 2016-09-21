#pragma once

#include <functional>
#include <type_traits>
#include <tuple>
#include <array>

#include <boost/optional.hpp>
#include <boost/noncopyable.hpp>

namespace mongo
{
	namespace ming
	{
		namespace detail { class Na; }
		template< typename T= detail::Na > class AutoRAII;

		template< typename T >
		class AutoRAII
		{
			private:
				std::function< void ( T ) > dtor;
				T resource;
				AutoRAII( const AutoRAII & )= delete;
				AutoRAII &operator= ( const AutoRAII & )= delete;

			public:
				template< typename Ctor, typename Dtor >
				explicit
				AutoRAII( Ctor c, Dtor d ) : dtor( d ), resource( c() ) {}

				~AutoRAII() noexcept
				{
					this->dtor( this->resource );
				}

				operator const T &() const
				{
					return this->resource;
				}
		};

		template< typename T >
		class AutoRAII< T * >
		{
			private:
				std::function< void ( T * ) > dtor;
				T *resource;
				AutoRAII( const AutoRAII & )= delete;
				AutoRAII &operator= ( const AutoRAII & )= delete;

			public:
				template< typename Ctor, typename Dtor >
				explicit
				AutoRAII( Ctor c, Dtor d ) : dtor( d ), resource( c() ) {}

				~AutoRAII() noexcept { this->dtor( this->resource ); }

				operator T *() const { return this->resource; } 

				T *operator->() const { return this->resource; }
				T &operator *() const { return *this->resource; }
		};

		template<>
		class AutoRAII< detail::Na >
		{
			private:
				std::function< void () > dtor;

			public:
				template< typename Ctor, typename Dtor >
				explicit
				AutoRAII( Ctor c, Dtor d ) : dtor( d ) { c(); }

				~AutoRAII() noexcept
				{
					if( this->dtor ) this->dtor();
				}

				void dismiss() { this->dtor= nullptr; }
		};
	}//namespace ming
}//namespace mongo
