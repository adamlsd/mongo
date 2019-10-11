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

#include "mongo/stdx/thread.h"

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
            while( blockage );
            
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
    jumpoff()
    {
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


        recurse( 0 );
    }
}  // namespace


int
main()
{
    stdx::thread thr( jumpoff );
    const void *const pos= mongo::getStackForThread( thr );
    const std::size_t amt= mongo::getStackSizeForThread( thr );
    std::cout << "Position is: " << pos << std::endl;
    std::cout << "Stack's limit is: " << amt << std::endl;
    thr.join();
    std::cout << "Local was at: " << address << std::endl;

    const auto bAddress= static_cast< const std::byte * >( static_cast< void * >( address ) );
    const auto bPos= static_cast< const std::byte * >( pos );
    if( bAddress < bPos || bAddress > ( bPos + amt ) )
    {
        std::cout << "Address was out of bounds" << std::endl;
    }
    else std::cout << "Address was in bounds" << std::endl;
    return EXIT_SUCCESS;
}
