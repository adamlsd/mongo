/**
 *    Copyright (C) 2019-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include "mongo/stdx/exception.h"

#include <unistd.h>

#include <stdlib.h>

#include <iostream>
#include <map>
#include <algorithm>
#include <string>
#include <stdexcept>
#include <exception>

#include "mongo/stdx/thread.h"
#include "mongo/stdx/testing/thread_helpers.h"

namespace
{
    namespace stdx = ::mongo::stdx;

    namespace C
    {
        const auto SignalNumber= SIGINFO;
    }

    std::atomic< bool > blockage{ true };
    std::atomic< void * > address;

    void
    recurse( const int n )
    {
        if( n == 10 )
        {
            raise( C::SignalNumber );
        }
        else recurse( n + 1 );
    }

    void
    handler( const int n )
    {
        address= (void *) &n;
        blockage= false;
    }

    void
    installSignalHandler()
    {
        struct sigaction action{};
        action.sa_handler= handler;
        action.sa_flags= SA_ONSTACK;
        sigemptyset( &action.sa_mask );
        const auto ec= ::sigaction( C::SignalNumber, &action, nullptr );
        int myErrno= errno;
        std::cerr << "Got ec: " << ec;
        if( ec != 0 )
        {
            std::cerr << " and errno is: " << myErrno;
        }
        std::cerr << std::endl;
    }

    void
    setupSignalMask()
    {
        sigset_t sigset;
        sigemptyset( &sigset );
        const auto ec= sigprocmask( SIG_UNBLOCK, &sigset, nullptr );
        int myErrno= errno;
        std::cerr << "Got ec: " << ec;
        if( ec != 0 )
        {
            std::cerr << " and errno is: " << myErrno;
        }
        std::cerr << std::endl;
    }


    std::mutex thrmtx;
    std::condition_variable cv;
    std::atomic< void * > mainAddress;

    void
    jumpoff()
    {
        auto lk= std::unique_lock( thrmtx );
        mainAddress= &lk;

        installSignalHandler();
        setupSignalMask();

        recurse( 0 );
        cv.notify_one();
        cv.wait( lk );
        std::cerr << "Notified in child" << std::endl;
    }

}  // namespace


int
main()
{
    mongo::stdx::testing::ThreadInformationListener listener;

    auto lk= std::unique_lock( thrmtx );
    stdx::thread thr( jumpoff );
    const auto id= thr.get_id();
    cv.wait( lk );
    //const auto [ pos, amt ]= mongo::getInformationForThread( thr );
    const auto [ pos, amt ]= listener.getMapping( thr ).altStack;
    
    std::cout << "Position is: " << pos << std::endl;
    std::cout << "Stack's limit is: " << amt << std::endl;
    std::cout << "Local was at: " << address << std::endl;

    const auto bAddress= static_cast< const std::byte * >( static_cast< void * >( address ) );
    const auto bPos= static_cast< const std::byte * >( pos );
    if( bAddress < bPos || bAddress > ( bPos + amt ) )
    {
        std::cout << "Address was out of bounds" << std::endl;
    }
    else std::cout << "Address was in bounds" << std::endl;

    const auto bmAddress= static_cast< const std::byte * >( static_cast< void * >( mainAddress ) );
    if( bmAddress < bPos || bmAddress > ( bPos + amt ) )
    {
        std::cout << "Main address was out of bounds (good)" << std::endl;
    }
    else std::cout << "Main Address was in bounds (bad)" << std::endl;

    lk.unlock();
    cv.notify_one();
    thr.join();

    try
    {
        listener.getMapping( id );
        abort();
    }
    catch( const std::out_of_range & )
    {
        std::cerr << "Identifier " << id << " was not found, as expected." << std::endl;
    }
    return EXIT_SUCCESS;
}
