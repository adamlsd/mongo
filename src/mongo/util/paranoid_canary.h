#pragma once
#include <sys/mman.h>
#include <unistd.h>

#include <time.h>

#include <iostream>
#include <thread>
#include <condition_variable>
#include <algorithm>
#include <numeric>
#include <iterator>
#include <cstdlib>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>
#include <utility>

namespace mongo_paranoid
{
	#define PREVENT_OPTIMIZING_VARIABLE( v ) \
	__asm__ __volatile__("" :: "m" (v));

	__attribute__(( __noinline__ ))
	inline void
	fast_assert( const bool b )
	{
		PREVENT_OPTIMIZING_VARIABLE( b );
		if( !b ) abort();
	}

	template< typename T, typename U >
	__attribute__(( __noinline__ ))
	inline void
	fast_assert_eq( const T &lhs, const U &rhs )
	{
		PREVENT_OPTIMIZING_VARIABLE( lhs );
		PREVENT_OPTIMIZING_VARIABLE( rhs );
		fast_assert( lhs == rhs );
	}

	template< typename T, typename U >
	__attribute__(( __noinline__ ))
	inline void
	fast_assert_ne( const T &lhs, const U &rhs )
	{
		PREVENT_OPTIMIZING_VARIABLE( lhs );
		PREVENT_OPTIMIZING_VARIABLE( rhs );
		fast_assert( lhs != rhs );
	}

	template< typename= void > struct protection_list
	{
		static std::mutex list_access;
		static std::vector< void * > list;
	};
	template< typename T > std::vector< void * > protection_list< T >::list;
	template< typename T > std::mutex protection_list< T >::list_access;

	class AddressGuard
	{
		private:
			inline static void
			add_address_to_list( void *const p )
			{
				std::lock_guard< std::mutex > lock( protection_list<>::list_access );

				const auto found= std::find( begin( protection_list<>::list ), end( protection_list<>::list ), p );
				fast_assert_eq( found, end( protection_list<>::list ) );

				protection_list<>::list.push_back( p );
			}


			inline static void
			remove_address_from_list( void *const p )
			{
				std::lock_guard< std::mutex > lock( protection_list<>::list_access );

				const auto found= std::find( begin( protection_list<>::list ), end( protection_list<>::list ), p );
				fast_assert_ne( found, end( protection_list<>::list ) );

				protection_list<>::list.erase( found );
			}

			AddressGuard( const AddressGuard & )= delete;
			AddressGuard &operator= ( const AddressGuard & )= delete;

			void *p;

		public:
			explicit AddressGuard( void *const i_p ) : p( i_p ) { add_address_to_list( p ); }
			~AddressGuard() { remove_address_from_list( p ); }
	};

	__attribute__(( __noinline__ ))
	inline void
	protection_thread( void *const p, const std::size_t page_count, std::mutex *m, volatile std::uint32_t *v )
	{
		std::unique_lock< std::mutex > l( *m );
		
		while( *v != 0x14147713 );
		asm( "" : : : "memory" );
		{
			AddressGuard g{ p };
			const bool success= !mprotect( p, 65536 * page_count, PROT_READ );
			asm( "" : : : "memory" );
			usleep( 400 );
			asm( "" : : : "memory" );
			int passes= 0;
			do
			{
				const bool retry= mprotect( p, 65536, PROT_READ | PROT_WRITE );
				if( !retry ) break;
				usleep( 100 );
				if( passes++ > 10 ) abort();
			} while( success );
			asm( "" : : : "memory" );
		}
		
		l.unlock();
		asm( "" : : : "memory" );
		usleep( 1 );
		*v= 0xFA110075;
		asm( "" : : : "memory" );
	}

	__attribute__(( __noinline__ ))
	inline void
	start_protection( void *const p, const std::size_t page_count= 1 )
	{
		volatile std::uint32_t touchpoint= 0;
		std::unique_ptr< std::mutex > m( new std::mutex );
		std::unique_lock< std::mutex > l( *m );
		std::unique_ptr< std::thread > t( new std::thread( protection_thread, p, page_count, m.get(), &touchpoint ) );
		t->detach();
		
		l.unlock();
		asm( "" : : : "memory" );
		touchpoint= 0x14147713;
		asm( "" : : : "memory" );
		while( touchpoint != 0xFA110075 );
		asm( "" : : : "memory" );
		l.lock();
	}

