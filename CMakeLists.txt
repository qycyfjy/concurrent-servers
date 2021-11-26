cmake_minimum_required(VERSION 3.15.0)
project(concurrent)

set(CMAKE_CXX_FLAGS "-DNDEBUG -O2 -D__const__= -pipe -W -Wall -Wno-unused-parameter -fPIC -fno-omit-frame-pointer")

find_package(Libevent CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)
find_package(unofficial-libuv CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

add_executable(concurrent_seq concurrent_seq.cpp helpers.h helpers.cpp)
target_link_libraries(concurrent_seq PRIVATE fmt::fmt absl::status absl::statusor)

add_executable(concurrent_thread concurrent_thread.cpp helpers.h helpers.cpp)
target_link_libraries(concurrent_thread PRIVATE fmt::fmt absl::status absl::statusor)

add_executable(concurrent_threadpool concurrent_threadpool.cpp helpers.h helpers.cpp ThreadPool.h)
target_link_libraries(concurrent_threadpool PRIVATE fmt::fmt absl::status absl::statusor absl::synchronization)

add_executable(event_driven concurrent_epoll.cpp helpers.h helpers.cpp)
target_link_libraries(event_driven PRIVATE fmt::fmt absl::status absl::statusor)

add_executable(uv_server concurrent_uv.cpp helpers.h helpers.cpp)
target_link_libraries(uv_server PRIVATE fmt::fmt absl::status absl::statusor unofficial::libuv::libuv)