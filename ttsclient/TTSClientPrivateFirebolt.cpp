/*
 * Copyright 2023 Comcast Cable Communications Management, LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "TTSClientPrivateFirebolt.h"

namespace TTS {

#define CHECK_CONNECTION_RETURN_ON_FAIL(ret) do {\
    if(!TextToSpeechServiceFirebolt::Instance()->isActive()) { \
        TTSLOG_ERROR("Connection to TTS manager is not establised"); \
        return ret; \
    } } while(0)

#define UNUSED(x) (void)(x)

#define DEFAULT_SESSION_ID 1

TTSClientPrivateFirebolt::TTSClientPrivateFirebolt(TTSConnectionCallback *callback, bool) :
    m_ttsEnabled(false),
    m_connectionCallback(callback),
    m_sessionCallback(nullptr),
    m_lastSpeechId(0),
    m_appId(0),
    m_firstQuery(true) {
    const char* env_client = std::getenv("CLIENT_IDENTIFIER");
    if (env_client) {
        m_callsign.assign(env_client);
        std::string::size_type pos =  m_callsign.find(',');
        if (pos != std::string::npos) {
            m_callsign.erase(pos,std::string::npos);
        }
    }
    TextToSpeechServiceFirebolt::Instance()->initialize();
    TextToSpeechServiceFirebolt::Instance()->registerClient(this);

    if(TextToSpeechServiceFirebolt::Instance()->isActive() && m_connectionCallback)
        m_connectionCallback->onTTSServerConnected();     	
}

TTSClientPrivateFirebolt::~TTSClientPrivateFirebolt() {
    TextToSpeechServiceFirebolt::Instance()-> deinitialize();
    TextToSpeechServiceFirebolt::Instance()->unregisterClient(this);
    abort(DEFAULT_SESSION_ID, false);
    destroySession(DEFAULT_SESSION_ID);
}

bool TTSClientPrivateFirebolt::isTTSEnabled(bool force) {
    CHECK_CONNECTION_RETURN_ON_FAIL(false);
    force |= m_firstQuery;
    m_firstQuery = false;

    if(!force) {
        return m_ttsEnabled;
    } else {

        if(!TextToSpeechServiceFirebolt::Instance()->isEnabled(m_ttsEnabled)) {
            TTSLOG_ERROR("TTS disabled");
            return false;
        }
        TTSLOG_VERBOSE("TTS is %s", m_ttsEnabled ? "enabled" : "disabled");
        return m_ttsEnabled;
    }
}

TTS_Error TTSClientPrivateFirebolt::pause(uint32_t sessionId, uint32_t speechId) {
    CHECK_CONNECTION_RETURN_ON_FAIL(TTS_FAIL);
    UNUSED(sessionId);

    if(!m_ttsEnabled) {
        TTSLOG_WARNING("TTS is disabled, nothing to pause");
        return TTS_OK;
    }

    uint32_t serviceid = m_requestedSpeeches.getServiceId(speechId);
    if(!serviceid) {
        TTSLOG_WARNING("No speech in progress");
        return TTS_OK;
    }

    if(!TextToSpeechServiceFirebolt::Instance()->pause(serviceid)) {
        TTSLOG_ERROR("Coudn't pause");
        return TTS_FAIL;
    }

    return TTS_OK;
}

TTS_Error TTSClientPrivateFirebolt::listVoices(std::string &language, std::vector<std::string> &voices) {
    if(!TextToSpeechServiceFirebolt::Instance()->listVoices(language,voices)) {
        TTSLOG_ERROR("Couldn't retrieve voice list");
        return TTS_FAIL;
    }
    return TTS_OK;
}

TTS_Error TTSClientPrivateFirebolt::enableTTS(bool enable) {
    TTSLOG_WARNING("enableTTS not supported through firebolt %d", enable);
    return TTS::TTS_OK;
}

TTS_Error TTSClientPrivateFirebolt::resume(uint32_t sessionId, uint32_t speechId) {
    CHECK_CONNECTION_RETURN_ON_FAIL(TTS_FAIL);
    UNUSED(sessionId);

    if(!m_ttsEnabled) {
        TTSLOG_WARNING("TTS is disabled, nothing to resume");
        return TTS_OK;
    }

    // Firebolt Expecting speechId
    
    uint32_t serviceid = m_requestedSpeeches.getServiceId(speechId);
    if(!serviceid) {
        TTSLOG_WARNING("No speech in progress");
        return TTS_OK;
    }

    if(!TextToSpeechServiceFirebolt::Instance()->resume(serviceid)) {
        TTSLOG_ERROR("Coudn't resume");
        return TTS_FAIL;
    }

    return TTS_OK;
}

bool TTSClientPrivateFirebolt::isSpeaking(uint32_t sessionId) {
    CHECK_CONNECTION_RETURN_ON_FAIL(false);
    UNUSED(sessionId);

    if(m_requestedSpeeches.empty() || !m_lastSpeechId) {
        TTSLOG_WARNING("No speech in progress");
        return false;
    }

    bool isspeaking = false;
    if(!TextToSpeechServiceFirebolt::Instance()->isSpeaking(m_lastSpeechId, isspeaking)) {
        TTSLOG_ERROR("isspeaking query failed");
        return false;
    }

    return isspeaking;
}

TTS_Error TTSClientPrivateFirebolt::getSpeechState(uint32_t sessionId, uint32_t speechId, SpeechState &state) {
    CHECK_CONNECTION_RETURN_ON_FAIL(TTS_FAIL);
    UNUSED(sessionId);

    uint32_t serviceid = m_requestedSpeeches.getServiceId(speechId);
    
    if(!serviceid) {
        TTSLOG_WARNING("No speech in progress");
        return TTS_OK;
    }
    TTSLOG_WARNING("state is SPEECH_IN_PROGRESS = %d", state == SPEECH_IN_PROGRESS); // Just to resolve unused compilation error
    Firebolt::TextToSpeech::SpeechStateResponse speechStateResponse;
    if(!TextToSpeechServiceFirebolt::Instance()->getSpeechState(speechId,speechStateResponse)) {
        TTSLOG_ERROR("Couldn't retrieve speech state");
        return TTS_FAIL;
    }
    // ! Enum state expects enum value;
    //state = (SpeechState) speechStateResponse.speechstate ;
    return TTS_OK;  
}

TTS_Error TTSClientPrivateFirebolt::destroySession(uint32_t sessionId) {
    UNUSED(sessionId);
    m_sessionCallback  = nullptr;
    return TTS_OK;
}

// speak API requires the SpeechData parameter; Firebolt is not using this
TTS_Error TTSClientPrivateFirebolt::speak(uint32_t sessionId, SpeechData& data) {
    CHECK_CONNECTION_RETURN_ON_FAIL(TTS_FAIL);
    UNUSED(sessionId);

    /*if(!m_ttsEnabled) {
        TTSLOG_ERROR("TTS is disabled, can't speak");
        return TTS_NOT_ENABLED;
    }*/
    m_lastSpeechId = 0;
    if(!TextToSpeechServiceFirebolt::Instance()->speak(m_callsign, data.text, m_lastSpeechId)) {
        return TTS_FAIL;
    }

    bool success = m_requestedSpeeches.add(data.id, m_lastSpeechId);
    TTSLOG_INFO("Requested speech with clientid-%d, serviceid-%d, is_duplicate_client_id=%d", data.id, m_lastSpeechId, !success);    
    return TTS_OK;   
}

