#include "FHSS.h"
#include "logging.h"
#include "options.h"
#include <string.h>

#if defined(RADIO_SX127X)
#include "SX127xDriver.h"

fhss_config_t domains[] = {
    {"AU915",  FREQ_HZ_TO_REG_VAL(915500000), FREQ_HZ_TO_REG_VAL(926900000), 20},
    {"FCC915", FREQ_HZ_TO_REG_VAL(903500000), FREQ_HZ_TO_REG_VAL(926900000), 40},
    {"EU868",  FREQ_HZ_TO_REG_VAL(865275000), FREQ_HZ_TO_REG_VAL(869575000), 13},
    {"IN866",  FREQ_HZ_TO_REG_VAL(865375000), FREQ_HZ_TO_REG_VAL(866950000), 4},
    {"AU433",  FREQ_HZ_TO_REG_VAL(433420000), FREQ_HZ_TO_REG_VAL(434420000), 3},
    {"EU433",  FREQ_HZ_TO_REG_VAL(433100000), FREQ_HZ_TO_REG_VAL(434450000), 3}
};
#elif defined(RADIO_SX128X)
#include "SX1280Driver.h"

fhss_config_t domains[] = {
    {    
    #if defined(Regulatory_Domain_EU_CE_2400)
        "CE_LBT",
    #elif defined(Regulatory_Domain_ISM_2400)
        "ISM2G4",
    #endif
    FREQ_HZ_TO_REG_VAL(2400400000), FREQ_HZ_TO_REG_VAL(2479400000), 80}
};
#endif

// Our table of FHSS frequencies. Define a regulatory domain to select the correct set for your location and radio
fhss_config_t *FHSSconfig;

// Actual sequence of hops as indexes into the frequency list
uint8_t FHSSsequence[256];
// Which entry in the sequence we currently are on
uint8_t volatile FHSSptr;
// Channel for sync packets and initial connection establishment
uint_fast8_t sync_channel;
// Offset from the predefined frequency determined by AFC on Team900 (register units)
int32_t FreqCorrection;
int32_t FreqCorrection_2;

uint32_t freq_spread;

uint8_t freq_start_not_converted;
uint8_t freq_stop_not_converted;
uint8_t freq_count_not_converted;

/**
Requirements:
1. 0 every n hops
2. No two repeated channels
3. Equal occurance of each (or as even as possible) of each channel
4. Pseudorandom

Approach:
  Fill the sequence array with the sync channel every FHSS_FREQ_CNT
  Iterate through the array, and for each block, swap each entry in it with
  another random entry, excluding the sync channel.

*/
void FHSSrandomiseFHSSsequence(const uint32_t seed)
{
    FHSSconfig = &domains[firmwareOptions.domain];
    DBGLN("Setting %s Mode", FHSSconfig->domain);
    DBGLN("Number of FHSS frequencies = %u", FHSSconfig->freq_count);

    sync_channel = (FHSSconfig->freq_count / 2) + 1;
    DBGLN("Sync channel = %u", sync_channel);

    freq_spread = (FHSSconfig->freq_stop - FHSSconfig->freq_start) * FREQ_SPREAD_SCALE / (FHSSconfig->freq_count - 1);

    // reset the pointer (otherwise the tests fail)
    FHSSptr = 0;
    rngSeed(seed);

    // initialize the sequence array
    for (uint16_t i = 0; i < FHSSgetSequenceCount(); i++)
    {
        if (i % FHSSconfig->freq_count == 0) {
            FHSSsequence[i] = sync_channel;
        } else if (i % FHSSconfig->freq_count == sync_channel) {
            FHSSsequence[i] = 0;
        } else {
            FHSSsequence[i] = i % FHSSconfig->freq_count;
        }
    }

    for (uint16_t i=0; i < FHSSgetSequenceCount(); i++)
    {
        // if it's not the sync channel
        if (i % FHSSconfig->freq_count != 0)
        {
            uint8_t offset = (i / FHSSconfig->freq_count) * FHSSconfig->freq_count; // offset to start of current block
            uint8_t rand = rngN(FHSSconfig->freq_count-1)+1; // random number between 1 and FHSS_FREQ_CNT

            // switch this entry and another random entry in the same block
            uint8_t temp = FHSSsequence[i];
            FHSSsequence[i] = FHSSsequence[offset+rand];
            FHSSsequence[offset+rand] = temp;
        }
    }

    // output FHSS sequence
    for (uint16_t i=0; i < FHSSgetSequenceCount(); i++)
    {
        DBG("%u ",FHSSsequence[i]);
        if (i % 10 == 9)
            DBGCR;
    }
    DBGCR;
}


