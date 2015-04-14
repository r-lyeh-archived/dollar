// simple c++11 profiler, header-only, boost licensed. based on code by Steve Rabin and Richard "superpig" Fine.
// - rlyeh

/* usage:

#include "profit.hpp"
int main() {_  // <-- put an underscore after every block scope you want to measure
    for( int x = 0; x < 10000000; ++x ) {_  // <--
        // slow stuff...
    }
    profit::report(std::cout); // report stats to std::cout;
    profit::reset();           // reset current scope
    profit::reset_all();       // reset all scopes (like when entering a new frame)
}

*/

#ifndef _
#pragma once

#include <cassert>
#include <string.h>
#include <algorithm>
#include <iostream>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#ifdef PROFIT_USE_OPENMP
#include <omp.h>
#else
#include <chrono>
#endif

#ifndef PROFIT_MAX_SAMPLES
#define PROFIT_MAX_SAMPLES 50
#endif

#define PROFIT_STRINGIFY(x) #x
#define PROFIT_TOSTRING(x) PROFIT_STRINGIFY(x)

#ifdef _MSC_VER
#define PROFIT(name)  profit _profit(name);
#define _             profit _profit(std::string(__FUNCTION__) + "(" __FILE__ ":" PROFIT_TOSTRING(__LINE__) ")" );
#else
#define PROFIT(name)  profit _profit(name);
#define _             profit _profit(std::string(__PRETTY_FUNCTION__) + "(" __FILE__ ":" PROFIT_TOSTRING(__LINE__) ")" );
#endif

class profit
{
    private:

        class auto_table {
            std::stringstream my_stream;
            typedef std::vector<std::string> row_t;
            typedef std::vector<row_t> rows_t;
            typedef std::map<size_t, size_t> column_widths_t;
            column_widths_t column_widths;
            rows_t rows;
            size_t header_every_nth_row;
            size_t horizontal_padding;

        public:
            auto_table() : header_every_nth_row(0), horizontal_padding(0) {}

        private:
            size_t width(std::string const& s) { return s.length(); }

            size_t combine_width(size_t one_width, size_t another_width) {
                return std::max(one_width, another_width);
            }

        private:
            void print_horizontal_line(std::ostream& stream) {
                stream << '+';
                size_t sum = std::accumulate(column_widths.begin(), column_widths.end(), 0,
                    [] (size_t i, const std::pair<const size_t, size_t>& x) {
                        return i + x.second; 
                    } );
                for (size_t i = 0;
                    i < sum + column_widths.size() +
                    2 * column_widths.size() * horizontal_padding - 1;
                    i++)
                    stream << '-';
                stream << "+\n";
            }

            std::string pad_column_left(std::string const& s, size_t column) {
                size_t s_width = width(s);
                size_t column_width = column_widths[column];
                if (s_width > column_width) return s;

                return std::string(column_width - s_width + horizontal_padding, ' ') + s +
                std::string(horizontal_padding, ' ');
            }

            std::string pad_column_right(std::string const& s, size_t column) {
                size_t s_width = width(s);
                size_t column_width = column_widths[column];
                if (s_width > column_width) return s;

                return std::string(horizontal_padding, ' ') + s +
                std::string(column_width - s_width + horizontal_padding, ' ');
            }

            std::string pad_column(std::string const& s, size_t column, bool right) {
                if (right)
                    return pad_column_right(s, column);
                else
                    return pad_column_left(s, column);
            }

            void print_row(size_t r, std::ostream& stream, int color = 7) {
                size_t column_count = column_widths.size();
                row_t const& row = rows[r];
                size_t header_count = row.size();

                for (size_t i = 0; i < header_count; i++) {
                    stream << '|';
                    stream << pad_column(row[i], i, i == column_count - 1);
                }

                for (size_t i = header_count; i < column_count; i++) {
                    stream << '|';
                    stream << pad_column("", i, i == column_count - 1);
                }

                stream << "|\n";
            }

            void print_header(std::ostream& stream) {
                print_horizontal_line(stream);
                if (rows.size() > 0) {
                    print_row(0, stream, 10);
                    print_horizontal_line(stream);
                }
            }

            void print_rows(std::ostream& stream) {
                size_t row_count = rows.size();

                if (row_count == 0) return;

                for (size_t row = 1; row < row_count - 1; row++) {
                    if (row > 1 && header_every_nth_row &&
                        (row - 1) % header_every_nth_row == 0)
                        print_header(stream);

                    print_row(row, stream, 15);
                }

                if (rows[row_count - 1].size() > 0) print_row(row_count - 1, stream, 15);
            }

            void print_footer(std::ostream& stream) {
                print_horizontal_line(stream);
            }

