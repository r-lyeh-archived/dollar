# Profit :moneybag: <a href="https://travis-ci.org/r-lyeh/profit"><img src="https://api.travis-ci.org/r-lyeh/profit.svg?branch=master" align="right" /></a>

- Profit is a generic CPU profiler (C++11)
- Profit is cross-platform. No external dependencies.
- Profit is tiny, self-contained, header-only.
- Profit is zlib/libpng licensed.

## Quick tutorial (tl;dr)
```c++
#include "profit.hpp"
int main() { $ // <-- put a dollar after every curly brace to determinate cpu cost of the scope
    for( int x = 0; x < 10000000; ++x ) { $ // <-- functions or loops will apply too
        // slow stuff...
    }
    profit::report(std::cout); // report stats to std::cout
    profit::reset();           // reset current scope
    profit::reset_all();       // reset all scopes (like when entering a new frame)
}
```

## API
- Determinate cpu cost of any scope by putting a `$` dollar sign in front of it.
- Or just insert an `profit raii("name")` object.
- The predefined `$` macro just adds function name, line and number to the raii object name.
- Use `profit::report(ostream)` to log report to any ostream object (like `std::cout`).
- Optionally, `profit::reset(name)` or `profit::reset_all()` to reset counters.

## API: auto format tool
- @snail23 was generous enough to create this [auto format tool that write dollars for you](https://github.com/snailsoft/format)

## Build options
- Profit is enabled by default. Compile with `-D$=` to disable it.
- Define PROFIT_USE_OPENMP to use OpenMP timers (instead of <chrono>)
- Define PROFIT_MAX_SAMPLES to change number of maximum instrumented samples.

## Todo
- json, csv, tsv, std::vector<float> (graph? opengl?)
- name %d

## Showcase
```c++
~/profit> cat ./sample.cc

#include <iostream>
#include <chrono>
#include <thread>
#include "profit.hpp"

void c( int x ) { $
#   define sleep(secs) std::this_thread::sleep_for( std::chrono::microseconds( int(secs * 1000000) ) )
    while( x-- > 0 ) { $
        sleep(0.0125);
    }
}
void b( int x ) { $
    while( x-- > 0 ) { $
        sleep(0.0125);
        c(x);
    }
}
void a( int x ) { $
    while( x-- > 0 ) { $
        sleep(0.0125);
        b(x);
    }
}

int main() {{ $
        a(10);
    }
    profit::report( std::cout );
}

~/profit> g++ sample.cc -std=c++11 && ./a.out
+------------------------------------------------------------+
|   min |   avg |   max |     % |   # | scope                |
+------------------------------------------------------------+
|  0.00 |  0.00 |  0.00 |  0.00 |   1 | main(sample.cc:25)   |
|  0.00 |  0.00 |  0.00 |  0.00 |   1 | a(sample.cc:18)      |
|  5.71 |  5.71 |  5.71 |  5.71 |  10 |   a(sample.cc:19)    |
|  0.00 |  0.00 |  0.00 |  0.00 |  10 |    b(sample.cc:12)   |
| 25.71 | 25.71 | 25.71 | 25.71 |  45 |     b(sample.cc:13)  |
|  0.00 |  0.00 |  0.00 |  0.00 |  45 |      c(sample.cc:6)  |
| 68.57 | 68.57 | 68.57 | 68.57 | 120 |       c(sample.cc:8) |
+------------------------------------------------------------+

~/profit> g++ sample.cc -std=c++11 -D_= && ./a.out
~/profit>
```

## Changelog
- v1.0.0 (2015/08/02)
  - Macro renamed
- v0.0.0 (2015/03/13)
  - Initial commit

## Licenses
- [Profit](https://github.com/r-lyeh/profit), zlib/libpng licensed.
- Profit is based on code by Steve Rabin and Richard "superpig" Fine.
