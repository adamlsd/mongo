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
		struct Na;

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

			inline operator const T &() const { return this->resource; }
	};

	template< typename T, typename Dtor >
	class ScopedRAII< T *, Dtor >: boost::noncopyable
	{
		private:
			Dtor dtor;
			T *resource;

		public:
			template< typename Ctor, typename Dtor_ >
			explicit
			ScopedRAII( Ctor c, Dtor_ d ) : dtor( std::move( d ) ), resource( c() ) {}

			~ScopedRAII() noexcept
			{
				this->dtor( this->resource );
			}

			inline operator const T *() const { return this->resource; }

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
}//namespace mongo
