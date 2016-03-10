// Dollar is a generic instrumented CPU profiler (C++11), header-only and zlib/libpng licensed.
// Dollar outputs traces for chrome:://tracing and also ASCII, CSV, TSV and Markdown text formats.
// - rlyeh ~~ listening to Team Ghost / High hopes.

/* usage:
#include "dollar.hpp" // dollar is enabled by default. compile with -D$= to disable any profiling 
int main() { $ // <-- put a dollar after every curly brace to determinate cpu cost of the scope
    for( int x = 0; x < 10000000; ++x ) { $ // <-- functions or loops will apply too
        // slow stuff...
    }
    std::ofstream file("chrome.json");
    dollar::chrome(file);                         // write tracing results to a json file (for chrome://tracing embedded profiler) 
    dollar::text(std::cout);                      // report stats to std::cout in text format; see also csv(), tsv() and markdown()
    dollar::clear();                              // clear all scopes (like when entering a new frame)
}
*/

#pragma once

#define DOLLAR_VERSION "1.2.0" /* (2016/10/03) Add chrome://tracing profiler support; Project renamed;
#define DOLLAR_VERSION "1.1.0" /* (2016/05/03) New tree view and CPU meters (ofxProfiler style); Smaller implementation;
#define DOLLAR_VERSION "1.0.1" // (2015/11/15) Fix win32 `max()` macro conflict
#define DOLLAR_VERSION "1.0.0" // (2015/08/02) Macro renamed
#define DOLLAR_VERSION "0.0.0" // (2015/03/13) Initial commit */

#ifdef $

#include <iostream>

namespace dollar {

    inline void csv( std::ostream &cout ) {}
    inline void tsv( std::ostream &cout ) {}
    inline void text( std::ostream &cout ) {}
    inline void chrome( std::ostream &cout ) {}
    inline void markdown( std::ostream &cout ) {}
    inline void pause( bool paused ) {}
    inline bool is_paused() {}
    inline void clear() {}

};

#else
#include <stdio.h>

#include <algorithm>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <thread>

#ifdef DOLLAR_USE_OMP
#   include <omp.h>
#else
#   include <chrono>
#endif

#ifdef _MSC_VER
#include <process.h>
#else
#include <unistd.h>
#endif

#ifndef DOLLAR_MAX_TRACES
#define DOLLAR_MAX_TRACES 512
#endif

#ifndef DOLLAR_CPUMETER_WIDTH 
#define DOLLAR_CPUMETER_WIDTH 10
#endif

#define DOLLAR_GLUE(a,b)    a##b 
#define DOLLAR_JOIN(a,b)    DOLLAR_GLUE(a,b)
#define DOLLAR_UNIQUE(sym)  DOLLAR_JOIN(sym, __LINE__)
#define DOLLAR_STRINGIFY(x) #x
#define DOLLAR_TOSTRING(x)  DOLLAR_STRINGIFY(x)

#ifdef _MSC_VER
#define DOLLAR(name)  dollar::sampler DOLLAR_UNIQUE(dollar_sampler_)(name);
#define $             dollar::sampler DOLLAR_UNIQUE(dollar_sampler_)(std::string(__FUNCTION__) + " (" __FILE__ ":" DOLLAR_TOSTRING(__LINE__) ")" );
#else
#define DOLLAR(name)  dollar::sampler DOLLAR_UNIQUE(dollar_sampler_)(name);
#define $             dollar::sampler DOLLAR_UNIQUE(dollar_sampler_)(std::string(__PRETTY_FUNCTION__) + " (" __FILE__ ":" DOLLAR_TOSTRING(__LINE__) ")" );
#endif