	__attribute__(( __noinline__ ))
	inline void
	defensiveCanary( const std::intptr_t i_p )
	{
		void *p= reinterpret_cast< void *const & >( i_p );
		PREVENT_OPTIMIZING_VARIABLE( p );
		start_protection( p );
	}

	__attribute__(( __noinline__ ))
	inline std::size_t
	cksum_memory( const volatile void *const i_p, const std::size_t sz )
	{
		const volatile std::uint8_t *const p= static_cast< const volatile std::uint8_t * >( i_p );
		std::atomic_thread_fence( std::memory_order_seq_cst );
		const auto rv= std::accumulate( p, p + sz, std::size_t{} );
		std::atomic_thread_fence( std::memory_order_seq_cst );
		return rv;
	}

	class SpearCanary
	{
		private:
			const volatile size_t kSize;
			const volatile std::uint16_t *const volatile p;
			const size_t kChecksum;

			static void
			scan_for_protection( const volatile std::uint16_t *const i_p, std::size_t size )
			{
				const std::uint64_t xp= (const std::size_t) i_p;

				const std::size_t addy= xp;
				const std::size_t page_addy= ( addy >> 16 ) << 16;

				start_protection( (void *) page_addy, 1 );
			}
						

		public:
			explicit inline
			SpearCanary( const volatile void *const i_p, const std::size_t sz )
					: kSize( sz ), p( (const volatile std::uint16_t *) i_p ), kChecksum( cksum_memory( i_p, kSize ) )
			{
				scan_for_protection( p, kSize );
			}

			~SpearCanary()
			{
				const auto nsum= cksum_memory( p, kSize );
				PREVENT_OPTIMIZING_VARIABLE( nsum );
				fast_assert_eq( kChecksum, nsum );
			}
	};

	class Canary
	{
		private:
			__attribute__(( __noinline__ ))
			static volatile std::uint8_t *
			cloneBlock( volatile std::uint8_t *const p, const std::size_t kSize, const std::size_t prereq, const std::size_t prereq2 ) noexcept
			{
				auto *const precopyChecksum= (std::size_t *) alloca( sizeof( std::size_t ) );
				fast_assert_eq( prereq, prereq2 );
				*precopyChecksum= cksum_memory( p, kSize );
				fast_assert_eq( prereq, *precopyChecksum );
				auto rv= new std::uint8_t [ kSize ]();
				std::copy_n( p, kSize, rv );
				auto *const postcopyChecksum= (std::size_t *) alloca( sizeof( std::size_t ) );
				*postcopyChecksum= cksum_memory( p, kSize );
				fast_assert_eq( *precopyChecksum, *postcopyChecksum );
				fast_assert_eq( prereq, *postcopyChecksum );
				auto *const rv_cksum= (std::size_t *) alloca( sizeof( std::size_t ) );
				*rv_cksum= cksum_memory( rv, kSize );
				fast_assert_eq( *precopyChecksum, *postcopyChecksum );
				fast_assert_eq( prereq, *postcopyChecksum );
				fast_assert_eq( *rv_cksum, *precopyChecksum );
				fast_assert_eq( *rv_cksum, *postcopyChecksum );
				fast_assert_eq( prereq, *rv_cksum );
				fast_assert_eq( *precopyChecksum, *postcopyChecksum );
				auto p_cksum= (std::size_t *) alloca( sizeof( std::size_t ) );
				*p_cksum= cksum_memory( p, kSize );
				fast_assert_eq( *rv_cksum, *p_cksum );
				fast_assert_eq( prereq, *p_cksum );
				fast_assert_eq( *postcopyChecksum, *p_cksum );
				fast_assert_eq( *precopyChecksum, *p_cksum );
				fast_assert_eq( *precopyChecksum, *postcopyChecksum );
				PREVENT_OPTIMIZING_VARIABLE( precopyChecksum );
				PREVENT_OPTIMIZING_VARIABLE( postcopyChecksum );
				PREVENT_OPTIMIZING_VARIABLE( rv_cksum );
				PREVENT_OPTIMIZING_VARIABLE( p_cksum );
				PREVENT_OPTIMIZING_VARIABLE( rv );
				PREVENT_OPTIMIZING_VARIABLE( prereq );
				PREVENT_OPTIMIZING_VARIABLE( prereq2 );
				PREVENT_OPTIMIZING_VARIABLE( p );
				PREVENT_OPTIMIZING_VARIABLE( kSize );
				return rv;
			}

