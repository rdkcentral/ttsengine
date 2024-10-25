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

pkg_check_modules(WPEFRAMEWORK_PLUGINS REQUIRED WPEFrameworkPlugins)
pkg_check_modules(WPEFRAMEWORK_SECURITYUTIL WPEFrameworkSecurityUtil)

# Build TTS Service Client library
set(TextToSpeechServiceClient_SOURCES
        TextToSpeechServiceCOMRPC.cpp
        TextToSpeechService.cpp
        Service.cpp
        ../common/logger.cpp
	TextToSpeechServiceFirebolt.cpp
   )
add_library(TextToSpeechServiceClient SHARED ${TextToSpeechServiceClient_SOURCES})

# Build TTS Service Client library
set(TTSClient_SOURCES
        TTSClient.cpp
        TTSClientPrivateJsonRPC.cpp
        TTSClientPrivateCOMRPC.cpp
	TTSClientPrivateFirebolt.cpp
   )
add_library(TTSClient SHARED ${TTSClient_SOURCES})

target_include_directories(TextToSpeechServiceClient PUBLIC ${WPEFRAMEWORK_PLUGINS_INCLUDE_DIRS})
target_include_directories(TTSClient PUBLIC ${GLIB_INCLUDE_DIRS})

if(NOT WPEFRAMEWORK_SECURITYUTIL_FOUND)
    add_definitions(-DSECURITY_TOKEN_ENABLED=0)
endif()

if(TTS_DEFAULT_BACKEND)
    add_definitions(-DTTS_DEFAULT_BACKEND="${TTS_DEFAULT_BACKEND}")
endif()

target_link_libraries(TextToSpeechServiceClient PUBLIC
        ${WPEFRAMEWORK_PLUGINS_LIBRARIES}
        ${WPEFRAMEWORK_SECURITYUTIL_LIBRARIES}
        -lpthread
	    FireboltSDK
    )

target_link_libraries(TTSClient PUBLIC
        ${GLIB_LIBRARIES}
        ${LIBS}
        TextToSpeechServiceClient
    )

install(TARGETS TTSClient TextToSpeechServiceClient LIBRARY DESTINATION lib)
install(FILES TTSClient.h ../common/TTSCommon.h TextToSpeechService.h Service.h DESTINATION include)