        public:
            auto_table& add_column(std::string const& name, size_t min_width = 0) {
                size_t new_width = combine_width(width(name), min_width);
                column_widths[column_widths.size()] = new_width;

                if (rows.size() < 1) rows.push_back(row_t());

                rows.front().push_back(name);
                return *this;
            }

            auto_table& with_header_every_nth_row(size_t n) {
                header_every_nth_row = n;
                return *this;
            }

            auto_table& with_horizontal_padding(size_t n) {
                horizontal_padding = n;
                return *this;
            }

            template <typename TPar>
            auto_table& operator<<(TPar const& input) {
                assert( column_widths.size() > 0 && "no columns defined!" );

                if (rows.size() < 1) rows.push_back(row_t());

                if (rows.back().size() >= column_widths.size()) rows.push_back(row_t());

                my_stream << input;
                std::string entry(my_stream.str());
                size_t column = rows.back().size();
                size_t new_width = combine_width(width(entry), column_widths[column]);
                column_widths[column] = new_width;

                rows.back().push_back(entry);

                my_stream.str("");

                return *this;
            }

            auto_table& operator<<(std::ostream &( *endl )(std::ostream &)) {
                if( *endl == static_cast<std::ostream &( * )(std::ostream&)>( std::endl ) ) {
                    rows.push_back(row_t());
                }
                return *this;
            }

            void print(std::ostream& stream) {
                this->print_header(stream);
                this->print_rows(stream);
                this->print_footer(stream);
            }

            std::stringstream& get_stream() { return my_stream; }
        };

    protected:

        struct local {
            int lastOpenedSample = -1;
            int openSampleCount = 0;
            float rootBegin = 0.0f;
            float rootEnd = 0.0f;
            bool bProfilerIsRunning = true;

            struct sample
            {
                bool bIsValid = false;          // whether or not this sample is valid (for use with fixed-size arrays)
                bool bIsOpen = 0;               // is this sample currently being profiled?
                unsigned int hits = 0;          // number of times this sample has been profiled this frame
                std::string name;               // name of the sample

                float startTime = 0;            // starting time on the clock, in seconds
                float totalTicks = 0;           // total time recorded across all profiles of this sample
                float childTime = 0;            // total time taken by children of this sample

                int parentCount = 0;            // number of parents this sample has (useful for indenting)

                float averagePc = -1;           // average percentage of game loop time taken up
                float minPc = -1;               // minimum percentage of game loop time taken up
                float maxPc = -1;               // maximum percentage of game loop time taken up
                unsigned long dataCount = 0;    // number of percentage values that have been stored

                void reset() {
                    bool b = bIsOpen;
                    *this = sample();
                    bIsOpen = b;
                }

                static float now() {
#ifdef PROFIT_USE_OPENMP
                    return float( omp_get_wtime() );
#else
                    static auto epoch = std::chrono::steady_clock::now();
                    return float( std::chrono::duration_cast< std::chrono::milliseconds >( std::chrono::steady_clock::now() - epoch ).count() / 1000.0 );
#endif
                }

            } samples[ PROFIT_MAX_SAMPLES ];
        };

        static local &get() {
            static local master;
            return master;
        }

        //index into the array of samples
        int iSampleIndex;
        int iParentIndex;

  public:

    profit( const std::string &sampleName ) {
        local &g = get();

        if( !g.bProfilerIsRunning ) {
            return;
        }

        //find the sample
        int i = 0;
        int storeIndex = -1;
        for( i = 0; i < PROFIT_MAX_SAMPLES; ++i ) {
            if ( !g.samples[i].bIsValid ) {
                if( storeIndex < 0 ) {
                    storeIndex = i;
                }
            } else {
                if( g.samples[i].name == sampleName ) {
                    //this is the sample we want
                    //check that it's not already open
                    assert( !g.samples[i].bIsOpen && "Tried to profile a sample which was already being profiled" );
                    //first, store it's index
                    iSampleIndex = i;
                    //the parent sample is the last opened sample
                    iParentIndex = g.lastOpenedSample;
                    g.lastOpenedSample = i;
                    g.samples[i].parentCount = g.openSampleCount++;
                    g.samples[i].bIsOpen = true;
                    ++g.samples[i].hits;
                    g.samples[i].startTime = local::sample::now();
                    //if this has no parent, it must be the 'main loop' sample, so do the global timer
                    if( iParentIndex < 0 ) {
                        g.rootBegin = g.samples[i].startTime;
                    }
                    return;
                }
            }
        }

        //we've not found it, so it must be a new sample
        //use the storeIndex value to store the new sample
        assert( storeIndex >= 0 && "Profiler has run out of sample slots!" );
        iSampleIndex = storeIndex;
        iParentIndex = g.lastOpenedSample;
        g.lastOpenedSample = storeIndex;

        g.samples[storeIndex].bIsValid = true;
        g.samples[storeIndex].bIsOpen = true;
        g.samples[storeIndex].hits = 1;
        g.samples[storeIndex].name = sampleName;

        g.samples[storeIndex].startTime = local::sample::now();
        g.samples[storeIndex].totalTicks = 0.0f;
        g.samples[storeIndex].childTime = 0.0f;

        g.samples[i].parentCount = g.openSampleCount++;

        if( iParentIndex < 0 ) {
            g.rootBegin = g.samples[storeIndex].startTime;
        }
    }

