// Profit is a simple c++11 instrumented profiler, header-only and zlib/libpng licensed.
// - rlyeh ~~ listening to Team Ghost / High hopes.

/* usage:
#include "profit.hpp"
int main() { $ // <-- put a dollar after every curly brace to determinate cpu cost of the scope
    for( int x = 0; x < 10000000; ++x ) { $ // <-- functions or loops will apply too
        // slow stuff...
    }
    profit::text(std::cout);   // report stats to std::cout in text format; see also csv(), tsv() and markdown()
    profit::clear();           // clear all scopes (like when entering a new frame)
}
// profit is enabled by default. compile with -D$= to disable all profiling */

#ifndef $
#pragma once

#define PROFIT_VERSION "1.1.0" /* (2016/05/03) New tree view and CPU meters (ofxProfiler style); Smaller implementation;
#define PROFIT_VERSION "1.0.1" // (2015/11/15) Fix win32 `max()` macro conflict
#define PROFIT_VERSION "1.0.0" // (2015/08/02) Macro renamed
#define PROFIT_VERSION "0.0.0" // (2015/03/13) Initial commit */

#include <stdio.h>

#include <algorithm>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifdef PROFIT_USE_OMP
#   include <omp.h>
#else
#   include <chrono>
#endif

#ifndef PROFIT_MAX_TRACES
#define PROFIT_MAX_TRACES 512
#endif

#define PROFIT_GLUE(a,b)    a##b 
#define PROFIT_JOIN(a,b)    PROFIT_GLUE(a,b)
#define PROFIT_UNIQUE(sym)  PROFIT_JOIN(sym, __LINE__)
#define PROFIT_STRINGIFY(x) #x
#define PROFIT_TOSTRING(x)  PROFIT_STRINGIFY(x)

#ifdef _MSC_VER
#define PROFIT(name)  profit::sampler PROFIT_UNIQUE(profit_sampler_)(name);
#define $             profit::sampler PROFIT_UNIQUE(profit_sampler_)(std::string(__FUNCTION__) + " (" __FILE__ ":" PROFIT_TOSTRING(__LINE__) ")" );
#else
#define PROFIT(name)  profit::sampler PROFIT_UNIQUE(profit_sampler_)(name);
#define $             profit::sampler PROFIT_UNIQUE(profit_sampler_)(std::string(__PRETTY_FUNCTION__) + " (" __FILE__ ":" PROFIT_TOSTRING(__LINE__) ")" );
#endif

namespace profit
{
    template < typename T >
    inline T* singleton() {
        static T tVar;
        return &tVar;
    }
    inline double now() {
#           ifdef PROFIT_USE_OMP
            static auto const epoch = omp_get_wtime(); 
            return omp_get_wtime() - epoch;
#           else
            static auto const epoch = std::chrono::steady_clock::now(); // milli ms > micro us > nano ns
            return std::chrono::duration_cast< std::chrono::microseconds >( std::chrono::steady_clock::now() - epoch ).count() / 1000000.0;
#           endif
    };
    inline std::vector< std::string > tokenize( const std::string &self, const std::string &delimiters ) {
        unsigned char map [256] = {};
        for( const unsigned char &ch : delimiters ) {
            map[ ch ] = '\1';
        }
        std::vector< std::string > tokens(1);
        for( const unsigned char &ch : self ) {
            /**/ if( !map[ch]             ) tokens.back().push_back( char(ch) );
            else if( tokens.back().size() ) tokens.push_back( std::string() );
        }
        while( tokens.size() && !tokens.back().size() ) tokens.pop_back();
        return tokens;
    }
    struct Node {
        std::string name;
        std::string value;
        std::vector<Node> children;

        Node( const std::string &name = std::string() ) : name(name) 
        {}

        template<size_t N>
        Node( const char (&name)[N] ) : name(name) 
        {}
    };
    inline void tree_printer( const Node &node, std::string indent, bool leaf, std::ostream &out ) {
        if( leaf ) {
            out << indent << "\\-" << node.name << node.value << std::endl;
            indent += "  ";
        } else {
            out << indent << "|-" << node.name << node.value << std::endl;
            indent += "| ";
        }
        for( auto end = node.children.size(), it = end - end; it < end; ++it ) {
            tree_printer( node.children[it], indent, it == (end - 1), out );
        }
    }
    inline void tree_printer( const Node &node, std::ostream &out = std::cout ) {
        tree_printer( node, "", true, out );
    }
    inline void tree_recreate_branch( Node &node, const std::vector<std::string> &names ) {
        auto *where = &node;
        for( auto &name : names ) {
            bool found = false;
            for( auto &it : where->children ) {
                if( it.name == name ) {
                    where = &it;
                    found = true;
                    break;
                }
            }
            if( !found ) {
                where->children.push_back( Node(name) );
                where = &where->children.back();
            }
        }
    }
    class profiler
    {
        std::vector<std::string> stack;
        bool paused;

        public:

        struct info {
            bool paused = false;
            unsigned hits = 0;
            double begin = 0, end = 0, total = 0;
            std::string title;

            info()
            {}

            info( const std::string &title ) : title(title)
            {}
        };

        profiler() : stack(PROFIT_MAX_TRACES, std::string())
        {}

        info &in( const std::string &title ) {
            stack.push_back( stack.empty() ? title : stack.back() + ";" + title ) ;

            auto &id = stack.back();

            if( counters.find( id ) == counters.end() ) {
                counters[ id ] = info ( stack.back() );
            }

            auto &sample = counters[ id ];

            sample.hits ++;
            sample.begin = profit::now();
            sample.paused = paused;

            return sample;
        }

