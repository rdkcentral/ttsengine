################################################################################
# If not stated otherwise in this file or this component's LICENSE file the
# following copyright and licenses apply:
#
# Copyright 2019 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
################################################################################
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

if (ENABLE_FIREBOLT_ONLY)
set(TTS_DEFAULT_BACKEND "firebolt")
add_definitions(-DENABLE_FIREBOLT_ONLY=1)
endif()

if(TTS_DEFAULT_BACKEND STREQUAL "firebolt")
find_package(FireboltClient CONFIG REQUIRED)
find_package(FireboltTransport CONFIG REQUIRED)
endif()

if (NOT ENABLE_FIREBOLT_ONLY)
pkg_check_modules(WPEFRAMEWORK_PLUGINS REQUIRED WPEFrameworkPlugins)
pkg_check_modules(WPEFRAMEWORK_SECURITYUTIL WPEFrameworkSecurityUtil)
endif()

# Build TTS Service Client library
set(TextToSpeechServiceClient_SOURCES
    ../common/logger.cpp
)

if (NOT ENABLE_FIREBOLT_ONLY)
    list(APPEND TextToSpeechServiceClient_SOURCES
        TextToSpeechServiceCOMRPC.cpp
        TextToSpeechService.cpp
        Service.cpp
        )
endif()

if(TTS_DEFAULT_BACKEND STREQUAL "firebolt")
    list(APPEND TextToSpeechServiceClient_SOURCES TextToSpeechServiceFirebolt.cpp)
endif()

add_library(TextToSpeechServiceClient SHARED ${TextToSpeechServiceClient_SOURCES})

# Build TTS Service Client library
set(TTSClient_SOURCES
    TTSClient.cpp
)

if (NOT ENABLE_FIREBOLT_ONLY)
    list(APPEND TTSClient_SOURCES
        TTSClientPrivateJsonRPC.cpp
        TTSClientPrivateCOMRPC.cpp
        )
endif()


if(TTS_DEFAULT_BACKEND STREQUAL "firebolt")
    add_definitions(-DTTS_DEFAULT_BACKEND_FIREBOLT)
    list(APPEND TTSClient_SOURCES TTSClientPrivateFirebolt.cpp)
endif()

add_library(TTSClient SHARED ${TTSClient_SOURCES})

target_include_directories(TextToSpeechServiceClient PUBLIC ${WPEFRAMEWORK_PLUGINS_INCLUDE_DIRS})

if(NOT WPEFRAMEWORK_SECURITYUTIL_FOUND)
    add_definitions(-DSECURITY_TOKEN_ENABLED=0)
endif()

if(TTS_DEFAULT_BACKEND)
    add_definitions(-DTTS_DEFAULT_BACKEND="${TTS_DEFAULT_BACKEND}")
endif()

if(TTS_DEFAULT_BACKEND STREQUAL "firebolt")
    target_link_libraries(TextToSpeechServiceClient PUBLIC
        -lpthread
        FireboltClient::FireboltClient
        FireboltTransport::FireboltTransport
    )
    if (NOT ENABLE_FIREBOLT_ONLY)
    target_link_libraries(TextToSpeechServiceClient PUBLIC
        ${WPEFRAMEWORK_PLUGINS_LIBRARIES}
        ${WPEFRAMEWORK_SECURITYUTIL_LIBRARIES}
    )
    endif()
else()
    target_link_libraries(TextToSpeechServiceClient PUBLIC
        ${WPEFRAMEWORK_PLUGINS_LIBRARIES}
        ${WPEFRAMEWORK_SECURITYUTIL_LIBRARIES}
        -lpthread
    )
endif()

target_link_libraries(TTSClient PUBLIC
    ${LIBS}
    TextToSpeechServiceClient
)

install(TARGETS TTSClient TextToSpeechServiceClient LIBRARY DESTINATION lib)
install(FILES TTSClient.h ../common/TTSCommon.h DESTINATION include)
if(NOT ENABLE_FIREBOLT_ONLY)
install(FILES TextToSpeechService.h Service.h DESTINATION include)
endif()
