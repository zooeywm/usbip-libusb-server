cmake_minimum_required(VERSION 3.20)
project(usbip_libusb_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

set(PLATFORM_USB_SOURCE src/PlatformUsbLinux.cpp)
if(APPLE)
    set(PLATFORM_USB_SOURCE src/PlatformUsbDarwin.cpp)
elseif(NOT UNIX)
    message(FATAL_ERROR "This prototype currently supports Linux and macOS only")
endif()

add_executable(usbip-libusb-server
    src/main.cpp
    src/NetUtil.cpp
    src/UsbIpProtocol.cpp
    src/LibusbBackend.cpp
    src/TransferManager.cpp
    src/UsbIpServer.cpp
    ${PLATFORM_USB_SOURCE}
)

target_include_directories(usbip-libusb-server PRIVATE
    src
    ${LIBUSB_INCLUDE_DIRS}
)

target_link_libraries(usbip-libusb-server PRIVATE
    ${LIBUSB_LIBRARIES}
    Threads::Threads
)

target_compile_options(usbip-libusb-server PRIVATE ${LIBUSB_CFLAGS_OTHER})
