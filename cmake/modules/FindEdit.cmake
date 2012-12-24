# - Try to find libedit
# Once done this will define
#
#  LIBEDIT_FOUND - system has the editline library
#  LIBEDIT_INCLUDE_DIR - the editline include directory
#  LIBEDIT_LIBRARIES - The libraries needed to use editline

# Copyright (c) 2006, Alexander Neundorf, <neundorf@kde.org>
# Copyright (c) 2012, Conrad Steenberg, <conrad.steenberg@gmail.com>
#
# Redistribution and use is allowed according to the terms of the BSD license.
# For details see the accompanying COPYING-CMAKE-SCRIPTS file.


if (LIBEDIT_INCLUDE_DIR AND LIBEDIT_LIBRARY)
  # Already in cache, be silent
  set(LIBEDIT_FIND_QUIETLY TRUE)
endif (LIBEDIT_INCLUDE_DIR AND LIBEDIT_LIBRARY)


if (NOT WIN32)
  # use pkg-config to get the directories and then use these values
  # in the FIND_PATH() and FIND_LIBRARY() calls
  find_package(PkgConfig)

  pkg_search_module(LIBEDIT libedit>=2)

endif (NOT WIN32)

if (LIBEDIT_FOUND)
    find_path(LIBEDIT_INCLUDE_DIR history.h readline.h
              HINTS ${PC_LIBEDIT_INCLUDEDIR} ${PC_LIBEDIT_INCLUDE_DIRS} 
              PATH_SUFFIXES editline)

    find_library(LIBEDIT_LIBRARY NAMES edit HINTS ${PC_LIBEDIT_LIBDIR} ${PC_LIBEDIT_LIBRARY_DIRS})

    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(editline DEFAULT_MSG LIBEDIT_INCLUDE_DIR LIBEDIT_LIBRARY)

    set(LIBEDIT_LIBRARIES ${LIBEDIT_LIBRARY} )

    mark_as_advanced(LIBEDIT_INCLUDE_DIR LIBEDIT_LIBRARIES LIBEDIT_LIBRARY)
endif (LIBEDIT_FOUND)
