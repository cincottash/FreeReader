cmake_minimum_required(VERSION 3.21)

# The app's name lives here (and nowhere else): rename it in one place.
set(APP_NAME "FreeReader")
set(APP_ORG "cincottash")
project(${APP_NAME} VERSION 0.1.0 LANGUAGES CXX)
# Release builds pass the real version (e.g. -DRELEASE_VERSION=1.2.3); local builds show 0.1.0-dev.
set(RELEASE_VERSION "" CACHE STRING "Version shown in the app (set by the release workflow)")
if(RELEASE_VERSION)
    set(APP_VERSION "${RELEASE_VERSION}")
else()
    set(APP_VERSION "${PROJECT_VERSION}-dev")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# STATIC_BUILD=ON (with a static Qt) makes one self-contained exe: Qt and every library it uses are linked in,
# so nothing has to be installed next to it. Development builds leave it OFF for faster linking.
option(STATIC_BUILD "Link everything statically (release exe)" OFF)
if(STATIC_BUILD AND MINGW)
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a)          # prefer libfoo.a over the DLL import library libfoo.dll.a
    set(PKG_CONFIG_USE_STATIC_LIBS ON)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
    # Libraries the static Qt dependencies (OpenSSL's libcrypto, FreeType) need but don't declare. They must come
    # last on the link line, after the libraries that use them.
    set(CMAKE_CXX_STANDARD_LIBRARIES "${CMAKE_CXX_STANDARD_LIBRARIES} -lbz2 -lbrotlidec -lbrotlicommon -lws2_32 -lcrypt32 -liphlpapi -lbcrypt")
endif()

# The touch app (Qt Quick) for phones and tablets; Android builds only that one. On desktops it can be built too,
# for testing (-DBUILD_MOBILE=ON).
if(ANDROID)
    set(BUILD_MOBILE ON)
endif()
option(BUILD_MOBILE "Build the touch app (Qt Quick)" OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Sql Concurrent Network Gui)
if(NOT ANDROID)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Svg)
endif()
# Spoken pronunciation in the translation pop-up, when this Qt has the module.
find_package(Qt6 QUIET COMPONENTS TextToSpeech)
find_package(ZLIB REQUIRED)
# 7-Zip and RAR comics (CB7, CBR) through libarchive, when it is there (desktop builds); without it they get a
# plain message.
if(NOT ANDROID)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBARCHIVE QUIET libarchive)
        # DjVu (scanned books) through DjVuLibre.
        pkg_check_modules(DJVU QUIET ddjvuapi)
        # WebP pictures (manga) where Qt has no WebP plug-in; the static build uses Qt's plug-in instead.
        if(NOT STATIC_BUILD)
            pkg_check_modules(WEBP QUIET libwebp)
        endif()
    endif()
    # CHM help files through chmlib (no pkg-config file).
    find_path(CHMLIB_INCLUDE_DIR chm_lib.h)
    find_library(CHMLIB_LIBRARY NAMES chm)
endif()
# PDF and XPS through MuPDF (AGPL-3.0, https://mupdf.com): its source folder, built with "make libs" into
# MUPDF_BUILD (the workflows show the command). Without it PDF and XPS files get a plain message.
set(MUPDF_DIR "" CACHE PATH "MuPDF source folder, built with 'make libs' (PDF and XPS)")
set(MUPDF_BUILD "build/freereader" CACHE STRING "MuPDF's build folder, inside MUPDF_DIR")
# MuPDF built with the system's FreeType, libjpeg and zlib (USE_SYSTEM_...=yes; the desktops), or with its own
# copies of everything (Android). Its HTML part is left out (html=no), so it needs no HarfBuzz.
if(ANDROID)
    option(MUPDF_SYSTEM_LIBS "MuPDF was built with the system's FreeType, libjpeg and zlib" OFF)
else()
    option(MUPDF_SYSTEM_LIBS "MuPDF was built with the system's FreeType, libjpeg and zlib" ON)
endif()
set(HAVE_MUPDF OFF)
if(MUPDF_DIR AND EXISTS "${MUPDF_DIR}/${MUPDF_BUILD}/libmupdf.a" AND EXISTS "${MUPDF_DIR}/${MUPDF_BUILD}/libmupdf-third.a")
    set(HAVE_MUPDF ON)
    if(MUPDF_SYSTEM_LIBS)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(MUPDF_DEPS REQUIRED freetype2 libjpeg)
    endif()
