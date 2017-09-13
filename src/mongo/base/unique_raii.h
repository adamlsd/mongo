#pragma once

#include <memory>
#include <utility>
#include <functional>

#include <boost/noncopyable.hpp>

namespace mongo
{
	namespace unique_raii_detail
	{
		class UniqueRAIIScopeGuardBase : boost::noncopyable
		{
			protected:
				mutable bool active_= false;

				inline bool active() const noexcept { return this->active_; }
				inline void disable() noexcept { this->active_= false; }

				explicit UniqueRAIIScopeGuardBase( const bool a ) : active_( a ) {}

			public:
				void Dismiss() const noexcept { this->active_= false; }
		};
	}

	template< typename T, typename Dtor >
	class UniqueRAII : boost::noncopyable
	{
		private:
			Dtor dtor;
			T resource;

			bool active_;

			inline bool active() const noexcept { return this->active_; }
			inline void disable() noexcept { this->active_= false; }


		public:
			~UniqueRAII()
			try
			{
				if( this->active_ ) this->dtor( this->resource );
			}
			catch( ... ) { return; }

			UniqueRAII( UniqueRAII &&copy )
					: dtor( std::move( copy.dtor ) ), resource( std::move( copy.resource ) ), active_()
			{
				using std::swap;
				swap( this->active_, copy.active_ );
			}

			UniqueRAII &
			operator= ( UniqueRAII copy )
			{
				using std::swap;
				swap( this->dtor, copy.dtor );
				swap( this->resource, copy.resource );
				swap( this->active_, copy.active_ );
				return *this;
			}

			template< typename Ctor, typename D >
			explicit
			UniqueRAII( Ctor c, D d )
					: dtor( std::move( d ) ), resource( c() ), active_( true ) {}

			inline operator const T &() const { return this->resource; }
	};

	template< typename Dtor >
	class UniqueRAII< void, Dtor >
			: public unique_raii_detail::UniqueRAIIScopeGuardBase
	{
		private:
			Dtor dtor;

		public:
			~UniqueRAII()
			try
			{
				if( this->active() ) this->dtor();
			}
			catch( ... ) { return; }

			UniqueRAII( UniqueRAII &&copy )
					: UniqueRAIIScopeGuardBase( false ), dtor( std::move( copy.dtor ) )
			{
				using std::swap;
				swap( this->active_, copy.active_ );
			}

			UniqueRAII &
			operator= ( UniqueRAII copy )
			{
				using std::swap;
				swap( this->dtor, copy.dtor );
				swap( this->active_, copy.active_ );
				return *this;
			}

			template< typename Ctor, typename D >
			explicit
			UniqueRAII( Ctor c, D d )
					: unique_raii_detail::UniqueRAIIScopeGuardBase( true ), dtor( std::move( d ) ) { c(); }
	};

	template< typename T, typename Dtor >
	class UniqueRAII< T *, Dtor > : boost::noncopyable
	{
		private:
			Dtor dtor;
			T *resource;

			bool active_;

			inline bool active() const noexcept { return this->active_; }
			inline void disable() noexcept { this->active_= false; }


		public:
			~UniqueRAII()
			try
			{
				if( this->active_ ) this->dtor( this->resource );
			}
			catch( ... ) { return; }

			UniqueRAII( UniqueRAII &&copy )
					: dtor( std::move( copy.dtor ) ), resource( std::move( copy.resource ) ), active_()
			{
				using std::swap;
				swap( this->active_, copy.active_ );
			}

			UniqueRAII &
			operator= ( UniqueRAII copy )
			{
				using std::swap;
				swap( this->dtor, copy.dtor );
				swap( this->resource, copy.resource );
				swap( this->active_, copy.active_ );
				return *this;
			}

			template< typename Ctor, typename D >
			explicit
			UniqueRAII( Ctor c, D d )
					: dtor( std::move( d ) ), resource( c() ), active_( true ) {}

			inline operator T *() const { return this->resource; }

			inline T &operator *() const { return *this->resource; }

			inline T *operator->() const { return this->resource; }
	};

	template< typename Ctor, typename Dtor >
	inline auto
	make_unique_raii( Ctor c, Dtor d )
	{
		return UniqueRAII< decltype( c() ), Dtor >( std::move( c ), std::move( d ) );
	}
}//namespace mongo