			const volatile size_t kSize;

			const volatile std::size_t offloadChecksum1;
			const volatile std::uint8_t *const volatile offload1;

			const volatile std::size_t offloadChecksum2;
			const volatile std::uint8_t *const volatile offload2;

			const volatile std::size_t offloadChecksum3;
			const volatile std::uint8_t *const volatile offload3;

			const volatile std::size_t offloadChecksum4;
			const volatile std::uint8_t *const volatile offload4;

			volatile std::size_t offloadChecksumPost;
			const volatile std::uint8_t *volatile offloadPost;

			const volatile unsigned char* const volatile t;

			const size_t kChecksum;

			static constexpr uint8_t kBits= 0xCD;

			__attribute__(( __noinline__ ))
			void
			_verify() const noexcept
			{
				fast_assert_eq( cksum_memory( t, kSize ), kChecksum );
			}


		public:
			__attribute__(( __noinline__ ))
			explicit
			Canary( volatile unsigned char *const i_t, const std::size_t sz ) noexcept
					: kSize( sz ),
					  offloadChecksum1( cksum_memory( i_t, kSize ) ), offload1( cloneBlock( i_t, kSize, offloadChecksum1, offloadChecksum1 ) ),
					  offloadChecksum2( cksum_memory( i_t, kSize ) ), offload2( cloneBlock( i_t, kSize, offloadChecksum2, offloadChecksum1 ) ),
					  offloadChecksum3( cksum_memory( i_t, kSize ) ), offload3( cloneBlock( i_t, kSize, offloadChecksum3, offloadChecksum2 ) ),
					  offloadChecksum4( cksum_memory( i_t, kSize ) ), offload4( cloneBlock( i_t, kSize, offloadChecksum4, offloadChecksum3 ) ),
					  t( i_t ), kChecksum( kSize * size_t( kBits ) )
			{
				::memset( const_cast< unsigned char * >( t ), kBits, kSize );
				_verify();
				offloadChecksumPost= cksum_memory( i_t, kSize );
				offloadPost= cloneBlock( i_t, kSize, offloadChecksumPost, kChecksum );

				fast_assert_eq( offloadChecksumPost, kChecksum );
				_verify();
				_verify();

				fast_assert_eq( offloadChecksum1, offloadChecksum2 );
				fast_assert_eq( offloadChecksum2, offloadChecksum3 );
				fast_assert_eq( offloadChecksum3, offloadChecksum4 );
			}