endif()
if(STATIC_BUILD AND MINGW AND TARGET harfbuzz::harfbuzz)
    # harfbuzz's CMake package always points at its DLL; use the static library instead.
    find_library(HARFBUZZ_STATIC NAMES libharfbuzz.a REQUIRED)
    set_target_properties(harfbuzz::harfbuzz PROPERTIES IMPORTED_LOCATION "${HARFBUZZ_STATIC}" IMPORTED_IMPLIB "${HARFBUZZ_STATIC}")
    # ...and everything the static harfbuzz needs (graphite2, glib, ...), as its pkg-config file lists them.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HB_STATIC REQUIRED harfbuzz)
    foreach(lib IN LISTS HB_STATIC_STATIC_LIBRARIES)
        string(APPEND CMAKE_CXX_STANDARD_LIBRARIES " -l${lib}")
    endforeach()
endif()
qt_standard_project_setup()

# The core: formats, layout engine, positions and services. No widgets and no operating system code, so tests
# and the app share it.
add_library(freereader_core STATIC
    src/core/zip/ZipArchive.cpp
    src/core/html/Dom.cpp
    src/core/html/HtmlParser.cpp
    src/core/css/Css.cpp
    src/core/css/Style.cpp
    src/core/format/Book.cpp
    src/core/format/epub/EpubFormat.cpp
    src/core/format/Convert.cpp
    src/core/format/text/TextFormats.cpp
    src/core/format/html/HtmlFormat.cpp
    src/core/format/fb2/Fb2Format.cpp
    src/core/format/comic/ComicFormat.cpp
    src/core/format/mobi/MobiFormat.cpp
    src/core/format/office/OfficeFormats.cpp
    src/core/format/pages/PageBook.cpp
    src/core/format/pdf/PdfFormat.cpp
    src/core/format/djvu/DjvuFormat.cpp
    src/core/format/chm/ChmFormat.cpp
    src/core/layout/Fonts.cpp
    src/core/layout/Hyphenator.cpp
    src/core/layout/Flow.cpp
    src/core/layout/Layout.cpp
    src/core/layout/PageRenderer.cpp
    src/core/position/Position.cpp
    src/core/services/BookIndex.cpp
    src/core/services/BookEngine.cpp
    src/core/services/Importer.cpp
    src/core/services/Library.cpp
    src/core/services/Secrets.cpp
    src/core/services/WordLog.cpp
    src/core/services/Annotations.cpp
    src/core/services/Highlighting.cpp
    src/core/services/BookSearch.cpp
    src/core/services/LibrarySearch.cpp
    src/core/services/Statistics.cpp
    src/core/services/KindleImport.cpp
    src/core/practice/Scheduler.cpp
    src/core/practice/Answer.cpp
    src/core/practice/Session.cpp
    src/core/translate/Translation.cpp
    src/core/translate/AiProviders.cpp
    src/core/translate/TranslationService.cpp
    src/core/translate/OfflineDictionary.cpp
    src/core/translate/DictionaryCardHtml.cpp
    src/core/translate/DictionaryDownload.cpp
    src/core/translate/OfflineTranslator.cpp
    src/core/translate/SavedWords.cpp
    src/core/translate/LanguagePacks.cpp
)
target_include_directories(freereader_core PUBLIC src)
target_compile_definitions(freereader_core PUBLIC APP_NAME="${APP_NAME}" APP_ORG="${APP_ORG}" APP_VERSION="${APP_VERSION}")
target_link_libraries(freereader_core PUBLIC Qt6::Gui Qt6::Sql Qt6::Concurrent Qt6::Network ZLIB::ZLIB)
if(WIN32)
    target_link_libraries(freereader_core PUBLIC advapi32)   # Credential Manager
endif()
if(LIBARCHIVE_FOUND)
    target_compile_definitions(freereader_core PRIVATE HAVE_LIBARCHIVE)
    target_include_directories(freereader_core PRIVATE ${LIBARCHIVE_INCLUDE_DIRS})
    if(STATIC_BUILD)
        target_compile_definitions(freereader_core PRIVATE LIBARCHIVE_STATIC)
        target_link_directories(freereader_core PUBLIC ${LIBARCHIVE_STATIC_LIBRARY_DIRS})
        target_link_libraries(freereader_core PUBLIC ${LIBARCHIVE_STATIC_LIBRARIES})
    else()
        target_link_directories(freereader_core PUBLIC ${LIBARCHIVE_LIBRARY_DIRS})
        target_link_libraries(freereader_core PUBLIC ${LIBARCHIVE_LIBRARIES})
    endif()
    message(STATUS "libarchive ${LIBARCHIVE_VERSION}: 7-Zip and RAR comics")
