cmake_minimum_required(VERSION 3.20)
project(usbip_libusb_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

add_executable(usbip-libusb-server
    src/main.cpp
    src/NetUtil.cpp
    src/UsbIpProtocol.cpp
    src/LibusbBackend.cpp
    src/TransferManager.cpp
    src/UsbIpServer.cpp
)

target_include_directories(usbip-libusb-server PRIVATE
    src
    ${LIBUSB_INCLUDE_DIRS}
)

target_link_libraries(usbip-libusb-server PRIVATE ${LIBUSB_LIBRARIES} Threads::Threads)
