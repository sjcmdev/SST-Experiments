#include "app.hpp"

#ifndef NDEBUG
#include "tests/test_stage1.hpp"
#include "tests/test_stage2.hpp"
#endif

void appInit(AppState& appState)
{
    (void)appState;

#ifndef NDEBUG
    runStage1Tests();
    runStage2Tests();
#endif
}