endif()
if(WEBP_FOUND)
    target_compile_definitions(freereader_core PRIVATE HAVE_WEBP)
    target_include_directories(freereader_core PRIVATE ${WEBP_INCLUDE_DIRS})
    target_link_directories(freereader_core PUBLIC ${WEBP_LIBRARY_DIRS})
    target_link_libraries(freereader_core PUBLIC ${WEBP_LIBRARIES})
    message(STATUS "libwebp ${WEBP_VERSION}: WebP pictures")
endif()
if(DJVU_FOUND)
    target_compile_definitions(freereader_core PRIVATE HAVE_DJVU)
    target_include_directories(freereader_core PRIVATE ${DJVU_INCLUDE_DIRS})
    if(STATIC_BUILD)
        # Linked in: its functions aren't DLL imports then.
        target_compile_definitions(freereader_core PRIVATE DDJVUAPI= MINILISPAPI=)
        target_link_directories(freereader_core PUBLIC ${DJVU_STATIC_LIBRARY_DIRS})
        target_link_libraries(freereader_core PUBLIC ${DJVU_STATIC_LIBRARIES})
    else()
        target_link_directories(freereader_core PUBLIC ${DJVU_LIBRARY_DIRS})
        target_link_libraries(freereader_core PUBLIC ${DJVU_LIBRARIES})
    endif()
    message(STATUS "DjVuLibre ${DJVU_VERSION}: DjVu books")