			__attribute__(( __noinline__ ))
			~Canary() noexcept
			{
				_verify();
				_verify();
				const volatile bool ck1= cksum_memory( offload1, kSize ) == offloadChecksum1;
				const volatile bool ck2= cksum_memory( offload2, kSize ) == offloadChecksum2;
				const volatile bool ck3= cksum_memory( offload3, kSize ) == offloadChecksum3;
				const volatile bool ck4= cksum_memory( offload4, kSize ) == offloadChecksum4;
				const volatile bool ck1a= cksum_memory( offload1, kSize ) == offloadChecksum1;
				const volatile bool ck2a= cksum_memory( offload2, kSize ) == offloadChecksum2;
				const volatile bool ck3a= cksum_memory( offload3, kSize ) == offloadChecksum3;
				const volatile bool ck4a= cksum_memory( offload4, kSize ) == offloadChecksum4;

				const volatile bool ck1_2= offloadChecksum1 == offloadChecksum2;
				const volatile bool ck1_3= offloadChecksum1 == offloadChecksum3;
				const volatile bool ck1_4= offloadChecksum1 == offloadChecksum4;

				const volatile bool ck2_3= offloadChecksum2 == offloadChecksum3;
				const volatile bool ck2_4= offloadChecksum2 == offloadChecksum4;

				const volatile bool ck3_4= offloadChecksum3 == offloadChecksum4;


				fast_assert( ck1 );
				fast_assert( ck2 );
				fast_assert( ck3 );
				fast_assert( ck4 );
				fast_assert( ck1a );
				fast_assert( ck2a );
				fast_assert( ck3a );
				fast_assert( ck4a );

				fast_assert( ck1_2 );
				fast_assert( ck1_3 );
				fast_assert( ck1_4 );

				fast_assert( ck2_3 );
				fast_assert( ck2_4 );

				fast_assert( ck3_4 );

				delete offload4; // Doing a manual deletion here, because unique_ptr is too annoying to figure out how to get at the raw pointer during debug.
				delete offload3; // Doing a manual deletion here, because unique_ptr is too annoying to figure out how to get at the raw pointer during debug.
				delete offload2; // Doing a manual deletion here, because unique_ptr is too annoying to figure out how to get at the raw pointer during debug.
				delete offload1; // Doing a manual deletion here, because unique_ptr is too annoying to figure out how to get at the raw pointer during debug.

				_verify();

				fast_assert( offloadChecksumPost == kChecksum );

				fast_assert_eq( cksum_memory( offloadPost, kSize ), offloadChecksumPost );
				fast_assert_eq( cksum_memory( offloadPost, kSize ), kChecksum );

				delete offloadPost;

				_verify();
			}
	};
}  // namespace mongo_paranoid

#define RAND_INJECT_CANARY \
	struct _canary_hack; \
	const std::size_t canary_allocAmount= 1024 + std::rand() % 32767; \
    volatile unsigned char *const canary_cookie= static_cast<unsigned char *>(alloca(canary_allocAmount)); \
    const mongo_paranoid::Canary canary_c(canary_cookie,canary_allocAmount); do {} while( 0 )

#define Q_INJECT_CANARY \
	struct _defensive_canary_hack; \
	const std::size_t x_canary_allocAmount= 1024 + std::rand() % 32767; \
    volatile unsigned char *const x_canary_cookie= static_cast<unsigned char *>(alloca(x_canary_allocAmount)); \
    const mongo_paranoid::Canary x_canary_c(x_canary_cookie,x_canary_allocAmount); \
	struct _defensive_canary_hack; \
	const void *const canary_block= alloca( 131072 ); \
	const std::intptr_t canary_ptr= reinterpret_cast< const std::uint64_t & >( canary_block ); \
	const std::intptr_t canary_page_aligned= ( canary_ptr >> 16 ) << 16; \
	mongo_paranoid::defensiveCanary( canary_page_aligned ); \
	struct _defensive_canary_hack; \
	const std::size_t canary_allocAmount= 1024 + std::rand() % 32767; \
    volatile unsigned char *const canary_cookie= static_cast<unsigned char *>(alloca(canary_allocAmount)); \
    const mongo_paranoid::Canary canary_c(canary_cookie, canary_allocAmount); \
	do {} while( 0 )

#define V_INJECT_CANARY \
	struct canary_hack; \
	const std::size_t canary_allocAmount= 1024 + std::rand() % 16384; \
    volatile void *const canary_cookie= alloca(canary_allocAmount); \
    const mongo_paranoid::SpearCanary canary_c(canary_cookie, canary_allocAmount); do {} while( 0 )

#define MPROTECT_INJECT_CANARY \
	struct canary_hack; \
	const std::size_t canary_amount= 1024 + 16384; \
    volatile std::uint8_t canary_cookie[canary_amount]; \
    const mongo_paranoid::SpearCanary canary_c(&canary_cookie, canary_amount); do {} while( 0 )

#define INJECT_CANARY MPROTECT_INJECT_CANARY
