cmake_minimum_required ( VERSION 2.8.12 )

FOREACH ( policy CMP0054 CMP0057 CMP0042 CMP0074 CMP0110 CMP0048 )
	IF ( POLICY ${policy} )
		CMAKE_POLICY ( SET ${policy} NEW )
	ENDIF ()
ENDFOREACH ()

set (VERSION_STR "1.0.0" )
PROJECT ( lemmatize_uk VERSION "${VERSION_STR}" )

# Specify build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo")
endif()
SET ( CMAKE_MODULE_PATH "${lemmatize_uk_SOURCE_DIR}/cmake" )

find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

if(Python3_VERSION VERSION_LESS 3.9)
    message(FATAL_ERROR "Python 3.9 at least required, found ${Python3_VERSION}")
endif()

include_directories(SYSTEM ${Python3_INCLUDE_DIRS})

# main bunch of sources
set ( lemmatize_uk_SRC lemmatize_uk.c )

add_library(lemmatize_uk SHARED ${lemmatize_uk_SRC})
target_link_libraries(lemmatize_uk ${Python3_LIBRARIES})
set_target_properties(lemmatize_uk PROPERTIES OUTPUT_NAME "lemmatize_uk")
set_target_properties(lemmatize_uk PROPERTIES PREFIX  "")
set(CPACK_RPM_PACKAGE_AUTOREQ 0)

file(WRITE ${PROJECT_BINARY_DIR}/${CMAKE_FILES_DIRECTORY}/manticore_uk_lemmatizer.conf "/usr/local/lib\n")
install(FILES ${PROJECT_BINARY_DIR}/${CMAKE_FILES_DIRECTORY}/manticore_uk_lemmatizer.conf DESTINATION /etc/ld.so.conf.d)

# --------------- related with packaging -------------------

set ( PACKAGE_NAME "manticore-lemmatizer-uk" )
set ( COMPANY_NAME "Manticore Software LTD" )
set ( BUILD_TAG "" CACHE STRING "The tag which will be added to build" )
set ( PKGSUFFIXES ) # list of suffixes which will be append to package name, will be '-'-separated and added to package file name along with DISTR_SUFFIX

# simpler packages: provide -DPACK=1, and DISTR_BUILD will be set from env $DISTR, easier in dockers
if (PACK)
        set(DISTR_BUILD "$ENV{DISTR}")
endif ()


# Make release build for the pointed distr
# That will override defaults and give possibility to build
# the distribution with minimal command line
if ( DISTR_BUILD )
        # set default options to be included into build
        set ( DISTR "${DISTR_BUILD}" CACHE STRING "Choose the distr." )
        if ( NOT "${DISTR_BUILD}" STREQUAL "macosbrew" )
                SET ( CMAKE_INSTALL_PREFIX "/usr/var" CACHE FILEPATH "Forced install to /usr/var" FORCE )
        endif ()
        message ( STATUS "DISTR_BUILD applied. Package will be set to ${DISTR_BUILD}" )
endif ()

include(SetBuildType)

configure_file ( "LICENSE" "${CMAKE_CURRENT_BINARY_DIR}/LICENSE" COPYONLY )

set ( CPACK_SOURCE_IGNORE_FILES "/\\\\.idea/" "/\\\\.git/"
                "/\\\\.svn/" "/autom4te\\\\.cache/" "/bin/" "/junk/" "/win/" "/gmock/" "/gtest/" "/libtest"   "/pkgconfig/"
                ".swp$" ".orig$" ".log$" ".deps/" )
set ( CPACK_SOURCE_GENERATOR TGZ )
set ( CPACK_PACKAGE_NAME "${PACKAGE_NAME}" )
set ( CPACK_PACKAGE_CONTACT "Manticore Team <build@manticoresearch.com>" )
set ( CMAKE_PROJECT_HOMEPAGE_URL "https://manticoresearch.com/" )
set ( CPACK_PACKAGE_VENDOR "${COMPANY_NAME}" )
set ( CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_BINARY_DIR}/LICENSE" )
set ( CPACK_PACKAGE_DESCRIPTION_SUMMARY "Manticore Ukrainian lemmatizer plugin")
set ( CPACK_PACKAGE_DESCRIPTION "Lemmatizer for Ukrainian language for Manticore Search based on Pymorphy2")
set ( CPACK_RPM_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION}" ) # the description will default to the default one (This is an installer created using CPack..." otherwise, i.e. it doesn't take CPACK_PACKAGE_DESCRIPTION as a default
set ( CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_BINARY_DIR}/config/CPackOptions.cmake" )

include ( SetBuildType )

SET ( CPACK_PACKAGE_VERSION "${VERSION_STR}" )
SET ( FULL_VERSION "${CPACK_PACKAGE_VERSION}" )
string ( TOLOWER "${CPACK_PACKAGE_NAME}" CPACK_PACKAGE_NAME_LOWERCASE )

if ( DISTR_SUFFIX )
        SET ( FULL_VERSION "${FULL_VERSION}${DISTR_SUFFIX}" )
endif ()

# delimiter between name and version
set ( _d "-" )
if ( CPACK_GENERATOR )
        if ( ${CPACK_GENERATOR} STREQUAL "DEB" )
                UNSET ( _d )
                SET ( _d "_" )
        endif()
endif()

SET ( CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME_LOWERCASE}${_d}${FULL_VERSION}" )

# deb specific
set(CPACK_DEBIAN_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}.deb")

# rpm specific (used inside .spec template)
SET ( CPACK_RPM_PACKAGE_VERSION "${VERSION_STR}" )

include ( CPack )
cpack_add_component( lemmatize_uk )

if ( WIN32 )
        IF ( CMAKE_EXE_LINKER_FLAGS MATCHES "x64" )
                LIST ( APPEND PKGSUFFIXES "x64" )
        else ()
                LIST ( APPEND PKGSUFFIXES "win32" )
        endif ()
endif ()

set ( CPACK_SUFFIX "" )
FOREACH ( SUFFIX ${PKGSUFFIXES} )
        set ( CPACK_SUFFIX "${CPACK_SUFFIX}-${SUFFIX}" )
endforeach ()

