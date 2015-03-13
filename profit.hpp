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
#include <string>
#include <iostream>
#include <string.h>

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
    protected:

        struct local {
            int lastOpenedSample = -1;
            int openSampleCount = 0;
            float rootBegin = 0.0f;
            float rootEnd = 0.0f;
            bool bProfilerIsRunning = true;

            struct profileSample
            {
                bool bIsValid = false;          // whether or not this sample is valid (for use with fixed-size arrays)
                bool bIsOpen;                   // is this sample currently being profiled?
                unsigned int hits;              // number of times this sample has been profiled this frame
                std::string name;               // name of the sample

                float startTime;                // starting time on the clock, in seconds
                float totalTicks;               // total time recorded across all profiles of this sample
                float childTime;                // total time taken by children of this sample

                int parentCount;                // number of parents this sample has (useful for indenting)

                float averagePc = -1;           // average percentage of game loop time taken up
                float minPc = -1;               // minimum percentage of game loop time taken up
                float maxPc = -1;               // maximum percentage of game loop time taken up
                unsigned long dataCount = 0;    // number of percentage values that have been stored

                void reset() {
                    bool b = bIsOpen;
                    *this = profileSample();
                    bIsOpen = b;
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

        static float now() {
#ifdef PROFIT_USE_OPENMP
            return float( omp_get_wtime() );
#else
            static auto epoch = std::chrono::steady_clock::now();
            return float( std::chrono::duration_cast< std::chrono::milliseconds >( std::chrono::steady_clock::now() - epoch ).count() / 1000.0 );
#endif
        }

  public:

    profit( const std::string &sampleName ) {
        auto &g = get();

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
                    g.samples[i].startTime = now();
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

        g.samples[storeIndex].startTime = now();
        g.samples[storeIndex].totalTicks = 0.0f;
        g.samples[storeIndex].childTime = 0.0f;

        g.samples[i].parentCount = g.openSampleCount++;

        if( iParentIndex < 0 ) {
            g.rootBegin = g.samples[storeIndex].startTime;
        }
    }

    ~profit() {
        auto &g = get();
        if( g.bProfilerIsRunning ) {
            float fEndTime = now();

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
        auto &g = get();
        if( g.bProfilerIsRunning ) {
            if (!g.rootEnd)
            g.rootEnd = now();

            char buffer[256];
            auto Sample = [&]( float fMin, float fAvg, float fMax, float tAvg, int hits, const std::string &name, int parentCount ) {
                char *ptr = buffer;

                ptr += sprintf( ptr, "%5.2f | ", fMin );
                ptr += sprintf( ptr, "%5.2f | ", fAvg );
                ptr += sprintf( ptr, "%5.2f | ", fMax );
                ptr += sprintf( ptr, "%5.2f | ", tAvg );
                ptr += sprintf( ptr,   "%3d | ", hits );
                ptr += sprintf( ptr, "%s%s", std::string(parentCount, ' ').c_str(), name.c_str() );

                cout << buffer << std::endl;
            };

            // auto total = ( rootEnd - rootBegin );
            cout << "------+-------+-------+-------+-----+-------------" << std::endl;
            cout << "  min |   avg |   max |     % |   # | scope" << std::endl;
            cout << "------+-------+-------+-------+-----+-------------" << std::endl;

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

            cout << std::endl;
        }
    }

    static void pause( bool paused ) {
        auto &g = get();
        g.bProfilerIsRunning = (paused ? false : true);
    }

    static bool paused() {
        return get().bProfilerIsRunning ? false : true;
    }

    static void reset( const std::string &strName ) {
        auto &g = get();
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
        auto &g = get();
        for( int i = 0; i < PROFIT_MAX_SAMPLES; ++i ) {
            if( g.samples[i].bIsValid ) g.samples[i].reset();
        }
    }

    template<typename ostream>
    inline friend ostream& operator<<( ostream &os, const profit &self ) {
        return self.report( os ), os;
    }
};

#endif
