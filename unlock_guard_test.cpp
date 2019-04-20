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

	class OwningUnlockable;

    class Unlockable : boost::noncopyable
    {
        private:
            ULock *lk;
			Poisoner *poisoner= nullptr;

			struct Poisoner
			{
				Unlockable *p= nullptr;
				Poisoner()= default;

				~Poisoner() { if( p ) { p->poison(); } }
			};

			void poison() { lk= nullptr; }
			void
			validate() const
			{
				assert( lk && "This Unlockable object, which is either suitable for r-value only, or experienced a nesting error -- it was poisoned" );
				assert( lk->owns_lock() && "This Unlockable object was unlocked by someone, it cannot be used at this time." );
			}

			static Poisoner
			makePoison()
			{
				return Poisoner{};
			}


        public:
            Unlockable( ULock &i_lk ) : lk( &i_lk ) { assert( lk->owns_lock() ); }
            Unlockable( ULock &&i_lk, Poisoner &&p= makePoison() ) : lk( &i_lk ) { assert( lk->owns_lock() ); p.p= this; }

			Unlockable( OwningUnlockable &&, Poisoner &&p= makePoison() );
			Unlockable( OwningUnlockable & );

            Unlockable( const Unlockable &copy )
				: lk( copy.lk )
            {
				copy.validate();
            }

			Unlockable()= default;

			~Unlockable()
			{
				validate();
				if( poisoner ) p->p= nullptr;
			}

			[[nodiscard]] auto
			promiscuous() &
			{
				validate();

				struct Guardian : boost::noncopyable
				{
					private:
						ULock *lk;
						Unlockable *ul;

						void poison() { ul= nullptr; }
						void
						validate()
						{
							assert( ul && "This Promiscuous object, which is suitable only for use when not chaste, was poisoned" );
							assert( !lk->owns_lock() && "This Promiscuous object was locked by someone, it cannot be used at this time." );
						}

					public:
						Guardian( Unlockable *i_ul )
							: lk( i_ul->lk ), ul( i_ul )
						{
							ul->poison();
							lk->unlock();
						}

						~Guardian()
						{
							validate();
							lk->lock();
							ul->lk= lk;
						}

						[[nodiscard]] auto
						chaste()
						{
							validate();
							lk->lock();
							Unlockable rv= *lk;
							poison();
							return rv;
						}
				};

				return Guardian( this );
			}

			operator ULock &() & { validate(); return *lk; }
    };

	auto
	makeUnlockGuard( ULock &lk )
	{
		return Unlockable( lk );
	}

	class OwningUnlockable : boost::noncopyable
	{
		private:
			ULock lk;

			friend Unlockable;

		public:
			OwningUnlockable( Mtx &mtx ) : lk( mtx ) { assert( lk.owns_lock() ); }

			[[nodiscard]] auto
			promiscuous() &
			{
				struct Guardian : boost::noncopyable
				{
					private:
						ULock *lk;

						void poison() { lk= nullptr; }
						void
						validate()
						{
							assert( lk && "This Promiscuous object, which is suitable only for use when not chaste, was poisoned" );
							assert( !lk->owns_lock() && "This Promiscuous object was locked by someone, it cannot be used at this time." );
						}

					public:
						Guardian( OwningUnlockable *i_ul )
							: lk( &i_ul->lk ) 
						{
							lk->unlock();
						}

						~Guardian()
						{
							validate();
							lk->lock();
						}

						[[nodiscard]] auto
						chaste()
						{
							validate();
							lk->lock();
							Unlockable rv{ *lk };
							poison();
							return rv;
						}
				};

				return Guardian( this );
			}
	};

	Unlockable::Unlockable( OwningUnlockable &o )
		: lk( &o.lk )
	{
		assert( lk->owns_lock() );
	}

	Unlockable::Unlockable( OwningUnlockable &&o, Unlockable::Poisoner &&p )
		: lk( &o.lk )
	{
		std::cerr << "Dangerous, but poisonable promote, via rvalue catching." << std::endl;
		assert( lk->owns_lock() );
		p.p= this;
	}

	auto
	makeUnlockGuard( Mtx &mtx )
	{
		return OwningUnlockable( mtx );
	}
}

namespace std
{
	template<> InfiniteMonkeys::OwningUnlockable &&move( InfiniteMonkeys::OwningUnlockable & )= delete;
	template<> InfiniteMonkeys::Unlockable &&move( InfiniteMonkeys::Unlockable & )= delete;
}

void disallowed();

using namespace InfiniteMonkeys;
void
f1( Unlockable u )
{
	std::cerr << "F1 called" << std::endl;
	auto prom= u.promiscuous();
	auto chaste= prom.chaste();
	auto p2= chaste.promiscuous();

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
