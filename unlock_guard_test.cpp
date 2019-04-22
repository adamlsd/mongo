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

		struct Evil {};
    //}

	class Poisonable : boost::noncopyable
	{
		private:
			struct C { enum State { Healthy, Poisoned }; };
			using State= C::State;

			State state= C::Healthy;
			Poisonable *const parent= nullptr;

		protected:
			[[nodiscard]] bool alive() const { return this->state != C::Poisoned; }
			void validate() const { assert( alive() ); }

			void revive() & { this->state= C::Healthy; }
			void poison() & { this->state= C::Poisoned; }

			explicit Poisonable( Poisonable *const i_parent ) : parent( i_parent ) { if( this->parent ) this->parent->poison(); }

		public:
			~Poisonable() { this->validate(); if( this->parent ) this->parent->revive(); }
	};

	class OwningUnlockable;

    class Unlockable : Poisonable
    {
        private:
            ULock *lk;
			struct Poisoner;
			Poisoner *poisoner= nullptr;

			struct Poisoner
			{
				Unlockable *p= nullptr;
				Poisoner()= default;

				~Poisoner() { if( p ) { p->poison(); } }
			};

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
            Unlockable( ULock &&i_lk, Poisoner &&p= makePoison() ) : Poisonable( nullptr ), lk( &i_lk ) { assert( this->lk->owns_lock() ); p.p= this; }

			Unlockable( OwningUnlockable &&, Poisoner &&p= makePoison() );
			Unlockable( OwningUnlockable & );

            Unlockable( const Unlockable &copy )
				: Poisonable( nullptr /* maybe it should be: `&copy` */ ), lk( copy.lk )
            {
				copy.validate();
            }

			~Unlockable()
			{
				validate();
				if( poisoner ) poisoner->p= nullptr;
			}

			[[nodiscard]] auto
			promiscuous() &
			{
				// This one doesn't work, I think.
				validate();

				struct Promiscuous : Poisonable
				{
					private:
						ULock *lk;

						void
						validate()
						{
							assert( this->alive() && "This Promiscuous object, which is suitable only for use when not chaste, was poisoned" );
							assert( !this->lk->owns_lock() && "This Promiscuous object was locked by someone, it cannot be used at this time." );
						}

					public:
						explicit Promiscuous( Unlockable *const i_ul )
							: Poisonable( i_ul ), lk( i_ul->lk )
						{
							lk->unlock();
						}

						~Promiscuous()
						{
							this->validate();
							this->lk->lock();
						}

#if 0
						[[nodiscard]] auto
						chaste()
						{
							this->validate();
							this->lk->lock();
							// TODO: Need to create ownership here.
						}
#endif
				};

				return Promiscuous( this );
			}

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
			ULock lk;

			friend Unlockable;

			explicit OwningUnlockable( Poisonable *const p, Mtx &mtx ) : Poisonable( p ), lk( mtx ) { assert( this->lk.owns_lock() ); }

		public:
			OwningUnlockable( Mtx &mtx ) : Poisonable( nullptr ), lk( mtx ) { assert( this->lk.owns_lock() ); }

			[[nodiscard]] auto
			promiscuous() &
			{
				this->validate();

				struct Promiscuous : Poisonable
				{
					private:
						ULock *lk;

						void
						validate()
						{
							assert( !lk->owns_lock() && "This Promiscuous object was locked by someone, it cannot be used at this time." );
							assert( this->alive() && "This Promiscuous object, which is suitable only for use when not chaste, was poisoned" );
						}

					public:
						Promiscuous( OwningUnlockable *const i_ul )
							: Poisonable( i_ul ), lk( &i_ul->lk ) 
						{
							this->lk->unlock();
						}

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

				return Promiscuous( this );
			}
	};

	Unlockable::Unlockable( OwningUnlockable &o )
		: Poisonable( nullptr ), lk( &o.lk )
	{
		assert( this->lk->owns_lock() );
	}

	Unlockable::Unlockable( OwningUnlockable &&o, Unlockable::Poisoner &&p )
		: Poisonable( nullptr ), lk( &o.lk ), poisoner( &p )
	{
		std::cerr << "It's dangerous to go alone, so take this poisoner.  This allows promote, via rvalue case catching." << std::endl;
		assert( this->lk->owns_lock() );
		this->poisoner->p= this;
	}

	auto
	makeUnlockGuard( Mtx &mtx )
	{
		return OwningUnlockable( mtx );
	}
}

namespace std
{
	template<> InfiniteMonkeys::OwningUnlockable &&move( InfiniteMonkeys::OwningUnlockable & ) noexcept= delete;
	template<> InfiniteMonkeys::Unlockable &&move( InfiniteMonkeys::Unlockable & ) noexcept= delete;
}

void disallowed();

using namespace InfiniteMonkeys;
void
f1( Unlockable u )
{
	std::cerr << "F1 called" << std::endl;
	auto prom= u.promiscuous();
	auto chaste= prom.chaste();
	//auto p2= chaste.promiscuous();

	std::cerr << "Got all" << std::endl;

	//auto c2= prom.chaste();
}

void
f2( Unlockable u )
{
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
		f1( l1 );
	}
	{
		ULock lk( mtx );
		f1( makeUnlockGuard( lk ) );
		auto l2= makeUnlockGuard( lk );
		f1( lk );
		f1( l2 );
	}



	f1( ULock( mtx ) );
}

#define DISALLOWED_SYNTAX
void
disallowed()
{
#ifdef DISALLOWED_SYNTAX
	//Unlockable l= makeUnlockGuard( mtx ); // Should die

	f2( makeUnlockGuard( mtx ) ); // Might want to allow?

	auto l2= makeUnlockGuard( mtx );
	//f1( std::move( l2 ) ); // Should die
#endif
}
