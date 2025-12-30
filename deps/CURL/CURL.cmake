set(_curl_platform_flags
  -DENABLE_IPV6:BOOL=ON
  -DENABLE_VERSIONED_SYMBOLS:BOOL=ON
  -DENABLE_THREADED_RESOLVER:BOOL=ON
  -DENABLE_MANUAL:BOOL=OFF
  -DENABLE_WEBSOCKETS:BOOL=ON
  -DCURL_DISABLE_LDAP:BOOL=ON
  -DCURL_DISABLE_LDAPS:BOOL=ON
  -DCURL_DISABLE_RTSP:BOOL=ON
  -DCURL_DISABLE_DICT:BOOL=ON
  -DCURL_DISABLE_TELNET:BOOL=ON
  -DCURL_DISABLE_POP3:BOOL=ON
  -DCURL_DISABLE_IMAP:BOOL=ON
  -DCURL_DISABLE_SMB:BOOL=ON
  -DCURL_DISABLE_SMTP:BOOL=ON
  -DCURL_DISABLE_GOPHER:BOOL=ON
  -DCURL_DISABLE_TFTP:BOOL=ON
  -DCURL_DISABLE_MQTT:BOOL=ON
  #-DHTTP_ONLY=ON

  -DCMAKE_USE_GSSAPI:BOOL=OFF
  -DCMAKE_USE_LIBSSH2:BOOL=OFF
  -DCURL_USE_LIBSSH2:BOOL=OFF
  -DCURL_DISABLE_SCP:BOOL=ON
  -DCURL_DISABLE_SFTP:BOOL=ON
  -DUSE_LIBIDN2:BOOL=OFF
  -DCURL_USE_LIBIDN2:BOOL=OFF
  -DHAVE_LIBIDN2:BOOL=OFF
  -DUSE_RTMP:BOOL=OFF
  -DUSE_NGHTTP2:BOOL=OFF
  -DUSE_MBEDTLS:BOOL=OFF
)

if (WIN32)
  #set(_curl_platform_flags  ${_curl_platform_flags} -DCMAKE_USE_SCHANNEL=ON)
  set(_curl_platform_flags  ${_curl_platform_flags} -DCMAKE_USE_OPENSSL=ON -DCURL_CA_PATH:STRING=none)
elseif (APPLE)
  set(_curl_platform_flags

    ${_curl_platform_flags}

    #-DCMAKE_USE_SECTRANSP:BOOL=ON
    -DCMAKE_USE_OPENSSL:BOOL=ON

    -DCURL_CA_PATH:STRING=none

    # Explicitly prevent libidn2 auto-detection by setting paths to empty
    -DLibidn2_LIBRARY:FILEPATH=""
    -DLibidn2_INCLUDE_DIR:PATH=""
  )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(_curl_platform_flags 

    ${_curl_platform_flags}

    -DCMAKE_USE_OPENSSL:BOOL=ON

    -DCURL_CA_PATH:STRING=none
    -DCURL_CA_BUNDLE:STRING=none
    -DCURL_CA_FALLBACK:BOOL=ON
  )
endif ()

if (BUILD_SHARED_LIBS)
  set(_curl_static OFF)
else()
  set(_curl_static ON)
endif()

phrozenorca_add_cmake_project(CURL
  # GIT_REPOSITORY      https://github.com/curl/curl.git
  # GIT_TAG             curl-7_75_0
  #URL                 https://github.com/curl/curl/archive/refs/tags/curl-7_75_0.zip
  #URL_HASH            SHA256=a63ae025bb0a14f119e73250f2c923f4bf89aa93b8d4fafa4a9f5353a96a765a
  URL                 https://curl.se/download/curl-8.11.0.zip
  URL_HASH            SHA256=3efe0ec3ec585d4cfa44d2a9f63704240e467bb48bcec52da42e0d5a1ed5362d
  DEPENDS             ${ZLIB_PKG}
  # PATCH_COMMAND       ${GIT_EXECUTABLE} checkout -f -- . && git clean -df && 
  #                     ${GIT_EXECUTABLE} apply --whitespace=fix ${CMAKE_CURRENT_LIST_DIR}/curl-mods.patch
  CMAKE_ARGS
    -DBUILD_TESTING:BOOL=OFF
    -DBUILD_CURL_EXE:BOOL=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCURL_STATICLIB=${_curl_static}
    ${_curl_platform_flags}
)

if(NOT OPENSSL_FOUND)
  # (openssl may or may not be built)
  add_dependencies(dep_CURL ${OPENSSL_PKG})
endif()

if (MSVC)
    add_debug_dep(dep_CURL)
endif ()
