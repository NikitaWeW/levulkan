cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
project(levulkan C CXX)

add_subdirectory(.. ${PROJECT_BINARY_DIR}/egn)

set(APP_SOURCES 
    "main.cpp"
    "Controller.cpp"
)

add_executable(levulkan ${APP_SOURCES})
target_link_libraries(levulkan PRIVATE engine)
target_include_directories(levulkan PRIVATE "src")