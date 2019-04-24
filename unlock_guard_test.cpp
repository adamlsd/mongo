#include <mutex>
#include <boost/noncopyable.hpp>

#include <cassert>

#include <iostream>


namespace InfiniteMonkeys
{
    //namespace detail
    //{
		using Mtx= std::mutex;
        using ULock= std::unique_lock< std::mutex >;
    //}

	class Poisonable : boost::noncopyable
	{
		protected:
			struct C { enum State { Healthy, Poisoned }; };
			using State= C::State;

			State state= C::Healthy;
			Poisonable *const parent= nullptr;

		protected:
			[[nodiscard]] bool alive() const { return this->state == C::Healthy; }
			void validate() const { assert( alive() ); }

			void revive() & { this->state= C::Healthy; }
			void poison() & { this->state= C::Poisoned; }

			explicit Poisonable( Poisonable *const i_parent ) : parent( i_parent ) { if( this->parent ) this->parent->poison(); }

		public:
			~Poisonable() { this->validate(); if( this->parent ) this->parent->revive(); }
	};

	class OwningUnlockable;
	class Promiscuous;


    class Unlockable : Poisonable
    {
        private:
			friend Promiscuous;
            ULock *lk;

			struct BlockerBase { protected: BlockerBase()= default; };
			struct Blocker : BlockerBase { private: Blocker()= default; friend Unlockable; };

			struct Poisoner : Poisonable { Poisoner() : Poisonable( nullptr ){} };

			void
			validate() const
			{
				assert( lk->owns_lock() && "This Unlockable object was unlocked by someone, it cannot be used at this time." );
				assert( this->alive() && "This Unlockable object, which is either suitable for r-value only, or experienced a nesting error -- it was poisoned" );
			}

			static Poisoner
			makePoison()
			{
				return Poisoner{};
			}


        public:
            Unlockable( ULock &i_lk ) : Poisonable( nullptr ), lk( &i_lk ) { assert( lk->owns_lock() ); }
			// TODO: I think the Poisoner can use the Poisonable base class as its basis, but for now it's fine.
			// In fact, I think we can detect on the line, rather than wait for the dtor, if the `Poisoner` is a child `Poisonable`.
            Unlockable( ULock &&i_lk, Blocker= {}, Poisoner &&p= makePoison() ) : Poisonable( &p ), lk( &i_lk ) { assert( this->lk->owns_lock() ); }

			Unlockable( OwningUnlockable &&, Blocker= {}, Poisoner &&p= makePoison() );
			Unlockable( OwningUnlockable & );

            Unlockable( const Unlockable &copy )
				: Poisonable( nullptr /* maybe it should be: `&copy` */ ), lk( copy.lk )
				// Ben and I agree that this probably should be a poisoning case.
            {
				this->validate();
            }

			~Unlockable()
			{
				validate();
			}

			[[nodiscard]] Promiscuous promiscuous() &;

			operator ULock &() & { validate(); return *lk; }
    };

	auto
	makeUnlockGuard( ULock &lk )
	{
		return Unlockable( lk );
	}

	class OwningUnlockable : Poisonable
	{
		private:
			friend Promiscuous;
			ULock lk;

			friend Unlockable;

			explicit OwningUnlockable( Poisonable *const p, Mtx &mtx ) : Poisonable( p ), lk( mtx ) { assert( this->lk.owns_lock() ); }

		public:
			OwningUnlockable( Mtx &mtx ) : Poisonable( nullptr ), lk( mtx ) { assert( this->lk.owns_lock() ); }

			[[nodiscard]] Promiscuous promiscuous() &;
	};

	struct Promiscuous : Poisonable
	{
		private:
			friend Unlockable;
			friend OwningUnlockable;

			ULock *lk;

			void
			validate()
			{
				assert( !lk->owns_lock() && "This Promiscuous object was locked by someone, it cannot be used at this time." );
				assert( this->alive() && "This Promiscuous object, which is suitable only for use when not chaste, was poisoned" );
			}

			Promiscuous( OwningUnlockable *const i_ul )
				: Poisonable( i_ul ), lk( &i_ul->lk ) 
			{
				this->lk->unlock();
			}

			Promiscuous( Unlockable *const i_ul )
				: Poisonable( i_ul ), lk( i_ul->lk ) 
			{
				this->lk->unlock();
			}

		public:
			~Promiscuous()
			{
				this->validate();
				this->lk->lock();
			}

			[[nodiscard]] auto
			chaste()
			{
				this->validate();
				return OwningUnlockable( this, *this->lk->mutex() );
			}
	};

	[[nodiscard]] Promiscuous
	Unlockable::promiscuous() &
	{
		this->validate();
		return Promiscuous( this );
	}

	[[nodiscard]] Promiscuous
	OwningUnlockable::promiscuous() &
	{
		this->validate();
		return Promiscuous( this );
	}

	Unlockable::Unlockable( OwningUnlockable &o )
		: Poisonable( nullptr ), lk( &o.lk )
	{
		assert( this->lk->owns_lock() );
	}

	Unlockable::Unlockable( OwningUnlockable &&o, Unlockable::Blocker, Unlockable::Poisoner &&p )
		: Poisonable( &p ), lk( &o.lk )
	{
		std::cerr << "It's dangerous to go alone, so take this poisoner.  This allows promote, via rvalue case catching." << std::endl;
		assert( this->lk->owns_lock() );
	}

	auto
	makeUnlockGuard( Mtx &mtx )
	{
		return OwningUnlockable( mtx );
	}
}

namespace std
{
	template<>
	InfiniteMonkeys::OwningUnlockable &&
	move( InfiniteMonkeys::OwningUnlockable & ) noexcept= delete;

	template<> InfiniteMonkeys::Unlockable &&move( InfiniteMonkeys::Unlockable & ) noexcept= delete;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////

void disallowed();

using namespace InfiniteMonkeys;
void
f1( Unlockable u )
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
f2( Unlockable u )
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
struct checker : std::true_type {};


// Should die at compiletime
template< typename T >
struct checker< T, std::void_t< decltype( std::move( std::declval< T & >() ) ) > >
	: std::false_type {};


#define DISALLOWED_SYNTAX
void
disallowed()
{
	f2( makeUnlockGuard( mtx ) ); // Should work fine at runtime

	auto l2= makeUnlockGuard( mtx ); // Should work fine at runtime

#ifdef DISALLOWED_SYNTAX
	Unlockable l= makeUnlockGuard( mtx ); // Should die at runtime


	static_assert( checker< Unlockable >::value );
	static_assert( checker< OwningUnlockable >::value );
#endif
}