TTS_Error TTSClientPrivateFirebolt::abort(uint32_t sessionId, bool clearPending) {
    CHECK_CONNECTION_RETURN_ON_FAIL(TTS_FAIL);
    UNUSED(sessionId);
    UNUSED(clearPending);

    if(!m_ttsEnabled) {
        TTSLOG_WARNING("TTS is disabled, nothing to abort");
        return TTS_OK;
    }

    if(m_requestedSpeeches.empty() || !m_lastSpeechId) {
        TTSLOG_WARNING("No speech in progress");
        return TTS_OK;
    }

    if(!TextToSpeechServiceFirebolt::Instance()->cancel(m_lastSpeechId)) {
        TTSLOG_ERROR("Coudn't abort");
        return TTS_FAIL;
    }

    return TTS_OK;
}

TTS_Error TTSClientPrivateFirebolt::setTTSConfiguration(Configuration &config) {
    Firebolt::TextToSpeech::TTSConfiguration ttsConfiguration;

    ttsConfiguration.ttsendpoint= config.ttsEndPoint;
    ttsConfiguration.ttsendpointsecured = config.ttsEndPointSecured;
    ttsConfiguration.language = config.language;
    ttsConfiguration.voice = config.voice;
    ttsConfiguration.volume = (int32_t) config.volume;
    ttsConfiguration.rate = config.rate;
    if(!TextToSpeechServiceFirebolt::Instance()->setConfiguration(ttsConfiguration)) {
        TTSLOG_ERROR("Couldn't set default configuration");
        return TTS_FAIL;
    }
    return TTS_OK;
}

TTS_Error TTSClientPrivateFirebolt::getTTSConfiguration(Configuration &config) {
    Firebolt::TextToSpeech::TTSConfiguration ttsConfiguration;
    if(!TextToSpeechServiceFirebolt::Instance()->getConfiguration(ttsConfiguration)) {
        TTSLOG_ERROR("Couldn't get default configuration");
        return TTS_FAIL;
    }
    config.ttsEndPoint= ttsConfiguration.ttsendpoint.value();
    config.ttsEndPointSecured = ttsConfiguration.ttsendpointsecured.value();
    config.language = ttsConfiguration.language.value();
    config.voice = ttsConfiguration.voice.value();
    config.volume = ttsConfiguration.volume.value();
    config.rate = ttsConfiguration.rate.value();
    return TTS_OK;
}

