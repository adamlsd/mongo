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
		namespace detail { class Na; } // The detail::Na class is used to indicate an absent parameter to AutoRAII
		template< typename T= detail::Na > class AutoRAII;

        // This form is intended for use with things like socket_t, unix file handles, and other primitive types.
		template< typename T >
		class AutoRAII
		{
			private:
                static_assert( !std::is_class< T >::value,
                        "AutoRAII cannot adapt to structs or classes.  Consider adding a dtor to your type or using a pointer to the struct." );

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

				operator const T &() const { return this->resource; }
		};

        // This form is intended for use with any pointer that has cleanup semantic functions.
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
				~AutoRAII() noexcept
				{
					if( this->dtor ) this->dtor();
				}

				template< typename Ctor, typename Dtor >
				explicit
				AutoRAII( Ctor c, Dtor d ) : dtor( d ) { c(); }

				// TODO: Deprecate, and introduce Transaction concept.
				void dismiss() { this->dtor= nullptr; }
		};
	}//namespace ming
}//namespace mongo