namespace dollar
{
    template < typename T >
    inline T* singleton() {
        static T tVar;
        return &tVar;
    }
    inline double now() {
#   ifdef DOLLAR_USE_OMP
        static auto const epoch = omp_get_wtime(); 
        return omp_get_wtime() - epoch;
#   else
        static auto const epoch = std::chrono::steady_clock::now(); // milli ms > micro us > nano ns
        return std::chrono::duration_cast< std::chrono::microseconds >( std::chrono::steady_clock::now() - epoch ).count() / 1000000.0;
#   endif
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
    template<typename info>
    struct Node {
        std::string name;
        info *value;
        std::vector<Node> children;

        Node( const std::string &name, info *value = 0 ) : name(name), value(value)
        {}

        void tree_printer( std::string indent, bool leaf, std::ostream &out ) const {
            if( leaf ) {
                out << indent << "+-" << name << std::endl;
                indent += "  ";
            } else {
                out << indent << "|-" << name << std::endl;
                indent += "| ";
            }
            for( auto end = children.size(), it = end - end; it < end; ++it ) {
                children[it].tree_printer( indent, it == (end - 1), out );
            }
        }
        void tree_printer( std::ostream &out = std::cout ) const {
            tree_printer( "", true, out );
        }
        Node&tree_recreate_branch( const std::vector<std::string> &names ) {
            auto *where = &(*this);
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
            return *where;
        }
        template<typename FN0, typename FN1, typename FN2>
        void tree_walker( const FN0 &method, const FN1 &pre_children, const FN2 &post_chilren  ) const {
            if( children.empty() ) {
                method( *this );
            } else {
                pre_children( *this );
                for( auto &child : children ) {
                    child.tree_walker( method, pre_children, post_chilren );
                }
                post_chilren( *this );
            }
        }
    };
    class profiler
    {
        std::vector<std::string> stack;
        bool paused;

        public:

        struct info {
            bool paused = false;
            unsigned hits = 0;
            double current = 0, total = 0;
#ifdef _MSC_VER
            int pid = 0;
#else
            pid_t pid = 0;
#endif
            std::thread::id tid;
            std::string title;

            info()
            {}

            info( const std::string &title ) : title(title)
            {}

            inline friend
            std::ostream &operator<<( std::ostream &os, const info &k ) {
                os << "title:" << tokenize(k.title, ";").back() << std::endl;
                os << "paused:" << k.paused << std::endl;
                os << "hits:" << k.hits << std::endl;
                os << "current:" << k.current << std::endl;
                os << "total:" << k.total << std::endl;
                os << "pid:" << k.pid << std::endl;
                os << "tid:" << k.tid << std::endl;
                return os;
            }
        };

        profiler() {
            stack.reserve( DOLLAR_MAX_TRACES );
        }

        info &in( const std::string &title ) {
#ifdef _MSC_VER
            auto pid = _getpid();
#else
            auto pid = getpid();
#endif
            auto tid = std::this_thread::get_id();

            //std::stringstream header;
            //header << pid << "/" << tid << "/" << title;
            //stack.push_back( stack.empty() ? header.str() : stack.back() + ";" + header.str() );
            stack.push_back( stack.empty() ? title : stack.back() + ";" + title );

            auto &id = stack.back();

            if( counters.find( id ) == counters.end() ) {
                counters[ id ] = info ( stack.back() );
            }

            auto &sample = counters[ id ];

            sample.hits ++;
            sample.current = -dollar::now();

            sample.pid = pid;
            sample.tid = tid;

            return sample;
        }

        void out( info &sample ) {
            sample.current += dollar::now();
            sample.total += ( sample.paused ? 0.f : sample.current );
            stack.pop_back();
        }

        template<bool for_chrome>
        void print( std::ostream &out, const char *tab = ",", const char *feed = "\r\n" ) const {
            auto inital_matches = []( const std::string &text, const std::string &abc ) -> unsigned {
                unsigned c = 0;
                for( auto end = (std::min)(text.size(), abc.size()), it = end - end; it < end; ++it, ++c ) {
                    if( text[it] != abc[it] ) break;
                }
                return c;
            };
            auto starts_with = [&]( const std::string &text, const std::string &abc ) -> bool {
                return inital_matches( text, abc ) == abc.size();
            };

            // create a copy of the class to modify it, so this method is still const
            auto copy = *this;

            // finish any active scope
            while( !copy.stack.empty() ) {
                auto &current = copy.counters[ stack.back() ];
                copy.out( current );
            }

            // update time hierarchically
            {
                // sorted tree
                std::vector< std::pair<std::string, info *> > az_tree;

                for( auto &it : copy.counters ) {
                    auto &info = it.second;
                    az_tree.emplace_back( info.title, &info );
                }

                std::sort( az_tree.begin(), az_tree.end() );
                std::reverse( az_tree.begin(), az_tree.end() );

                // here's the magic
                for( size_t i = 0; i < az_tree.size(); ++i ) {
                    for( size_t j = i + 1; j < az_tree.size(); ++j ) {
                        if( starts_with( az_tree[ i ].first, az_tree[ j ].first ) ) {
                            az_tree[ j ].second->total -= az_tree[ i ].second->total;
                        }
                    }
                }
            }

            // calculate total accumulated time
            double total = 0;
            for( auto &it : copy.counters ) {
                total += it.second.total;
            }

            std::vector<std::string> list;

            // string2tree {
            static unsigned char pos = 0;
            info dummy; 
            dummy.title = "/"; 
#ifdef _MSC_VER
            dummy.pid = _getpid();
#else
            dummy.pid = getpid();
#endif
            dummy.tid = std::this_thread::get_id();
            Node<info> root( std::string() + "\\|/-"[(++pos)%4], &dummy );
            for( auto it = copy.counters.begin(), end = copy.counters.end(); it != end; ++it ) {
                auto &info = it->second;
                list.push_back( info.title );

                auto split = tokenize( info.title, ";" );

                auto &node = root.tree_recreate_branch( split );
                node.value = &info;
            }
            std::stringstream ss;
            root.tree_printer( ss );
            list = tokenize( ss.str(), "\r\n" );
            static size_t maxlen = 0;
            for( auto &it : list ) {
                maxlen = (std::max)(maxlen, it.size());
            }
            for( auto &it : list ) {
                /**/ if( maxlen > it.size() ) it += std::string( maxlen - it.size(), ' ' );
                else if( maxlen < it.size() ) it.resize( maxlen );
            }
            // }

            // prettify name/titles
            size_t i = 0;
            if( for_chrome ) {
                for( auto &cp : copy.counters ) {
                    cp.second.title = tokenize( cp.second.title, ";" ).back();
                    for( auto &ch : cp.second.title ) {
                        if( ch == '\\' ) ch = '/';
                    }
                }
            } else {
                size_t x = 0;
                for( auto &cp : copy.counters ) {
                    cp.second.title = list[++x];
                    for( auto &ch : cp.second.title ) {
                        if( ch == '\\' ) ch = '/';
                    }
                }               
            }

            if( !for_chrome ) {
                std::string format, sep, graph, buffer(1024, '\0');
                // pre-loop
                for( auto &it : std::vector<std::string>{ "%4d.","%s","[%s]","%5.2f%% CPU","(%9.3fms)","%5d hits",feed } ) {
                    format += sep + it;
                    sep = tab;
                }
                // loop
                for( auto &it : copy.counters ) {
                    auto &info = it.second;
                    double cpu = info.total * 100.0 / total;
                    int width(cpu*DOLLAR_CPUMETER_WIDTH/100);
                    graph = std::string( width, '=' ) + std::string( DOLLAR_CPUMETER_WIDTH - width, '.' );
#ifdef _MSC_VER
                    sprintf_s( &buffer[0], 1024,
#else
                    sprintf( &buffer[0], 
#endif
                    format.c_str(), ++i, it.second.title.c_str(), graph.c_str(), cpu, (float)(info.total * 1000), info.hits );
                    out << &buffer[0];
                }
            } else {

                // setup
                out << "[" << std::endl;

                // json array format
                // [ref] https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU/preview
                // [ref] https://github.com/catapult-project/catapult/blob/master/tracing/tracing/base/color_scheme.html#L54

                auto get_color = []( float pct ) {
                    return pct <= 16 ? "good":
                           pct <= 33 ? "bad":
                           "terrible";
                };

                double timestamp = 0;
                root.tree_walker(
                    [&]( const Node<info> &node ) { 
                        auto &info = *node.value;
                        double cpu = info.total * 100.0 / total;
                        out << "{\"name\": \"" << info.title << "\"," 
                                "\"cat\": \"" << "CPU,DOLLAR" << "\","
                                "\"ph\": \"" << 'X' << "\","
                                "\"pid\": " << info.pid << ","
                                "\"tid\": " << info.tid << ","
                                "\"ts\": " << (unsigned int)(timestamp * 1000 * 1000) << ","
                                "\"dur\": " << (unsigned int)(info.total * 1000 * 1000) << ","
                                "\"cname\": \"" << get_color(cpu) << "\"" "," <<
                                "\"args\": {}},\n";
                        timestamp += info.total;
                    },
                    [&]( const Node<info> &node ) { 
                        auto &info = *node.value;
                        double cpu = info.total * 100.0 / total;
                        out << "{\"name\": \"" << info.title << "\"," 
                                "\"cat\": \"" << "CPU,DOLLAR" << "\","
                                "\"ph\": \"" << 'B' << "\","
                                "\"pid\": " << info.pid << ","
                                "\"tid\": " << info.tid << ","
                                "\"ts\": " << (unsigned int)(timestamp * 1000 * 1000) << ","
                                "\"args\": {}},\n";
                        timestamp += info.total;
                    },
                    [&]( const Node<info> &node ) { 
                        auto &info = *node.value;
                        double cpu = info.total * 100.0 / total;
                        out << "{\"name\": \"" << info.title << "\"," 
                                "\"cat\": \"" << "CPU,DOLLAR" << "\","
                                "\"ph\": \"" << 'E' << "\","
                                "\"pid\": " << info.pid << ","
                                "\"tid\": " << info.tid << ","
                                "\"ts\": " << (unsigned int)((timestamp + info.total) * 1000 * 1000) << ","
                                "\"cname\": \"" << get_color(cpu) << "\"" "," <<
                                "\"args\": {}},\n";
                        timestamp += info.total;
                    } );
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
            auto num_unfinished_scopes = stack.size();
            *this = profiler();
            stack.resize( num_unfinished_scopes );
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
        singleton<profiler>()->print<0>(os, ",");
    }

    inline void tsv( std::ostream &os ) {
        singleton<profiler>()->print<0>(os, "\t");
    }

    inline void markdown( std::ostream &os ) {
        singleton<profiler>()->print<0>(os, "|");
    }

    inline void text( std::ostream &os ) {
        singleton<profiler>()->print<0>(os, " ");
    }

    inline void chrome( std::ostream &os ) {
        singleton<profiler>()->print<1>(os, "");
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

#endif

#ifdef DOLLAR_BUILD_DEMO
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>

void x( int counter ) { $
    while( counter-- > 0 ) { $
        std::this_thread::sleep_for( std::chrono::microseconds( int(0.00125 * 1000000) ) );
    }
}
void c( int counter ) { $
    while( counter-- > 0 ) { $
        std::this_thread::sleep_for( std::chrono::microseconds( int(0.00125 * 1000000) ) );
    }
}
void y( int counter ) { $
    while( counter-- > 0 ) { $
        std::this_thread::sleep_for( std::chrono::microseconds( int(0.00125 * 1000000) ) );
        if( counter % 2 ) c(counter); else x(counter);
    }
}
void a( int counter ) { $
    while( counter-- > 0 ) { $
        std::this_thread::sleep_for( std::chrono::microseconds( int(0.00125 * 1000000) ) );
        y(counter);
    }
}

int main() { $
    a(10);

    // write tracing results to a json file (for chrome://tracing embedded profiler)
    std::ofstream file("chrome.json");
    dollar::chrome(file);
    
    // display ascii text results
    dollar::text(std::cout);

    // clear next frame
    dollar::clear();
}
#endif
