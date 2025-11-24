cmake_minimum_required(VERSION 3.20)
project(MultiRoblox VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# -------------------------------------------------------------------
# 1. Pliki źródłowe
# -------------------------------------------------------------------
set(SOURCE_FILES
    main.c
    gui.c
    log.c
    updater.c
    account_manager.c
    account_storage.c
    account_dialogs.c
    resources.rc
    account_webview.cpp
    roblox_launch.cpp
)

add_executable(MultiRoblox WIN32 ${SOURCE_FILES})

# -------------------------------------------------------------------
# 2. WebView2 — INCLUDE (nagłówki)
# -------------------------------------------------------------------
# Zakładam, że masz:
# external/webview2/include
# external/webview2/lib
# external/webview2/dll
include_directories(${CMAKE_SOURCE_DIR}/external/webview2/include)

# -------------------------------------------------------------------
# 3. Standardowe include (Twój folder projektu)
# -------------------------------------------------------------------
target_include_directories(MultiRoblox PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# -------------------------------------------------------------------
# 4. WebView2 — LIB (linkowanie)
# -------------------------------------------------------------------
# Dodajemy folder z bibliotekami .lib
target_link_directories(MultiRoblox PRIVATE ${CMAKE_SOURCE_DIR}/external/webview2/lib)

# Linkowanie statycznej biblioteki WebView2
target_link_libraries(MultiRoblox PRIVATE
    WebView2LoaderStatic.lib
)

# -------------------------------------------------------------------
# 5. Linkowanie dodatkowych bibliotek systemowych
# -------------------------------------------------------------------
target_link_libraries(MultiRoblox PRIVATE
    Crypt32
    comctl32
    winhttp
    windowscodecs
    Msimg32
)

# -------------------------------------------------------------------
# 6. Kopiowanie WebView2Loader.dll obok EXE po kompilacji
# -------------------------------------------------------------------
add_custom_command(TARGET MultiRoblox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_SOURCE_DIR}/external/webview2/dll/WebView2Loader.dll"
        "$<TARGET_FILE_DIR:MultiRoblox>"
)

