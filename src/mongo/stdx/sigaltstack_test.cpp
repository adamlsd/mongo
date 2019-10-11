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


void breakpoint() {}

namespace
{
    namespace C
    {
        const auto SignalNumber= SIGINFO;
    }


namespace stdx = ::mongo::stdx;

void
recurse( int n )
{
    std::cout << "We have some stack area at: " << (void *) &n << std::endl;
    std::cerr << "Recursed to depth " << n << std::endl;
    if( n == 10 ) 
    {
        raise( C::SignalNumber );
        while( true );
    }
    asm volatile ("");
    if( getpid() ) recurse( n + 1 );
}

void
handler(int)
{
    int n= 42;
    std::cout << "We have some stack area at: " << (void *) &n << std::endl;
    std::cerr << "Handled." << std::endl;
    while( true ) breakpoint();
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


int main() {
    stdx::thread thr( jumpoff );
    thr.join();
    return EXIT_SUCCESS;
}