uint32_t TTSClientPrivateFirebolt::createSession(uint32_t appId, std::string appName, TTSSessionCallback *callback) {
    CHECK_CONNECTION_RETURN_ON_FAIL(0);
    UNUSED(appName);

    m_appId = appId;
    if(m_connectionCallback) {
        isTTSEnabled(true);
        m_connectionCallback->onTTSStateChanged(m_ttsEnabled);
    }

    m_sessionCallback = callback;
    if(m_sessionCallback)
        m_sessionCallback->onTTSSessionCreated(m_appId, DEFAULT_SESSION_ID);

    return DEFAULT_SESSION_ID;
}

void TTSClientPrivateFirebolt::onTTSStateChange(bool enabled) {
    m_lastSpeechId = 0;
    m_ttsEnabled = enabled;
    if(m_connectionCallback) {
        TTSLOG_INFO("Got tts_state_changed event from TTS Manager for %p", this);
        m_connectionCallback->onTTSStateChanged(enabled);
    }
}

void TTSClientPrivateFirebolt::onVoiceChange(std::string voice) {
    if(m_connectionCallback) {
        TTSLOG_INFO("Got voice_changed event from TTS Manager %p, new voice = %s", this, voice.c_str());
        m_connectionCallback->onVoiceChanged(voice);
    }
}

void TTSClientPrivateFirebolt::onSpeechStart(uint32_t serviceSpeechId) {
    uint32_t clientSpeechId = m_requestedSpeeches.getClientId(serviceSpeechId);
    if(clientSpeechId && m_sessionCallback) {
        SpeechData data(clientSpeechId);
        TTSLOG_INFO("Got started event from session %u", DEFAULT_SESSION_ID);
        m_sessionCallback->onSpeechStart(m_appId, DEFAULT_SESSION_ID, data);
    }
}

void TTSClientPrivateFirebolt::onSpeechPause(uint32_t serviceSpeechId) {
    uint32_t clientSpeechId = m_requestedSpeeches.getClientId(serviceSpeechId);
    if(clientSpeechId && m_sessionCallback) {
        TTSLOG_INFO("Got paused event from session %u", DEFAULT_SESSION_ID);
        m_sessionCallback->onSpeechPause(m_appId, DEFAULT_SESSION_ID, clientSpeechId);
    }
}

void TTSClientPrivateFirebolt::onSpeechResume(uint32_t serviceSpeechId) {
    uint32_t clientSpeechId = m_requestedSpeeches.getClientId(serviceSpeechId);
    if(clientSpeechId && m_sessionCallback) {
        TTSLOG_INFO("Got resumed event from session %u", DEFAULT_SESSION_ID);
        m_sessionCallback->onSpeechResume(m_appId, DEFAULT_SESSION_ID, clientSpeechId);
    }
}

void TTSClientPrivateFirebolt::onSpeechCancel(uint32_t serviceSpeechId) {
    uint32_t clientSpeechId = m_requestedSpeeches.removeServiceId(serviceSpeechId);
    if(clientSpeechId && m_sessionCallback) {
        TTSLOG_INFO("Got cancelled event from session %u, speech id %u", DEFAULT_SESSION_ID);
        m_sessionCallback->onSpeechCancelled(m_appId, DEFAULT_SESSION_ID, clientSpeechId);
    }
}

void TTSClientPrivateFirebolt::onSpeechInterrupt(uint32_t speeechId){
    uint32_t clientSpeechId = m_requestedSpeeches.removeServiceId(speeechId);
    if(clientSpeechId && m_sessionCallback) {
        //TTSLOG_INFO("Got interrupted event from session %u", DEFAULT_SESSION_ID);
        m_sessionCallback->onSpeechInterrupted(m_appId, DEFAULT_SESSION_ID, clientSpeechId);
    }
}

void TTSClientPrivateFirebolt::onNetworkError(uint32_t speeechId) {
    uint32_t clientSpeechId = m_requestedSpeeches.removeServiceId(speeechId);
    if(clientSpeechId && m_sessionCallback) {
        //TTSLOG_INFO("Got networkerror event from session %u", DEFAULT_SESSION_ID);
        m_sessionCallback->onNetworkError(m_appId, DEFAULT_SESSION_ID, clientSpeechId);
    }
}

void TTSClientPrivateFirebolt::onPlaybackError(uint32_t serviceSpeechId) {
    uint32_t clientSpeechId = m_requestedSpeeches.removeServiceId(serviceSpeechId);
    if(clientSpeechId && m_sessionCallback) {
        TTSLOG_INFO("Got playbackerror event from session %u", DEFAULT_SESSION_ID);
        m_sessionCallback->onPlaybackError(m_appId, DEFAULT_SESSION_ID, clientSpeechId);
    }
}

void TTSClientPrivateFirebolt::onSpeechComplete(uint32_t serviceSpeechId) {
    uint32_t clientSpeechId = m_requestedSpeeches.removeServiceId(serviceSpeechId);
    if(clientSpeechId && m_sessionCallback) {
        SpeechData data(clientSpeechId);
        TTSLOG_INFO("Got spoke event from session %u", DEFAULT_SESSION_ID);
        m_sessionCallback->onSpeechComplete(m_appId, DEFAULT_SESSION_ID, data);
    }
}

} // namespace TTS
