cmake_minimum_required(VERSION 3.30)
project(deleteSearchQuery)

set(CMAKE_CXX_STANDARD 23)

add_executable(deleteSearchQuery WIN32
        main.cpp
        dcWebHook.cpp
        dcWebHook.h
        main.h
        fetcher.cpp
        fetcher.h
        deletion.cpp
        deletion.h
)

find_package(CURL REQUIRED)
target_link_libraries(deleteSearchQuery PRIVATE CURL::libcurl)

find_package(nlohmann_json REQUIRED)
target_link_libraries(deleteSearchQuery PRIVATE nlohmann_json::nlohmann_json)

find_package(wxWidgets CONFIG REQUIRED COMPONENTS core base)
target_link_libraries(deleteSearchQuery PRIVATE wx::core wx::base)