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

				template< typename Dtor > explicit AutoRAII( T &&, Dtor &d )= delete;

				template< typename Dtor >
				explicit
				AutoRAII( typename std::enable_if< std::is_nothrow_move_constructible< T >::value, T && >::type t, const Dtor &d ) noexcept
				try
						: dtor( d ), resource( std::move( t ) ) {}
				catch( ... )
				{
					d( t );
				}

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

				// It is unsafe to make AutoRAII accept an unmanaged object with an xvalue dtor -- this implies that
				// the dtor function may have been created by an expression.  An expression creating a dtor can throw,
				// and we have no way to tell whether it would throw or not.

				// An expression creating a dtor can throw, so we want to make it hard to accidentally believe that the
				// creation of an AutoRAII under that circumstance makes code "magically" safe.
				template< typename Dtor > explicit AutoRAII( T *, Dtor && )= delete;

				// It is mostly safe to make AutoRAII accept an unmanaged object with an lvalue dtor -- this implies that
				// the dtor function was created in a statement other than our construction.  The `T *` may have been created
				template< typename Dtor >
				explicit
				AutoRAII( T *t, const Dtor &d ) noexcept
				try
						: dtor( d ), resource( std::move( t ) ) {}
				catch( ... )
				{
					d( t );
				}

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

				// TODO: Deprecate, and introduce Alepha::Transaction concept.
				void dismiss() { this->dtor= nullptr; }
		};

		namespace detail
		{
			// TODO: Remove this as the basis of UniqueRAII to disable the scopeguard idiom.
			// Do this once the idiom has been removed from the codebase.
			class UniqueRAIIBase : boost::noncopyable
			{
				protected:
					mutable bool active;

					~UniqueRAIIBase()= default;
					UniqueRAIIBase()= default;
					explicit UniqueRAIIBase( const bool a ) : active( a ) {}

				public:
					// These are here to support the ScopeGuard idiom.
					void dismiss() const { active= false; }
					void Dismiss() const { dismiss(); }
			};
		}

		template< typename Dtor, typename T >
		class UniqueRAII : public detail::UniqueRAIIBase
		{
			private:
				Dtor dtor;
				T resource;

				static_assert( std::is_nothrow_copy_constructible< Dtor >::value ||
						std::is_nothrow_move_constructible< Dtor >::value,
						"The destructor must be nothrow-move or copy constructible." );

				static_assert( std::is_nothrow_move_constructible< T >::value,
						"The type must be nothrow-move constructible." );

				UniqueRAII( const UniqueRAII & )= delete;
				UniqueRAII &operator= ( const UniqueRAII & )= delete;

			public:
				~UniqueRAII() noexcept
				{
					if( active ) dtor( resource );
				}

				UniqueRAII( UniqueRAII &&copy ) noexcept
						: UniqueRAIIBase( std::move( copy.active ) ), dtor( std::move( copy.dtor ) ),
						resource( std::move( copy.resource ) )
				{
					copy.active= false;
				}

				UniqueRAII &operator= ( UniqueRAII &&copy )= delete;

				template< typename Ctor >
				UniqueRAII( Ctor &&c, Dtor &&d )
						: dtor( std::move( d ) ), resource( c() ) {}

				void execute() { dtor( resource ); }
				void Execute() { execute(); }
		};

		namespace detail
		{
			// The detail::StatefulNa class is used to indicate an absent parameter to UniqueRAII, when emulating ScopeGuard
			class StatefulNa
			{
				private:
					//template< typename D, typename T > friend class UniqueRAII;
					StatefulNa()= default;

				public:
					static StatefulNa giveMeAStatefulNa() { return {}; }
			};
		}

		template< typename Dtor, typename Ctor >
		auto
		makeUniqueRAII( Ctor &&c, Dtor &&d )
			-> UniqueRAII< Dtor, decltype( c() ) >
		{
			return UniqueRAII< Dtor, decltype( c() ) >( std::forward< Ctor >( c ), std::forward< Dtor >( d ) );
		}
	}//namespace ming


#if 0
	using ScopeGuard= const ming::detail::UniqueRAIIBase &;

	template< typename F, typename ... Args >
	auto
	makeGuard( F &&fun, Args && ... args )
	{
		return ming::makeUniqueRAII( ming::detail::StatefulNa::giveMeAStatefulNa, [=]( const ming::detail::StatefulNa & ){ fun( std::move( args )... ); } );
				
	}
#endif
}//namespace mongo
