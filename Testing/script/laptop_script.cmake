# This script takes in optional environment variables.
#   MERCURY_BUILD_CONFIGURATION=Debug | Release
#   MERCURY_DASHBOARD_MODEL=Experimental | Nightly | Continuous
#   MERCURY_BUILD_STATIC_LIBRARIES
#   MERCURY_DO_COVERAGE
#   MERCURY_DO_MEMCHECK

# MERCURY_BUILD_CONFIGURATION = Debug | Release
set(MERCURY_BUILD_CONFIGURATION "$ENV{MERCURY_BUILD_CONFIGURATION}")
if(NOT MERCURY_BUILD_CONFIGURATION)
  set(MERCURY_BUILD_CONFIGURATION "Debug")
endif()
string(TOLOWER ${MERCURY_BUILD_CONFIGURATION} lower_mercury_build_configuration)
set(CTEST_BUILD_CONFIGURATION ${MERCURY_BUILD_CONFIGURATION})

# MERCURY_DASHBOARD_MODEL=Experimental | Nightly | Continuous
set(MERCURY_DASHBOARD_MODEL "$ENV{MERCURY_DASHBOARD_MODEL}")
if(NOT MERCURY_DASHBOARD_MODEL)
  set(MERCURY_DASHBOARD_MODEL "Experimental")
endif()
set(dashboard_model ${MERCURY_DASHBOARD_MODEL})
string(TOLOWER ${MERCURY_DASHBOARD_MODEL} lower_mercury_dashboard_model)

# Disable loop when MERCURY_DASHBOARD_MODEL=Continuous
set(MERCURY_NO_LOOP $ENV{MERCURY_NO_LOOP})
if(MERCURY_NO_LOOP)
  message("Disabling looping (if applicable)")
  set(dashboard_disable_loop TRUE)
endif()

# Number of jobs to build
set(CTEST_BUILD_FLAGS "-j4")

# Build shared libraries
set(mercury_build_shared ON)
set(MERCURY_BUILD_STATIC_LIBRARIES $ENV{MERCURY_BUILD_STATIC_LIBRARIES})
if(MERCURY_BUILD_STATIC_LIBRARIES)
  message("Building static libraries")
  set(mercury_build_shared OFF)
endif()

# Build name referenced in cdash
set(CTEST_BUILD_NAME "test-x64-${lower_mercury_dashboard_model}")

set(CTEST_CMAKE_GENERATOR "Unix Makefiles")
# Must point to the root where we can checkout/build/run the tests
set(CTEST_DASHBOARD_ROOT "$ENV{HOME}/workspace/Testing/${MERCURY_DASHBOARD_MODEL}")
# Give a site name
set(CTEST_SITE "$ENV{HOSTNAME}")
set(CTEST_TEST_TIMEOUT 180) # 3 minute timeout

# Optional coverage options
set(MERCURY_DO_COVERAGE $ENV{MERCURY_DO_COVERAGE})
if(MERCURY_DO_COVERAGE)
  message("Enabling Coverage")
  set(CTEST_COVERAGE_COMMAND "/usr/bin/gcov")
  set(CTEST_BUILD_NAME "${CTEST_BUILD_NAME}-coverage")
  # don't run parallel coverage tests, no matter what.
  set(CTEST_TEST_ARGS PARALLEL_LEVEL 1)

  # needed by mercury_common.cmake
  set(dashboard_do_coverage TRUE)

  # add Coverage dir to the root so that we don't mess the non-coverage
  # dashboard.
  set(CTEST_DASHBOARD_ROOT "${CTEST_DASHBOARD_ROOT}/Coverage")
endif()

# Optional memcheck options
set(MERCURY_DO_MEMCHECK $ENV{MERCURY_DO_MEMCHECK})
if(MERCURY_DO_MEMCHECK)
  message("Enabling Memcheck")
  set(CTEST_MEMORYCHECK_COMMAND "/usr/bin/valgrind")
  set(CTEST_MEMORYCHECK_COMMAND_OPTIONS "--gen-suppressions=all --trace-children=yes --fair-sched=yes -q --leak-check=yes --show-reachable=yes --num-callers=50 -v")
  set(CTEST_MEMORYCHECK_SUPPRESSIONS_FILE ${CTEST_SCRIPT_DIRECTORY}/MercuryValgrindSuppressions.supp)

  # needed by mercury_common.cmake
  set(dashboard_do_memcheck TRUE)
endif()

set(dashboard_source_name mercury)
set(dashboard_binary_name mercury-${lower_mercury_build_configuration})
if(NOT mercury_build_shared)
  set(dashboard_binary_name ${dashboard_binary_name}-static)
endif()

# Initial cache used to build mercury, options can be modified here
set(dashboard_cache "
CMAKE_C_FLAGS:STRING=-Wall -Wextra -Wshadow -Winline -Wundef -Wcast-qual -std=gnu99

BUILD_SHARED_LIBS:BOOL=${mercury_build_shared}
BUILD_TESTING:BOOL=ON

MEMORYCHECK_COMMAND:FILEPATH=${CTEST_MEMORYCHECK_COMMAND}
MEMORYCHECK_SUPPRESSIONS_FILE:FILEPATH=${CTEST_MEMORYCHECK_SUPPRESSIONS_FILE}
COVERAGE_COMMAND:FILEPATH=${CTEST_COVERAGE_COMMAND}

MERCURY_ENABLE_COVERAGE:BOOL=${dashboard_do_coverage}
MERCURY_ENABLE_PARALLEL_TESTING:BOOL=ON
MERCURY_USE_BOOST_PP:BOOL=OFF
MERCURY_USE_XDR:BOOL=OFF
NA_USE_BMI:BOOL=ON
BMI_INCLUDE_DIR:PATH=/opt/bmi/default/include
BMI_LIBRARY:FILEPATH=/opt/bmi/default/lib/libbmi.so
NA_USE_MPI:BOOL=ON
MPIEXEC_MAX_NUMPROCS:STRING=2

MERCURY_TEST_INIT_COMMAND:STRING=killall -9 -r hg_test_client;killall -9 -r hg_test_server;
MERCURY_TESTING_CORESIDENT:BOOL=ON
")

#set(dashboard_git_url $ENV{GIT_URL})
#set(dashboard_git_branch $ENV{GIT_BRANCH})
#set(dashboard_git_commit $ENV{GIT_COMMIT})

#set(ENV{CC}  /usr/bin/gcc)
#set(ENV{CXX} /usr/bin/g++)

include(mercury_common.cmake)

#######################################################################
