# Profit :moneybag: <a href="https://travis-ci.org/r-lyeh/profit"><img src="https://api.travis-ci.org/r-lyeh/profit.svg?branch=master" align="right" /></a>

- [x] Generic CPU instrumented profiler (C++11)
- [x] Cross-platform. No external dependencies.
- [x] Tiny, self-contained, header-only.
- [x] ZLIB/libPNG licensed.

## Quick tutorial (tl;dr)
```c++
#include "profit.hpp"
int main() { $ // <-- put a dollar after every curly brace to determinate cpu cost of the scope
    for( int x = 0; x < 10000000; ++x ) { $ // <-- functions or loops will apply too
        // slow stuff...
    }
    profit::text(std::cout);   // report stats to std::cout in text format; see also csv(), tsv() and markdown()
    profit::clear();           // clear all scopes (like when entering a new frame)
}
```

## API
- Determinate the CPU cost of any scope by putting a `$` dollar sign in front of it.
- Or just insert an `profit raii("name")` object.
- The predefined `$` macro just adds function name, line and number to the RAII object name.
- Use `profit::text(ostream)` to print a text report in any ostream object (like `std::cout`).
- See also `profit::csv(ostream)`, `profit::tsv(ostream)`, and `profit::markdown(ostream)`.
- Finally, use `profit::clear()` when entering a new frame.

## API: auto format tool
- @snail23 was generous enough to create this [auto format tool that write dollars for you](https://github.com/snailsoft/format)

## Build options
- Profit is enabled by default. Compile with `-D$=` to disable it.
- Define PROFIT_USE_OPENMP to use OpenMP timers (instead of `<chrono>` timers)
- Define PROFIT_MAX_TRACES to change number of maximum instrumented samples (default: 512).

## Upcoming
- New chrome-profiler json traces
- std::vector<float> (graph? opengl?)
- "name %d" format

## Showcase
```c++
~/profit> cat ./sample.cc

#include <iostream>
#include <chrono>
#include <thread>
#include "profit.hpp"

#define sleep(secs) std::this_thread::sleep_for( std::chrono::microseconds( int(secs * 1000000) ) )

void x( int counter ) { $
    while( counter-- > 0 ) { $
        sleep(0.00125);
    }
}
void c( int counter ) { $
    while( counter-- > 0 ) { $
        sleep(0.00125);
    }
}
void y( int counter ) { $
    while( counter-- > 0 ) { $
        sleep(0.00125);
        if( counter % 2 ) c(counter); else x(counter);
    }
}
void a( int counter ) { $
    while( counter-- > 0 ) { $
        sleep(0.00125);
        y(counter);
    }
}

int main() {{ $
        a(10);
    }

    // display results
    profit::text(std::cout);

    // clear next frame
    profit::clear();
}

~/profit> g++ sample.cc -std=c++11 && ./a.out
   1. \-|                                [..........]  0.00% CPU (    0.007ms)     1 hits
   2.   \-main (profit.hpp:442)          [..........]  0.03% CPU (    0.113ms)     1 hits
   3.     \-a (profit.hpp:435)           [..........]  5.61% CPU (   19.633ms)    10 hits
   4.       \-a (profit.hpp:436)         [..........]  0.33% CPU (    1.163ms)    10 hits
   5.         \-y (profit.hpp:429)       [==........] 25.60% CPU (   89.556ms)    45 hits
   6.           \-y (profit.hpp:430)     [..........]  0.63% CPU (    2.187ms)    20 hits
   7.             |-x (profit.hpp:419)   [===.......] 33.52% CPU (  117.273ms)    60 hits
   8.             | \-x (profit.hpp:420) [..........]  0.59% CPU (    2.070ms)    25 hits
   9.             \-c (profit.hpp:424)   [===.......] 33.68% CPU (  117.843ms)    60 hits

~/profit> g++ sample.cc -std=c++11 -D$= && ./a.out
~/profit>
```

## Changelog
- v1.1.0 (2016/05/03): New tree view and CPU meters (ofxProfiler style); Smaller implementation;
- v1.0.1 (2015/11/15): Fix win32 `max()` macro conflict
- v1.0.0 (2015/08/02): Macro renamed
- v0.0.0 (2015/03/13): Initial commit