void CustomFHSSrandomiseFHSSsequence(const uint32_t seed, const uint8_t freq_start, const uint8_t freq_stop, const uint8_t freq_count) {
    // Saving not converted values
    freq_start_not_converted = freq_start;
    freq_stop_not_converted = freq_stop;
    freq_count_not_converted = freq_count;

    DBGLN("freq_start_not_converted = %u", freq_start_not_converted);
    DBGLN("freq_stop_not_converted = %u", freq_stop_not_converted);
    DBGLN("freq_count_not_converted = %u", freq_count_not_converted);

    FHSSconfig = &domains[firmwareOptions.domain];

    // Setting our own frequencies and grid
//    FHSSconfig->domain = "Custom domain";
    // Matching selected point to frequency
    switch (freq_start) {
        case 0:
            FHSSconfig->freq_start = FREQ_HZ_TO_REG_VAL(750000000);
            break;
        case 1:
            FHSSconfig->freq_start = FREQ_HZ_TO_REG_VAL(800000000);
            break;
        case 2:
            FHSSconfig->freq_start = FREQ_HZ_TO_REG_VAL(850000000);
            break;
        case 3:
            FHSSconfig->freq_start = FREQ_HZ_TO_REG_VAL(900000000);
            break;
        case 4:
            FHSSconfig->freq_start = FREQ_HZ_TO_REG_VAL(950000000);
            break;
        case 5:
            FHSSconfig->freq_start = FREQ_HZ_TO_REG_VAL(1000000000);
            break;
        default:
            break;
    }
    // Matching selected point to frequency
    switch (freq_stop) {
        case 0:
            FHSSconfig->freq_stop = FREQ_HZ_TO_REG_VAL(750000000);
            break;
        case 1:
            FHSSconfig->freq_stop = FREQ_HZ_TO_REG_VAL(800000000);
            break;
        case 2:
            FHSSconfig->freq_stop = FREQ_HZ_TO_REG_VAL(850000000);
            break;
        case 3:
            FHSSconfig->freq_stop = FREQ_HZ_TO_REG_VAL(900000000);
            break;
        case 4:
            FHSSconfig->freq_stop = FREQ_HZ_TO_REG_VAL(950000000);
            break;
        case 5:
            FHSSconfig->freq_stop = FREQ_HZ_TO_REG_VAL(1000000000);
            break;
        default:
            break;
    }
    // Matching selected point to grid
    switch (freq_count) {
        case 0:
            FHSSconfig->freq_count = 10;
            break;
        case 1:
            FHSSconfig->freq_count = 20;
            break;
        case 2:
            FHSSconfig->freq_count = 30;
            break;
        case 3:
            FHSSconfig->freq_count = 40;
            break;
        default:
            break;
    }

    DBGLN("Setting %s Mode", FHSSconfig->domain);
    DBGLN("freq_start = %u", FHSSconfig->freq_start);
    DBGLN("freq_stop = %u", FHSSconfig->freq_stop);
    DBGLN("Number of FHSS frequencies = %u", FHSSconfig->freq_count);

    // This code as in FHSSrandomiseFHSSsequence
    sync_channel = (FHSSconfig->freq_count / 2) + 1;
    DBGLN("Sync channel = %u", sync_channel);

    freq_spread = (FHSSconfig->freq_stop - FHSSconfig->freq_start) * FREQ_SPREAD_SCALE / (FHSSconfig->freq_count - 1);

    // reset the pointer (otherwise the tests fail)
    FHSSptr = 0;
    rngSeed(seed);

    // initialize the sequence array
    for (uint16_t i = 0; i < FHSSgetSequenceCount(); i++)
    {
        if (i % FHSSconfig->freq_count == 0) {
            FHSSsequence[i] = sync_channel;
        } else if (i % FHSSconfig->freq_count == sync_channel) {
            FHSSsequence[i] = 0;
        } else {
            FHSSsequence[i] = i % FHSSconfig->freq_count;
        }
    }

    for (uint16_t i=0; i < FHSSgetSequenceCount(); i++)
    {
        // if it's not the sync channel
        if (i % FHSSconfig->freq_count != 0)
        {
            uint8_t offset = (i / FHSSconfig->freq_count) * FHSSconfig->freq_count; // offset to start of current block
            uint8_t rand = rngN(FHSSconfig->freq_count-1)+1; // random number between 1 and FHSS_FREQ_CNT

            // switch this entry and another random entry in the same block
            uint8_t temp = FHSSsequence[i];
            FHSSsequence[i] = FHSSsequence[offset+rand];
            FHSSsequence[offset+rand] = temp;
        }
    }

    // output FHSS sequence
    for (uint16_t i=0; i < FHSSgetSequenceCount(); i++)
    {
        DBG("%u ",FHSSsequence[i]);
        if (i % 10 == 9)
            DBGCR;
    }
    DBGCR;
}

bool isDomain868()
{
    return strcmp(FHSSconfig->domain, "EU868") == 0;
}

bool isChanged(const uint8_t freq_start, const uint8_t freq_stop, const uint8_t freq_count)
{
    return freq_start_not_converted != freq_start || freq_stop_not_converted != freq_stop || freq_count_not_converted != freq_count;
}
