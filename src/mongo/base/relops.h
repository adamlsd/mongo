#pragma once

#include <type_traits>

namespace mongo
{
	namespace relops
	{
		namespace relops_detail
		{
			template< typename T >
			bool
			eq( const T &lhs, const T &rhs )
			{
				return lhs == rhs;
			}

			template< typename T >
			bool
			lt( const T &lhs, const T &rhs )
			{
				return lhs < rhs;
			}
		}//namespace relops_detail

		namespace equality
		{
			template< typename T >
			auto
			make_equality_lens( const T &t )
			{
				return t.make_equality_lens();
			}

			struct hook
			{
				template< typename T >
				friend typename std::enable_if< std::is_base_of< hook, T >::value, bool >::type
				operator == ( const T &lhs, const T &rhs )
				{
					return relops_detail::eq( make_equality_lens( lhs ), make_equality_lens( rhs ) );
				}

				template< typename T >
				friend typename std::enable_if< std::is_base_of< hook, T >::value, bool >::type
				operator != ( const T &lhs, const T &rhs )
				{
					return !( lhs == rhs );
				}
			};
		}//namespace equality

		namespace order
		{
			template< typename T >
			auto
			make_strict_weak_order_lens( const T &t )
			{
				return t.make_strict_weak_order_lens();
			}

			struct hook
			{
				template< typename T >
				friend typename std::enable_if< std::is_base_of< hook, T >::value, bool >::type
				operator < ( const T &lhs, const T &rhs )
				{
					return relops_detail::lt( make_strict_weak_order_lens( lhs ), make_strict_weak_order_lens( rhs ) );
				}

				template< typename T >
				friend typename std::enable_if< std::is_base_of< hook, T >::value, bool >::type
				operator > ( const T &lhs, const T &rhs )
				{
					return rhs < lhs;
				}

				template< typename T >
				friend typename std::enable_if< std::is_base_of< hook, T >::value, bool >::type
				operator <= ( const T &lhs, const T &rhs )
				{
					return !( lhs > rhs );
				}

				template< typename T >
				friend typename std::enable_if< std::is_base_of< hook, T >::value, bool >::type
				operator >= ( const T &lhs, const T &rhs )
				{
					return !( lhs < rhs );
				}
			};
		}//namespace order
	} // namespace relops
}//namespace mongo
