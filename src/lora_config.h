#pragma once

#ifdef REGION_US
constexpr long kDefaultFrequencyHz = 915000000L;
constexpr long kLockedLoraFrequencyHz = 915000000L;
#else
constexpr long kDefaultFrequencyHz = 433000000L;
constexpr long kLockedLoraFrequencyHz = 433000000L;
#endif
