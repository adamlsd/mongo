#include <mutex>
#include <boost/noncopyable.hpp>
#include <string>

#include <cassert>

#include <iostream>


namespace InfiniteMonkeys
{
	namespace Testing
	{
		class TestFailure
		{
			private:
				std::string message;

			public:
				explicit TestFailure( std::string i_message ) : message( std::move( i_message ) ) {}

				const char *what() const noexcept { return message.c_str(); }
		};

		namespace Flags
		{
			static bool doNotFail= false;
		}

		namespace Status
		{
			std::unique_ptr< TestFailure > failure;
		}


		void
		assert_( bool b, const char *const reason )
		{
			if( b ) return;

			Status::failure= std::make_unique< TestFailure >( "Assertion failed: " + std::string( reason ) );
			if( !std::as_const( Flags::doNotFail ) )
			{
				std::cerr << "Assertion failure: \"" << reason << "\"" << std::endl;
				throw *Status::failure;
			}
		}

		class ScopedFailure
		{
			private:
				const bool oldFail;

			public:
				ScopedFailure()
					: oldFail( Flags::doNotFail )
				{
					assert_( !InfiniteMonkeys::Testing::Status::failure, "Test failure prior to test" );
					Flags::doNotFail= true;
				}

				~ScopedFailure()
				{
					assert_( InfiniteMonkeys::Testing::Status::failure.get(), "No reported failure!" );
					std::cerr << "A failure, when expecting one, was seen as: " << Status::failure->what() << std::endl;
					Status::failure= nullptr;
					Flags::doNotFail= oldFail;
				}
		};
	}
	using Testing::assert_;

    //namespace detail
    //{
		using Mtx= std::mutex;
        using ULock= std::unique_lock< std::mutex >;
    //}

	namespace WithReason
	{
		class Poisonable : boost::noncopyable
		{
			protected:
				const char *state= nullptr;
				Poisonable *const parent= nullptr;

			protected:
				[[nodiscard]] bool alive() const { return this->state == nullptr; }
				void validate() const { assert_( alive(), state ); }

				void revive() & { this->state= nullptr; }
				void poison( const char *reason ) & { this->state= reason; }

				explicit Poisonable( Poisonable *const i_parent, const char *const reason ) : parent( i_parent ) { if( this->parent ) this->parent->poison( reason ); }
				explicit Poisonable( std::nullptr_t ) : parent( nullptr ) {}

			public:
				~Poisonable() noexcept( false ) { this->validate(); if( this->parent ) this->parent->revive(); }
		};
	}

	namespace WithoutReason
	{
		class Poisonable : boost::noncopyable
		{
			protected:
				struct C { enum State { Healthy, Poisoned }; };
				using State= C::State;

				State state= C::Healthy;
				Poisonable *const parent= nullptr;

			protected:
				[[nodiscard]] bool alive() const { return this->state == C::Healthy; }
				void validate() const { assert_( alive(), "Not Healthy" ); }

				void revive() & { this->state= C::Healthy; }
				void poison( const char * /* reason */ ) & { this->state= C::Poisoned; }

				explicit
				Poisonable( Poisonable *const i_parent, const char *const i_reason )
					: parent( i_parent )
				{
					if( this->parent )
					{
						this->parent->validate();
						this->parent->poison( i_reason );
					}
				}

			public:
				~Poisonable() noexcept( false ) { this->validate(); if( this->parent ) this->parent->revive(); }
		};
	}
	using WithReason::Poisonable;

	class OwningLock;
	class Unlocked;


    class StrongLock : Poisonable
    {
        private:
			friend Unlocked;
            ULock *lk;

			struct BlockerBase { protected: BlockerBase()= default; };
			struct Blocker : BlockerBase { private: Blocker()= default; friend StrongLock; };

			struct Poisoner : Poisonable { Poisoner() : Poisonable( nullptr ){} };

			void
			validate() const
			{
				this->Poisonable::validate();
				assert_( lk->owns_lock(), "This StrongLock object was unlocked by someone, it cannot be used at this time." );
				assert_( this->alive(), "This StrongLock object, which is either suitable for r-value only, or experienced a nesting error -- it was poisoned" );
			}

			static Poisoner
			makePoison()
			{
				return Poisoner{};
			}


        public:
            StrongLock( ULock &i_lk ) : Poisonable( nullptr ), lk( &i_lk ) { assert_( lk->owns_lock(), "Cannot create a StrongLock on an unlocked lock" ); }
			// TODO: I think the Poisoner can use the Poisonable base class as its basis, but for now it's fine.
			// In fact, I think we can detect on the line, rather than wait for the dtor, if the `Poisoner` is a child `Poisonable`.
            StrongLock( ULock &&i_lk, Blocker= {}, Poisoner &&p= makePoison() )
				: Poisonable( &p, "Reference outlived the temporary it points to" ), lk( &i_lk )
			{
				assert_( this->lk->owns_lock(), "Cannot create a StrongLock on an unlocked lock" );
			}

			StrongLock( OwningLock && );
			StrongLock( OwningLock & );

            StrongLock( StrongLock &copy )
				: Poisonable( &copy, "Another StrongLock is currently responsible for this lock." ), lk( copy.lk )
            {
				this->validate();
            }

			~StrongLock()
			{
				validate();
			}

