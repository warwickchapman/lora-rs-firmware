#pragma once

#ifndef LRS_FW_VERSION
// Fallback only. Normal builds inject VERSION via tools/build_metadata.py.
#define LRS_FW_VERSION "0.0.0-dev"
#endif

#ifndef LRS_GIT_SHA
#define LRS_GIT_SHA "nogit"
#endif

#ifndef LRS_GIT_BRANCH
#define LRS_GIT_BRANCH "unknown"
#endif

#ifndef LRS_GIT_DIRTY
#define LRS_GIT_DIRTY 0
#endif

#ifndef LRS_BUILD_ID
#define LRS_BUILD_ID "nobuild"
#endif

#ifndef LRS_BUILD_DATE_SHORT
#define LRS_BUILD_DATE_SHORT "000000"
#endif