        void out( info &sample ) {
            sample.end = profit::now();
            sample.total += ( sample.paused ? 0.f : sample.end - sample.begin );
            stack.pop_back();
        }

        void print( std::ostream &out, const char *tab = ",", const char *feed = "\r\n" ) const {
            auto inital_matches = []( const std::string &abc, const std::string &txt ) -> unsigned {
                unsigned c = 0;
                for( auto end = (std::min)(abc.size(), txt.size()), it = end - end; it < end; ++it, ++c ) {
                    if( abc[it] != txt[it] ) break;
                }
                return c;
            };
            auto starts_with = [&]( const std::string &abc, const std::string &txt ) -> bool {
                return inital_matches( abc, txt ) == txt.size();
            };

            // update times
            std::vector< std::string > sort;
            auto copy = *this;
            
            {
                // sorted tree
                std::vector< std::pair<std::string &, info *> > az_tree;

                for( auto &it : copy.counters ) {
                    auto &info = it.second;
                    az_tree.emplace_back( info.title, &info );
                }

                std::sort( az_tree.begin(), az_tree.end() );
                std::reverse( az_tree.begin(), az_tree.end() );

                // update time hierarchically
                for( size_t i = 0; i < az_tree.size(); ++i ) {
                    for( size_t j = i + 1; j < az_tree.size(); ++j ) {
                        if( starts_with( az_tree[ i ].first, az_tree[ j ].first ) ) {
                            az_tree[ j ].second->total -= az_tree[ i ].second->total;
                        }
                    }
                }
            }
            

            double total = 0;

            std::vector<std::string> list;

            for( auto &it : copy.counters ) {
                auto &info = it.second;
                total += info.total;
                list.push_back( info.title );
            }

            size_t i = 1;
            std::string format, sep, graph, buffer(1024, '\0');

            if( 1 ) {
                // flat2tree
                static char pos = 0;
                Node root( std::string() + "\\|/-"[(++pos)%4] );
                for( auto &entry : list ) {
                    auto split = tokenize( entry, ";" );
                    tree_recreate_branch( root, split );
                }
                std::stringstream ss;
                tree_printer( root, ss );
                list = tokenize( ss.str(), "\r\n" );
                static size_t maxlen = 0;
                for( auto &it : list ) {
                    maxlen = (std::max)(maxlen, it.size());
                }
                for( auto &it : list ) {
                    /**/ if( maxlen > it.size() ) it += std::string( maxlen - it.size(), ' ' );
                    else if( maxlen < it.size() ) it.resize( maxlen );
                }
                // pre-loop
                for( auto &it : std::vector<std::string>{ "%4d.","%s","[%s]","%5.2f%% CPU","(%9.3fms)","%5d hits",feed } ) {
                    format += sep + it;
                    sep = tab;
                }
                // loop
                for( auto &it : copy.counters ) {
                    auto &info = it.second;
                    double cpu = info.total * 100.0 / total;
                    graph = std::string( int(cpu/10), '=' ) + std::string( 10-int(cpu/10), '.' );
#ifdef _MSC_VER
                    sprintf_s( &buffer[0], 1024,
#else
                    sprintf( &buffer[0], 
#endif
                    format.c_str(), i, list[i].c_str(), graph.c_str(), cpu, (float)(info.total * 1000), info.hits );
                    sort.push_back( &buffer[0] );
                    ++i;
                }
                // flush
                //std::sort( sort.begin(), sort.end() );
                for( auto &it : sort ) {
                    out << it;
                }
            }
        }

        void pause( bool paused_ ) {
            paused = paused_;
        }

        bool is_paused() const {
            return paused;
        }

        void clear() {
            bool p = paused;
            *this = profiler();
            paused = p;
        }

        private: std::map< std::string, info > counters;
    };

    class sampler {
        sampler();
        sampler( const sampler & );
        sampler& operator=( const sampler & );
        profiler::info *handle;

        public: // public api

        explicit sampler( const std::string &title ) {
            handle = &singleton<profiler>()->in( title );
        }

        ~sampler() {
            singleton<profiler>()->out( *handle );
        }
    };

    inline void csv( std::ostream &os ) {
        singleton<profiler>()->print(os, ",");
    }

    inline void tsv( std::ostream &os ) {
        singleton<profiler>()->print(os, "\t");
    }

    inline void markdown( std::ostream &os ) {
        singleton<profiler>()->print(os, "|");
    }

    inline void text( std::ostream &os ) {
        singleton<profiler>()->print(os, " ");
    }

    inline void pause( bool paused ) {
        singleton<profiler>()->pause( paused );
    }

    inline bool is_paused() {
        return singleton<profiler>()->is_paused();
    }

    inline void clear() {
        singleton<profiler>()->clear();
    }
}

#else

#include <iostream>

namespace profit {

    inline void csv( std::ostream &cout ) {}
    inline void tsv( std::ostream &cout ) {}
    inline void text( std::ostream &cout ) {}
    inline void chrome( std::ostream &cout ) {}
    inline void markdown( std::ostream &cout ) {}
    inline void pause( bool paused ) {}
    inline bool is_paused() {}
    inline void clear() {}

};

#endif

#ifdef PROFIT_BUILD_DEMO
#include <iostream>
#include <chrono>
#include <thread>
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
#endif
