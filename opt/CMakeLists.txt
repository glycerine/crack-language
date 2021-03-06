# optional extensions

set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib/crack/ext)

SET(GTK2_ADDITIONAL_SUFFIXES x86_64-linux-gnu/glib-2.0 gdk-pixbuf-2.0)
macro_optional_find_package(GTK2 2.6 COMPONENTS gtk)
macro_log_feature(GTK2_FOUND "gtk2+" "Bindings for gtk2+" "http://www.gtk.org/")

if (GTK2_FOUND)
    include_directories(${GTK2_INCLUDE_DIRS})
    add_library(gtk SHARED _gtk.cc)
    set_target_properties(gtk
                      PROPERTIES
                      OUTPUT_NAME _gtk
                      PREFIX ""
                      )
    target_link_libraries( gtk
                          ${GTK2_LIBRARIES}
                         )
    install(TARGETS gtk DESTINATION "lib/crack-${CRACK_VERSION}/crack/ext")
endif(GTK2_FOUND)

macro_optional_find_package(PCRE)
macro_log_feature(PCRE_FOUND "PCRE" "Bindings for PCRE" "http://www.pcre.org/")

if (PCRE_FOUND)
    include_directories(${PCRE_INCLUDE_DIR})
    add_library(pcre SHARED _pcre.cc)
    set_target_properties(pcre
                      PROPERTIES
                      OUTPUT_NAME _pcre
                      PREFIX ""
                      LINK_FLAGS ${LLVM_LDFLAGS}
                      )
    target_link_libraries(pcre
                          libcrack ${PCRE_LIBRARIES}
                         )
    install(TARGETS pcre DESTINATION "lib/crack-${CRACK_VERSION}/crack/ext")
endif(PCRE_FOUND)

macro_optional_find_package(SDL)
macro_log_feature(SDL_FOUND "SDL" "Bindings for SDL" "http://www.libsdl.org/")

if (SDL_FOUND)
    include_directories(${SDL_INCLUDE_DIR})
    add_library(sdl SHARED _sdl.cc)
    set_target_properties(sdl
                        PROPERTIES
                        OUTPUT_NAME _sdl
                        PREFIX ""
                        )
    target_link_libraries( sdl
                            ${SDL_LIBRARY}
                        )
    install(TARGETS sdl DESTINATION "lib/crack-${CRACK_VERSION}/crack/ext")
endif (SDL_FOUND)

macro_optional_find_package(OpenGL)
macro_log_feature(OPENGL_FOUND "OpenGL" "Bindings for OpenGL" "http://www.opengl.org/")

if (OPENGL_FOUND)
    include_directories(${OPENGL_INCLUDE_DIR})
    add_library(gl SHARED _gl.cc)
    set_target_properties(gl
                        PROPERTIES
                        OUTPUT_NAME _gl
                        PREFIX ""
                        )
    target_link_libraries( gl
                            ${OPENGL_LIBRARIES}
                        )
    install(TARGETS gl DESTINATION "lib/crack-${CRACK_VERSION}/crack/ext")
endif (OPENGL_FOUND)

macro_optional_find_package(XS)
macro_log_feature(XS_FOUND "xs" "Bindings for Crossroads I/O" "http://crossroads.io")
if (XS_FOUND)
    add_library(xs SHARED _xs.cc)
    set_target_properties(xs
                        PROPERTIES
                        OUTPUT_NAME _xs
                        PREFIX ""
                        )
    target_link_libraries( xs
                          -lxs
                        )
    install(TARGETS xs DESTINATION "lib/crack-${CRACK_VERSION}/crack/ext")
endif (XS_FOUND)

find_package(PkgConfig)

if (PKG_CONFIG_FOUND)
    # TODO: write a *proper* FindCairo.cmake file or copy one over from another project
    option(WITH_Cairo "Search for cairo package" ON)

    if (WITH_Cairo)
        pkg_search_module(CAIRO cairo>=1.10)
    else (WITH_Cairo)
        set(CAIRO_FOUND FALSE)
    endif (WITH_Cairo)

    macro_log_feature(CAIRO_FOUND "Cairo" "Bindings for Cairo" "http://www.cairographics.org/")

    if (CAIRO_FOUND)
        add_library(cairo SHARED _cairo.cc)
        set_target_properties(cairo
                            PROPERTIES
                            OUTPUT_NAME _cairo
                            PREFIX ""
                            )
        target_link_libraries( cairo
                                ${CAIRO_LDFLAGS}
                            )
        install(TARGETS cairo DESTINATION "lib/crack-${CRACK_VERSION}/crack/ext")
        macro_log_feature(SDL_FOUND "CairoSDL" "Bindings for Cairo/SDL" "http://www.cairographics.org/")
        if (SDL_FOUND)
            add_library(cairosdl SHARED _cairosdl.cc cairosdl.c)
            set_target_properties(cairosdl
                                PROPERTIES
                                OUTPUT_NAME _cairosdl
                                PREFIX ""
                                )
            target_link_libraries( cairosdl
                                    ${CAIRO_LDFLAGS}
                                    ${SDL_LIBRARY}
                                )
            install(TARGETS cairosdl DESTINATION "lib/crack-${CRACK_VERSION}/crack/ext")
        endif (SDL_FOUND)

    endif (CAIRO_FOUND)
endif (PKG_CONFIG_FOUND)
