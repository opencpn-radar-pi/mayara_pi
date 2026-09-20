/******************************************************************************
 * mayara_pi - doctest entry point.
 *
 * The only translation unit that compiles doctest itself; every other test
 * file includes the header without DOCTEST_CONFIG_IMPLEMENT, so the framework
 * is built once instead of per test file.
 *****************************************************************************/
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
