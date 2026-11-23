# FreeReader for phones and tablets: a Qt Quick touch interface over the same core as the desktop app (books,
# layout, library, word log, translation). Android builds this; desktops can build it for testing.
find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2)

qt_add_executable(freereader_mobile
    main.cpp
    MobileApp.cpp
    BookPage.cpp
    ../app/Settings.cpp
    ../app/Appearance.cpp
    ../app/ThemeCore.cpp
    ../app/ThemeData.cpp
)
set_target_properties(freereader_mobile PROPERTIES OUTPUT_NAME FreeReaderMobile)
if(ANDROID)
    # The Android app is a library loaded by Qt's Java launcher; its name is the package's library.
    set_target_properties(freereader_mobile PROPERTIES OUTPUT_NAME FreeReader)
endif()

qt_add_qml_module(freereader_mobile
    URI FreeReaderMobile
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/LibraryPage.qml
        qml/ReaderPage.qml
        qml/LookupSheet.qml
        qml/SettingsPage.qml
        qml/Theme.qml
        qml/BarButton.qml
        qml/Icon.qml
        qml/IconButton.qml
        qml/SelectionBar.qml
        qml/DictionarySheet.qml
        qml/NoteEditor.qml
        qml/Navigator.qml
        qml/SearchSheet.qml
        qml/ThemeList.qml
        qml/WordsPage.qml
    SOURCES
        BookPage.h
        MobileApp.h
)

target_include_directories(freereader_mobile PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(freereader_mobile PRIVATE freereader_core Qt6::Quick Qt6::QuickControls2)

# The bundled fonts and the icon (SIL Open Font License; the license texts sit next to the fonts).
file(GLOB MOBILE_FONT_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/resources/fonts/*.ttf)
qt_add_resources(freereader_mobile mobile_resources PREFIX "/" BASE ${CMAKE_SOURCE_DIR}/resources
    FILES ${CMAKE_SOURCE_DIR}/resources/icon.svg ${MOBILE_FONT_FILES})

if(ANDROID)
    # The app's own Android files (manifest, icons), when there are any; else Qt's defaults.
    if(EXISTS ${CMAKE_SOURCE_DIR}/android/AndroidManifest.xml)
        set_target_properties(freereader_mobile PROPERTIES QT_ANDROID_PACKAGE_SOURCE_DIR ${CMAKE_SOURCE_DIR}/android)
    endif()
    set_target_properties(freereader_mobile PROPERTIES
        QT_ANDROID_TARGET_SDK_VERSION 35
        QT_ANDROID_MIN_SDK_VERSION 28
        QT_ANDROID_VERSION_NAME ${APP_VERSION}
        QT_ANDROID_VERSION_CODE 1)
    # HTTPS (Gemini, the dictionary downloads): Qt for Android uses OpenSSL, which isn't part of Android.
    if(DEFINED FREEREADER_ANDROID_OPENSSL_DIR)
        set_property(TARGET freereader_mobile APPEND PROPERTY QT_ANDROID_EXTRA_LIBS
            ${FREEREADER_ANDROID_OPENSSL_DIR}/${CMAKE_ANDROID_ARCH_ABI}/libcrypto_3.so
            ${FREEREADER_ANDROID_OPENSSL_DIR}/${CMAKE_ANDROID_ARCH_ABI}/libssl_3.so)
    endif()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(freereader_mobile PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
