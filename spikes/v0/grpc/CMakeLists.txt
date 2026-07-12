cmake_minimum_required(VERSION 3.20)
project(grpc-spike CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPCPP REQUIRED IMPORTED_TARGET grpc++)
pkg_check_modules(PROTOBUF REQUIRED IMPORTED_TARGET protobuf)

set(PROTO ${CMAKE_CURRENT_SOURCE_DIR}/spike.proto)
set(GEN ${CMAKE_CURRENT_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${GEN})
add_custom_command(
  OUTPUT ${GEN}/spike.pb.cc ${GEN}/spike.pb.h
         ${GEN}/spike.grpc.pb.cc ${GEN}/spike.grpc.pb.h
  COMMAND protoc --proto_path=${CMAKE_CURRENT_SOURCE_DIR}
          --cpp_out=${GEN} --grpc_out=${GEN}
          --plugin=protoc-gen-grpc=/usr/bin/grpc_cpp_plugin
          ${PROTO}
  DEPENDS ${PROTO}
  VERBATIM)

add_executable(grpc-spike-server
  server.cpp
  ${GEN}/spike.pb.cc
  ${GEN}/spike.grpc.pb.cc)
target_include_directories(grpc-spike-server PRIVATE ${GEN})
target_link_libraries(grpc-spike-server PRIVATE
  Qt6::Core PkgConfig::GRPCPP PkgConfig::PROTOBUF)
