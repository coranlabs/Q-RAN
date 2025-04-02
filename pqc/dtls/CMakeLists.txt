# dtls/CMakeLists.txt

set(dtls_sources dtls.c)

add_library(DTLS ${dtls_sources})

target_include_directories(DTLS PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_package(OpenSSL 3.3.1 REQUIRED)

target_link_libraries(DTLS PUBLIC OpenSSL::SSL OpenSSL::Crypto)

target_link_libraries(DTLS PRIVATE UTIL)