    ~profit() {
        local &g = get();
        if( g.bProfilerIsRunning ) {
            float fEndTime = local::sample::now();

            //phew... ok, we're done timing
            g.samples[iSampleIndex].bIsOpen = false;
            //calculate the time taken this profile, for ease of use later on
            float fTimeTaken = fEndTime - g.samples[iSampleIndex].startTime;

            if( iParentIndex >= 0 ) {
                g.samples[iParentIndex].childTime += fTimeTaken;
            } else {
                //no parent, so this is the end of the main loop sample
                g.rootEnd = fEndTime;
            }
            g.samples[iSampleIndex].totalTicks += fTimeTaken;
            g.lastOpenedSample = iParentIndex;
            --g.openSampleCount;
        }
    }

    static void report( std::ostream &cout ) {
        local &g = get();
        if( g.bProfilerIsRunning ) {
            if (!g.rootEnd)
            g.rootEnd = local::sample::now();

            auto_table printer;
            printer
                .add_column("min")
                .add_column("avg")
                .add_column("max")
                .add_column("%")
                .add_column("#")
                .add_column("scope")
                .with_horizontal_padding(1)
            ;

            char buffer[256];

            auto Sample = [&]( float fMin, float fAvg, float fMax, float tAvg, int hits, const std::string &name, int parentCount ) {
                //todo: use <iomanip>
                sprintf( buffer, "%5.2f", fMin );
                printer << buffer;
                sprintf( buffer, "%5.2f", fAvg );
                printer << buffer;
                sprintf( buffer, "%5.2f", fMax );
                printer << buffer;
                sprintf( buffer, "%5.2f", tAvg );
                printer << buffer;
                sprintf( buffer,   "%3d", hits );
                printer << buffer;
                //sprintf( buffer, "%s%s", ... );
                printer << ( std::string(parentCount, ' ') + name );
            };

            // auto total = ( rootEnd - rootBegin );

            for( int i = 0; i < PROFIT_MAX_SAMPLES; ++i ) {
                if( g.samples[i].bIsValid ) {
                    float sampleTime, percentage;
                    //calculate the time spend on the sample itself (excluding children)
                    sampleTime = g.samples[i].totalTicks - g.samples[i].childTime;
                    percentage = ( sampleTime / ( g.rootEnd - g.rootBegin ) ) * 100.0f;

                    //add it to the sample's values
                    float totalPc;
                    totalPc = g.samples[i].averagePc * g.samples[i].dataCount;
                    totalPc += percentage; g.samples[i].dataCount++;
                    g.samples[i].averagePc = totalPc / g.samples[i].dataCount;
                    if( g.samples[i].minPc < 0 || percentage < g.samples[i].minPc ) {
                        g.samples[i].minPc = percentage;
                    }
                    if( g.samples[i].maxPc < 0 || percentage > g.samples[i].maxPc ) {
                        g.samples[i].maxPc = percentage;
                    }

                    //output these values
                    Sample( g.samples[i].minPc, g.samples[i].averagePc, g.samples[i].maxPc, percentage, g.samples[i].hits, g.samples[i].name, g.samples[i].parentCount );

                    //reset the sample for next time
                    g.samples[i].hits = 0;
                    g.samples[i].totalTicks = 0;
                    g.samples[i].childTime = 0;
                }
            }

            printer.print(std::cout);
        }
    }

    static void pause( bool paused ) {
        local &g = get();
        g.bProfilerIsRunning = (paused ? false : true);
    }

    static bool paused() {
        return get().bProfilerIsRunning ? false : true;
    }

    static void reset( const std::string &strName ) {
        local &g = get();
        for( int i = 0; i < PROFIT_MAX_SAMPLES; ++i ) {
            if( g.samples[i].bIsValid && g.samples[i].name == strName ) {
                //found it
                //reset avg/min/max ONLY
                //because the sample may be running
                g.samples[i].maxPc = g.samples[i].minPc = -1;
                g.samples[i].averagePc = -1;
                g.samples[i].dataCount = 0;
                return;
            }
        }
    }

    static void reset_all() {
        local &g = get();
        for( int i = 0; i < PROFIT_MAX_SAMPLES; ++i ) {
            if( g.samples[i].bIsValid ) g.samples[i].reset();
        }
    }

    std::ostream& operator<<( std::ostream &os ) {
        return report( os ), os;
    }
};

#endif
