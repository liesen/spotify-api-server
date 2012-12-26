# The name of our project is "HELLO". CMakeLists files in this project can 
# refer to the root source directory of the project as ${HELLO_SOURCE_DIR} and 
# to the root binary directory of the project as ${HELLO_BINARY_DIR}. 
CMAKE_MINIMUM_REQUIRED (VERSION 2.6) 
PROJECT (spotify-api-server) 

SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR}/cmake)
FIND_PACKAGE( SubversionLibrary REQUIRED )

FIND_PACKAGE(PkgConfig)
PKG_CHECK_MODULES(SPOTIFY REQUIRED libspotify)
PKG_CHECK_MODULES(JANSSON REQUIRED jansson)
PKG_CHECK_MODULES(EVENT REQUIRED libevent_pthreads)

SET(CMAKE_C_FLAGS "-std=c99 -Wall")

# Recurse into the "Hello" and "Demo" subdirectories. This does not actually 
# cause another cmake executable to run. The same process will walk through 
# the project's entire directory structure. 

# Make sure the compiler can find include files from our Hello library. 
INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}
  ${SUBVERSION_INCLUDE_DIRS}
  ${SPOTIFY_INCLUDE_DIRS}
  ${JANSSON_INCLUDE_DIRS}
  ${EVENT_INCLUDE_DIRS}
)

# Make sure the linker can find the Hello library once it is built. 
LINK_DIRECTORIES (
  ${SPOTIFY_LIBRARY_DIRS}
  ${JANSSON_LIBRARY_DIRS}
  ${EVENT_LIBRARY_DIRS}
)

# Add executable called "helloDemo" that is built from the source files 
# "demo.cxx" and "demo_b.cxx". The extensions are automatically found. 
ADD_EXECUTABLE (server
  constants.h
  diff.c
  diff.h
  json.c
  json.h
  main.c
  server.c
  server.h
)

# Link the executable to the Hello library. 
TARGET_LINK_LIBRARIES(server
  ${SPOTIFY_LIBRARIES}
  ${JANSSON_LIBRARIES}
  ${EVENT_LIBRARIES}
  ${SUBVERSION_LIBRARIES}
)
