#include <iostream>
#include <chrono>
#include <thread>
#include "profit.hpp"

void c( int x ) {_
#   define sleep(secs) std::this_thread::sleep_for( std::chrono::microseconds( int(secs * 1000000) ) )
    while( x-- > 0 ) {_
        sleep(0.0125);
    }
}
void b( int x ) {_
    while( x-- > 0 ) {_
        sleep(0.0125);
        c(x);
    }
}
void a( int x ) {_
    while( x-- > 0 ) {_
        sleep(0.0125);
        b(x);
    }
}

int main() {{_
        a(10);
    }
    profit::report( std::cout );
}
