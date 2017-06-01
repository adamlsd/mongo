#pragma once

#include <memory>
#include <utility>
#include <functional>

#include <boost/optional.hpp>
#include <boost/noncopyable.hpp>

namespace mongo
{
	namespace raii_detail
	{
		class Na;

		template< typename T >
		struct select_dtor
		{
			using type= std::function< void ( T ) >;
		};

		template<>
		struct select_dtor< Na >
		{
			using type= std::function< void () >;
		};
	} //namespace raii_detail

	template< typename T= raii_detail::Na, typename Dtor= typename raii_detail::select_dtor< T >::type > class ScopedRAII;

	template< typename T, typename Dtor >
	class ScopedRAII : boost::noncopyable
	{
		private:
			Dtor dtor;
			T resource;

		public:
			template< typename Ctor, typename Dtor_ >
			explicit
			ScopedRAII( Ctor c, Dtor_ d ) : dtor( std::move( d ) ), resource( c() ) {}

			~ScopedRAII() noexcept
			{
				this->dtor( this->resource );
			}

			inline operator       T &()       { return this->resource; }
			inline operator const T &() const { return this->resource; }

			inline       T &operator *()       { return *this->resource; }
			inline const T &operator *() const { return *this->resource; }

			inline       T *operator->()       { return this->resource; }
			inline const T *operator->() const { return this->resource; }
	};

	template<>
	class ScopedRAII< raii_detail::Na > : boost::noncopyable
	{
		private:
			std::function< void () > dtor;
			friend class DismissableRAII;

		public:
			template< typename Ctor, typename Dtor >
			explicit
			ScopedRAII( Ctor c, Dtor d ) : dtor( d ) { c(); }

			~ScopedRAII() noexcept
			{
				if( this->dtor ) this->dtor();
			}
	};

	class DismissableRAII : ScopedRAII< raii_detail::Na >
	{
		public:
			template< typename Ctor, typename Dtor >
			explicit
			DismissableRAII( Ctor c, Dtor d ) : ScopedRAII< raii_detail::Na >( c, d ) {}

			void dismiss() { this->dtor= nullptr; }
	};

	template< typename T, typename Dtor >
	class UniqueRAII : boost::noncopyable
	{
		private:
			Dtor dtor;
			T resource;

			//TODO(ADAM): Consider making an adaptive "null-state" mechanism to allow for zero-extra-cost degenerate state.
			bool active_;

			inline bool active() const noexcept { return this->active_; }
			inline void disable() noexcept { this->active_= false; }


		public:
			~UniqueRAII()
			{
				if( this->active_ ) this->dtor( this->resource );
			}

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

			inline operator       T &()       { return this->resource; }
			inline operator const T &() const { return this->resource; }

			inline       T &operator *()       { return *this->resource; }
			inline const T &operator *() const { return *this->resource; }

			inline       T *operator->()       { return this->resource; }
			inline const T *operator->() const { return this->resource; }
	};

	template< typename Ctor, typename Dtor >
	inline auto
	make_unique_raii( Ctor c, Dtor d )
	{
		return UniqueRAII< decltype( c() ), Dtor >( std::move( c ), std::move( d ) );
	}
}//namespace mongo
