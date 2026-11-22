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

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Svg Sql Concurrent Network)
# Spoken pronunciation in the translation pop-up, when this Qt has the module.
find_package(Qt6 QUIET COMPONENTS TextToSpeech)
find_package(ZLIB REQUIRED)
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
    src/core/services/BookSearch.cpp
    src/core/practice/Scheduler.cpp
    src/core/practice/Answer.cpp
    src/core/translate/Translation.cpp
    src/core/translate/AiProviders.cpp
    src/core/translate/TranslationService.cpp
    src/core/translate/OfflineDictionary.cpp
    src/core/translate/DictionaryDownload.cpp
)
target_include_directories(freereader_core PUBLIC src)
target_compile_definitions(freereader_core PUBLIC APP_NAME="${APP_NAME}" APP_ORG="${APP_ORG}" APP_VERSION="${APP_VERSION}")
target_link_libraries(freereader_core PUBLIC Qt6::Gui Qt6::Sql Qt6::Concurrent Qt6::Network ZLIB::ZLIB)
if(WIN32)
    target_link_libraries(freereader_core PUBLIC advapi32)   # Credential Manager
endif()
# Hyphenation patterns (hyph-utf8: MIT, BSD, LPPL; each file's license header sits next to it).
file(GLOB HYPH_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/resources/hyphenation/*.txt)
qt_add_resources(freereader_core hyphenation PREFIX "/" BASE resources FILES ${HYPH_FILES})

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

# Local tests (kept out of the repository).
option(BUILD_LOCAL_TESTS "Build the local tests when the tests folder exists" ON)
if(BUILD_LOCAL_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()
