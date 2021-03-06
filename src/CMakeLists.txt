if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif ()

set(CMAKE_SKIP_BUILD_RPATH            FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH    FALSE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# Set RPATH when installing to a custom (non-system) directory
list(FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES "${CMAKE_INSTALL_PREFIX}/lib" isSystemDir)
if ("${isSystemDir}" STREQUAL "-1")
    set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
endif ()

include(${CMAKE_SOURCE_DIR}/cmake/EnsureLibraries.cmake)

if (NOT DEFINED LIBCLANG_LIBRARIES)
    set(LIBCLANG_LIBRARIES $ENV{LIBCLANG_LIBRARIES})
endif ()

if (NOT DEFINED LIBCLANG_CXXFLAGS)
    set(LIBCLANG_CXXFLAGS $ENV{LIBCLANG_CXXFLAGS})
endif ()

if (NOT DEFINED LIBCLANG_LIBDIR)
    set(LIBCLANG_LIBDIR $ENV{LIBCLANG_LIBDIR})
endif()

if (NOT DEFINED LIBCLANG_LIBRARIES OR NOT DEFINED LIBCLANG_CXXFLAGS OR NOT DEFINED LIBCLANG_LIBDIR)
    find_package(LibClang REQUIRED)
    set(CMAKE_CXX_FLAGS ${LIBCLANG_CXXFLAGS})
endif ()

find_package(Curses REQUIRED)

if (EXISTS ${CLANG_COMPILATION_INCLUDE})
    add_definitions(-DHAVE_CXCOMPILATIONDATABASE)
endif ()

set(RCT_STATIC 1)
set(RCT_NO_INSTALL 1)
add_subdirectory(rct)

include(${CMAKE_CURRENT_LIST_DIR}/rct/compiler.cmake)
# Set further compile options after including the rct compiler option.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wstrict-aliasing=2 -fno-exceptions -Wcast-qual -fPIC")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wstrict-aliasing=2 -Wcast-qual -fPIC")
if (NOT CYGWIN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fstack-protector-all -Wstack-protector")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector-all -Wstack-protector")
endif ()

if (CMAKE_BUILD_TYPE MATCHES "Debug")
    add_definitions("-DRTAGS_DEBUG")
    set(RCT_EVENTLOOP_CALLBACK_TIME_THRESHOLD 2000)
endif ()

if("${LIBCLANG_LIBDIR}" STREQUAL "")
    message(FATAL_ERROR "Define LIBCLANG_LIBDIR is empty, please provide the path to the clang library. See README for more help.")
endif()
add_definitions("-DCLANG_LIBDIR=${LIBCLANG_LIBDIR}")
add_definitions("-DOS_${CMAKE_SYSTEM_NAME}")

if (CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
    add_definitions(-D__LONG_LONG_SUPPORTED)
endif ()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
add_definitions("-DRTAGS_BIN=\"${PROJECT_BINARY_DIR}/bin/\"")

set(RTAGS_SOURCES
    IndexMessage.cpp
    Location.cpp
    Symbol.cpp
    QueryMessage.cpp
    RTags.cpp)

add_library(rtags STATIC ${RTAGS_SOURCES})

include_directories(${CMAKE_CURRENT_LIST_DIR}
    rct/include/
    rct
    ${CMAKE_CURRENT_BINARY_DIR}/rct/include/rct)

if (CMAKE_SYSTEM_NAME MATCHES "Darwin")
    set(START_GROUP "")
    set(END_GROUP "")
else ()
    set(START_GROUP "-Wl,--start-group")
    set(END_GROUP "-Wl,--end-group")
endif ()

add_executable(rc rc.cpp RClient.cpp)
target_link_libraries(rc rtags rct)

set(RTAGS_CLANG_LIBRARIES rtags rct ${START_GROUP} ${LIBCLANG_LIBRARIES} ${END_GROUP} -lstdc++ ${CURSES_LIBRARIES})
set(RTAGS_CLANG_SOURCES RTagsClang.cpp Source.cpp)

add_executable(rdm
    rdm.cpp
    ClassHierarchyJob.cpp
    CompilerManager.cpp
    CompletionThread.cpp
    SymbolInfoJob.cpp
    DependenciesJob.cpp
    DumpThread.cpp
    FileManager.cpp
    FindFileJob.cpp
    FindSymbolsJob.cpp
    FollowLocationJob.cpp
    IncludeFileJob.cpp
    IndexerJob.cpp
    JobScheduler.cpp
    ListSymbolsJob.cpp
    Preprocessor.cpp
    Project.cpp
    QueryJob.cpp
    ReferencesJob.cpp
    ScanThread.cpp
    Server.cpp
    StatusJob.cpp
    Symbol.cpp
    ${RTAGS_CLANG_SOURCES})

target_link_libraries(rdm ${RTAGS_CLANG_LIBRARIES})

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

add_executable(rp rp.cpp ClangIndexer.cpp ${RTAGS_CLANG_SOURCES})
target_link_libraries(rp ${RTAGS_CLANG_LIBRARIES})

if (CYGWIN)
    EnsureLibraries(rdm rct)
endif ()

if (NOT "${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
    file (GLOB binFiles "${PROJECT_SOURCE_DIR}/bin/*")
    file (MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
    message("Copying files from ${PROJECT_SOURCE_DIR}/bin to ${PROJECT_BINARY_DIR}/bin")
    file (COPY ${binFiles} DESTINATION "${PROJECT_BINARY_DIR}/bin")
endif ()

# find emacs and complain if not found
if (EMACS)
    set(EMACS_EXECUTABLE ${EMACS})
else ()
    find_program(EMACS_EXECUTABLE emacs)
    if(NOT EMACS_EXECUTABLE)
        message(SEND_ERROR "Emacs could not be found")
    endif ()
endif ()

if (NOT RTAGS_ELISP_INSTALL_LOCATION)
    set(RTAGS_ELISP_INSTALL_LOCATION ${CMAKE_INSTALL_PREFIX}/share/emacs/site-lisp/rtags/)
endif ()
foreach(el rtags.el rtags-ac.el company-rtags.el)
    if (NOT "${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
        install(FILES ${el} DESTINATION ${RTAGS_ELISP_INSTALL_LOCATION})
    endif ()
    if (NOT RTAGS_NO_ELISP_BYTECOMPILE)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${el}c
            COMMAND ${EMACS_EXECUTABLE} -batch -l ${CMAKE_CURRENT_LIST_DIR}/compile-shim.el -l ${CMAKE_CURRENT_LIST_DIR}/rtags.el -f batch-byte-compile
            ${CMAKE_CURRENT_BINARY_DIR}/${el}
            DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${el}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Creating byte-compiled Emacs lisp ${CMAKE_CURRENT_BINARY_DIR}/${el}c")
        add_custom_target(emacs_byte_compile_${el} ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${el}c)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${el}c DESTINATION ${RTAGS_ELISP_INSTALL_LOCATION})
    endif ()
endforeach()

install(CODE "message(\"Installing rtags...\")")
find_package(bash-completion)
if (BASH_COMPLETION_FOUND)
    file (MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/completions")
    execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink rtags rc
        COMMAND ${CMAKE_COMMAND} -E create_symlink rtags rdm
        WORKING_DIRECTORY "${PROJECT_BINARY_DIR}/completions")
    install(FILES rtags-bash-completion.bash DESTINATION ${BASH_COMPLETION_COMPLETIONSDIR}
        PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ
        RENAME rtags)
    install(FILES "${PROJECT_BINARY_DIR}/completions/rc" "${PROJECT_BINARY_DIR}/completions/rdm"
        DESTINATION ${BASH_COMPLETION_COMPLETIONSDIR})
else()
    message(STATUS "Bash completion file will not be installed, as cmake bash-completion package was not found!")
endif()
install(TARGETS rdm rc rp RUNTIME DESTINATION bin COMPONENT rtags)
install(FILES ../bin/gcc-rtags-wrapper.sh DESTINATION bin)
install(FILES ../man/man7/rc.7 ../man/man7/rdm.7 DESTINATION share/man/man7/)