			[[nodiscard]] Unlocked promiscuous() &;

			operator ULock &() & { validate(); return *lk; }
    };

	auto
	makeUnlockGuard( ULock &lk )
	{
		return StrongLock( lk );
	}

	class OwningLock : Poisonable
	{
		private:
			friend Unlocked;
			ULock lk;

			friend StrongLock;

			explicit OwningLock( Poisonable *const p, Mtx &mtx )
				: Poisonable( p, "Use of an unlocked scope while it was locked by a nested scope" ), lk( mtx )
			{
				 assert_( this->lk.owns_lock(), "Cannot create a StrongLock (owning) on an unlocked lock" );
			}

		public:
			OwningLock( Mtx &mtx ) : Poisonable( nullptr ), lk( mtx ) { assert_( this->lk.owns_lock(), "Internal error acquiring lock" ); }

			[[nodiscard]] Unlocked promiscuous() &;
	};

	struct Unlocked : Poisonable
	{
		private:
			friend StrongLock;
			friend OwningLock;

			ULock *lk;

			void
			validate()
			{
				this->Poisonable::validate();
				assert_( !lk->owns_lock(), "This Unlocked object was locked by someone, it cannot be used at this time." );
				assert_( this->alive(), "This Unlocked object, which is suitable only for use when not chaste, was poisoned" );
			}

			Unlocked( OwningLock *const i_ul )
				: Poisonable( i_ul, "Use of a locked scope while it was unlocked by a nested scope" ), lk( &i_ul->lk )
			{
				this->lk->unlock();
			}

			Unlocked( StrongLock *const i_ul )
				: Poisonable( i_ul, "Use of a locked scope while it was unlocked by a nested scope" ), lk( i_ul->lk ) 
			{
				this->lk->unlock();
			}

		public:
			~Unlocked()
			{
				this->validate();
				this->lk->lock();
			}

			[[nodiscard]] auto
			chaste()
			{
				this->validate();
				return OwningLock( this, *this->lk->mutex() );
			}
	};

	[[nodiscard]] Unlocked
	StrongLock::promiscuous() &
	{
		this->validate();
		return Unlocked( this );
	}

	[[nodiscard]] Unlocked
	OwningLock::promiscuous() &
	{
		this->validate();
		return Unlocked( this );
	}

	StrongLock::StrongLock( OwningLock &o )
		: Poisonable( &o, "A StrongLock is currently responsible for the lock owned by this OwningLock" ), lk( &o.lk )
	{
		assert_( this->lk->owns_lock(), "Cannot create a StrongLock on an unlocked lock" );
	}

	StrongLock::StrongLock( OwningLock &&o )
		: Poisonable( &o, "Reference outlived the temporary it points to" ), lk( &o.lk )
	{
		std::cerr << "It's dangerous to go alone, so take this poisoner.  This allows promote, via rvalue case catching." << std::endl;
		assert_( this->lk->owns_lock(), "Cannot create a StrongLock on an unlocked lock" );
	}

	auto
	makeUnlockGuard( Mtx &mtx )
	{
		return OwningLock( mtx );
	}
}

namespace std
{
	template<>
	InfiniteMonkeys::OwningLock &&
	move( InfiniteMonkeys::OwningLock & ) noexcept= delete;

	template<> InfiniteMonkeys::StrongLock &&move( InfiniteMonkeys::StrongLock & ) noexcept= delete;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////

void disallowed();

using namespace InfiniteMonkeys;
void
f1( StrongLock u )
{
	std::cerr << "F1 called" << std::endl;
	auto prom= u.promiscuous();
	auto chaste= prom.chaste();
	auto p2= chaste.promiscuous();
	auto c2= p2.chaste();
	auto p3= c2.promiscuous();
	auto c3= p3.chaste();

	std::cerr << "Got all" << std::endl;

	if( 0 )
	{
		auto c2= prom.chaste();
	}
}

void
f2( StrongLock u )
{
	f1( u );
	f1( u );
}

std::mutex mtx;

int
main()
{
	// Syntax and typesystem only tests...
	disallowed();

	{
		auto l1= makeUnlockGuard( mtx );
		f2( l1 );
	}
	{
		ULock lk( mtx );
		f2( makeUnlockGuard( lk ) );
		auto l2= makeUnlockGuard( lk );
		f2( lk );
		f2( l2 );
	}



	f2( ULock( mtx ) );
}

template< typename T, typename = void >
struct syntax_checker : std::false_type {};


// Should die at compiletime
template< typename T >
struct syntax_checker< T, std::void_t< decltype( std::move( std::declval< T & >() ) ) > >
	: std::true_type {};

template< typename T >
constexpr bool check_movable= syntax_checker< T >::value;


#define DISALLOWED_SYNTAX
void
disallowed()
{
	f2( makeUnlockGuard( mtx ) ); // Should work fine at runtime

	auto l2= makeUnlockGuard( mtx ); // Should work fine at runtime

#ifdef DISALLOWED_SYNTAX
	{
		InfiniteMonkeys::Testing::ScopedFailure failureScope;
		StrongLock l= makeUnlockGuard( mtx ); // Should die at runtime
	}
	std::cerr << "Correctly caught a test failure for unlock guard." << std::endl;

	static_assert( !check_movable< StrongLock > );
	static_assert( !check_movable< OwningLock > );
#endif
}