endif()
# Without a system chmlib (Android): its source (chm_lib.c, lzx.c; LGPL-2.1), built here.
set(CHMLIB_SOURCE_DIR "" CACHE PATH "chmlib's source folder (chm_lib.c, lzx.c), for builds without a system chmlib")
if(CHMLIB_SOURCE_DIR AND EXISTS "${CHMLIB_SOURCE_DIR}/chm_lib.c")
    enable_language(C)
    add_library(chmlib_local STATIC "${CHMLIB_SOURCE_DIR}/chm_lib.c" "${CHMLIB_SOURCE_DIR}/lzx.c")
    set_target_properties(chmlib_local PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_definitions(chmlib_local PRIVATE CHM_MT CHM_USE_PREAD CHM_USE_IO64)
    target_compile_options(chmlib_local PRIVATE -w)   # old C: its warnings aren't ours
    if(NOT WIN32)
        target_compile_options(chmlib_local PRIVATE -include strings.h)   # ffs(), which it uses undeclared
    endif()
    set(CHMLIB_INCLUDE_DIR "${CHMLIB_SOURCE_DIR}")
    set(CHMLIB_LIBRARY chmlib_local)
endif()
if(CHMLIB_INCLUDE_DIR AND CHMLIB_LIBRARY)
    target_compile_definitions(freereader_core PRIVATE HAVE_CHMLIB)
    target_include_directories(freereader_core PRIVATE ${CHMLIB_INCLUDE_DIR})
    target_link_libraries(freereader_core PUBLIC ${CHMLIB_LIBRARY})
    message(STATUS "chmlib: CHM help files")
endif()
if(HAVE_MUPDF)
    target_compile_definitions(freereader_core PRIVATE HAVE_MUPDF)
    target_include_directories(freereader_core PRIVATE "${MUPDF_DIR}/include")
    target_link_libraries(freereader_core PUBLIC "${MUPDF_DIR}/${MUPDF_BUILD}/libmupdf.a" "${MUPDF_DIR}/${MUPDF_BUILD}/libmupdf-third.a")
    if(MUPDF_SYSTEM_LIBS)
        if(STATIC_BUILD)
            target_link_directories(freereader_core PUBLIC ${MUPDF_DEPS_STATIC_LIBRARY_DIRS})
            target_link_libraries(freereader_core PUBLIC ${MUPDF_DEPS_STATIC_LIBRARIES})
        else()
            target_link_directories(freereader_core PUBLIC ${MUPDF_DEPS_LIBRARY_DIRS})
            target_link_libraries(freereader_core PUBLIC ${MUPDF_DEPS_LIBRARIES})
        endif()
    endif()
    target_link_libraries(freereader_core PUBLIC -lz -lm)   # after MuPDF's libraries, which use them
    message(STATUS "MuPDF in ${MUPDF_DIR}: PDF and XPS")
else()
    message(STATUS "No MuPDF (MUPDF_DIR): PDF and XPS files get a message")
endif()
# Hyphenation patterns (hyph-utf8: MIT, BSD, LPPL; each file's license header sits next to it).
file(GLOB HYPH_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/resources/hyphenation/*.txt)
qt_add_resources(freereader_core hyphenation PREFIX "/" BASE resources FILES ${HYPH_FILES})

if(BUILD_MOBILE)
    add_subdirectory(src/mobile)
endif()

if(NOT ANDROID)
# The screens (everything but main.cpp), in a library so local whole-app tests can link them.
file(GLOB UI_SOURCES CONFIGURE_DEPENDS src/app/*.cpp)
list(REMOVE_ITEM UI_SOURCES "${CMAKE_SOURCE_DIR}/src/app/main.cpp")
add_library(freereader_ui STATIC ${UI_SOURCES})
target_include_directories(freereader_ui PUBLIC src)
target_link_libraries(freereader_ui PUBLIC freereader_core Qt6::Widgets Qt6::Svg)
if(TARGET Qt6::TextToSpeech)
    target_link_libraries(freereader_ui PUBLIC Qt6::TextToSpeech)
    target_compile_definitions(freereader_ui PUBLIC HAVE_TEXT_TO_SPEECH)
endif()

set(SOURCES src/app/main.cpp)
if(WIN32)
    list(APPEND SOURCES packaging/app.rc)
endif()

qt_add_executable(app WIN32 ${SOURCES})
set_target_properties(app PROPERTIES OUTPUT_NAME ${APP_NAME})
target_include_directories(app PRIVATE src)
target_link_libraries(app PRIVATE freereader_ui)

# Renders resources/icon.svg to packaging/icon.ico; run it after changing the icon.
qt_add_executable(make_icon packaging/make_icon.cpp)
target_link_libraries(make_icon PRIVATE Qt6::Gui Qt6::Svg)
set_target_properties(make_icon PROPERTIES EXCLUDE_FROM_ALL ON)

if(STATIC_BUILD)
    # Only the plug-ins the app uses: SVG and the image formats books contain (PNG is built into Qt), the SQLite
    # driver, and on Windows the normal display plug-in plus "offscreen" for invisible tests.
    qt_import_plugins(app
        INCLUDE_BY_TYPE imageformats Qt6::QSvgPlugin Qt6::QJpegPlugin Qt6::QGifPlugin
        INCLUDE_BY_TYPE iconengines Qt6::QSvgIconPlugin
        INCLUDE_BY_TYPE sqldrivers Qt6::QSQLiteDriverPlugin)
    # WebP pages (manga), when this Qt has the plug-in. (TIFF is left out: the static libtiff needs JBIG and a
    # 12-bit JPEG library that aren't there.)
    set(EXTRA_IMAGE_PLUGINS "")
    foreach(plugin IN ITEMS QWebpPlugin)
        if(TARGET Qt6::${plugin})
            list(APPEND EXTRA_IMAGE_PLUGINS Qt6::${plugin})
        endif()
    endforeach()
    if(EXTRA_IMAGE_PLUGINS)
        qt_import_plugins(app INCLUDE_BY_TYPE imageformats Qt6::QSvgPlugin Qt6::QJpegPlugin Qt6::QGifPlugin ${EXTRA_IMAGE_PLUGINS})
    endif()
    if(WIN32)
        # HTTPS through Windows' own TLS (Schannel); speech through Windows' voices (SAPI).
        qt_import_plugins(app INCLUDE_BY_TYPE platforms Qt6::QWindowsIntegrationPlugin Qt6::QOffscreenIntegrationPlugin
            INCLUDE_BY_TYPE tls Qt6::QSchannelBackendPlugin
            EXCLUDE_BY_TYPE networkinformation)
        if(TARGET Qt6::QTextToSpeechSapiPlugin)
            qt_import_plugins(app INCLUDE_BY_TYPE texttospeech Qt6::QTextToSpeechSapiPlugin)
        endif()
    endif()
endif()

# The icon and the bundled fonts (SIL Open Font License; the license texts sit next to the fonts).
file(GLOB FONT_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/resources/fonts/*.ttf)
qt_add_resources(freereader_ui resources PREFIX "/" BASE resources FILES resources/icon.svg resources/check-light.svg resources/check-dark.svg ${FONT_FILES})

if(WIN32)
    target_compile_definitions(app PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MINGW)
        # No compiler runtime DLLs next to the exe; strip debug symbols from release builds.
        target_link_options(app PRIVATE -static-libgcc -static-libstdc++ $<$<CONFIG:Release>:-s>)
    endif()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(freereader_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_compile_options(freereader_ui PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_compile_options(app PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

install(TARGETS app RUNTIME DESTINATION bin)
endif()   # NOT ANDROID

# Local tests (kept out of the repository).
option(BUILD_LOCAL_TESTS "Build the local tests when the tests folder exists" ON)
if(BUILD_LOCAL_TESTS AND NOT ANDROID AND EXISTS "${CMAKE_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()
