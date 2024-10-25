/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2019 RDK Management
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
*/

#include "TTSClientPrivateCOMRPC.h"

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/socket.h>

// --- //

namespace TTS {

#define CHECK_CONNECTION_RETURN_ON_FAIL(ret) do {\
    if(!TextToSpeechServiceCOMRPC::Instance()->isActive()) { \
        TTSLOG_ERROR("Connection to TTS manager is not establised"); \
        return ret; \
    } } while(0)

#define UNUSED(x) (void)(x)

#define DEFAULT_SESSION_ID 1

// --- //

TTSClientPrivateCOMRPC::TTSClientPrivateCOMRPC(TTSConnectionCallback *callback, bool) :
    m_ttsEnabled(false),
    m_connectionCallback(callback),
    m_sessionCallback(nullptr),
    m_lastSpeechId(0),
    m_appId(0),
    m_firstQuery(true) {
    if (Core::SystemInfo::GetEnvironment(_T("CLIENT_IDENTIFIER"), m_callsign) == true) {
        std::string::size_type pos =  m_callsign.find(',');
        if (pos != std::string::npos)
        {
            m_callsign.erase(pos,std::string::npos);
        }
    }

    TextToSpeechServiceCOMRPC::Instance()->initialize(m_callsign);
    TextToSpeechServiceCOMRPC::Instance()->registerClient(this);

    if(TextToSpeechServiceCOMRPC::Instance()->isActive() && m_connectionCallback)
        m_connectionCallback->onTTSServerConnected();
}

TTSClientPrivateCOMRPC::~TTSClientPrivateCOMRPC() {
    TextToSpeechServiceCOMRPC::Instance()->unregisterClient(this);
    abort(DEFAULT_SESSION_ID, false);
    destroySession(DEFAULT_SESSION_ID);
}

TTS_Error TTSClientPrivateCOMRPC::enableTTS(bool enable) {
    if(!TextToSpeechServiceCOMRPC::Instance()->enableTTS(enable)) {
        TTSLOG_ERROR("Couldn't %s TTS", enable ? "enable" : "disable");
        return TTS_FAIL;
    }
    return TTS_OK;
}

TTS_Error TTSClientPrivateCOMRPC::listVoices(std::string &language, std::vector<std::string> &voices) {
    if(!TextToSpeechServiceCOMRPC::Instance()->listVoices(language,voices)) {
        TTSLOG_ERROR("Couldn't retrieve voice list");
        return TTS_FAIL;
    }
    return TTS_OK;
}


TTS_Error TTSClientPrivateCOMRPC::setTTSConfiguration(Configuration &config) {
    Exchange::ITextToSpeech::Configuration ttsconfig;
    ttsconfig.ttsEndPoint= config.ttsEndPoint;
    ttsconfig.ttsEndPointSecured = config.ttsEndPointSecured;
    ttsconfig.language = config.language;
    ttsconfig.voice = config.voice;
    ttsconfig.volume = (uint8_t) config.volume;
    ttsconfig.rate = config.rate;
    if(!TextToSpeechServiceCOMRPC::Instance()->setConfiguration(ttsconfig)) {
        TTSLOG_ERROR("Couldn't set default configuration");
        return TTS_FAIL;
    }
    return TTS_OK;
}

TTS_Error TTSClientPrivateCOMRPC::getTTSConfiguration(Configuration &config) {
    Exchange::ITextToSpeech::Configuration ttsconfig;
    if(!TextToSpeechServiceCOMRPC::Instance()->getConfiguration(ttsconfig)) {
        TTSLOG_ERROR("Couldn't get default configuration");
        return TTS_FAIL;
    }
    config.ttsEndPoint= ttsconfig.ttsEndPoint;
    config.ttsEndPointSecured = ttsconfig.ttsEndPointSecured;
    config.language = ttsconfig.language;
    config.voice = ttsconfig.voice;
    config.volume = (double) ttsconfig.volume;
    config.rate = ttsconfig.rate;
    return TTS_OK;
}

bool TTSClientPrivateCOMRPC::isTTSEnabled(bool force) {
    CHECK_CONNECTION_RETURN_ON_FAIL(false);

    force |= m_firstQuery;
    m_firstQuery = false;

    if(!force) {
        return m_ttsEnabled;
    } else {

        if(!TextToSpeechServiceCOMRPC::Instance()->isEnabled(m_ttsEnabled)) {
            TTSLOG_ERROR("Couldn't retrieve TTS enabled/disabled detail");
            return false;
        }
        TTSLOG_VERBOSE("TTS is %s", m_ttsEnabled ? "enabled" : "disabled");
        return m_ttsEnabled;
    }
}

uint32_t TTSClientPrivateCOMRPC::createSession(uint32_t appId, std::string appName, TTSSessionCallback *callback) {
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

TTS_Error TTSClientPrivateCOMRPC::destroySession(uint32_t sessionId) {
    UNUSED(sessionId);
    m_sessionCallback  = nullptr;
    return TTS_OK;
}

TTS_Error TTSClientPrivateCOMRPC::speak(uint32_t sessionId, SpeechData& data) {
    CHECK_CONNECTION_RETURN_ON_FAIL(TTS_FAIL);
    UNUSED(sessionId);

    TextToSpeechServiceCOMRPC::Instance()->registerSpeechEventHandlers(m_callsign);

    m_lastSpeechId = 0;
    if(!TextToSpeechServiceCOMRPC::Instance()->speak(m_callsign, data.text, m_lastSpeechId)) {
        return TTS_FAIL;
    }

    bool success = m_requestedSpeeches.add(data.id, m_lastSpeechId);
    TTSLOG_INFO("Requested speech with clientid-%d, serviceid-%d, is_duplicate_client_id=%d", data.id, m_lastSpeechId, !success);    
    return TTS_OK;
}

TTS_Error TTSClientPrivateCOMRPC::abort(uint32_t sessionId, bool clearPending) {
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

    if(!TextToSpeechServiceCOMRPC::Instance()->cancel(m_lastSpeechId)) {
        TTSLOG_ERROR("Coudn't abort");
        return TTS_FAIL;
    }

    return TTS_OK;
}

TTS_Error TTSClientPrivateCOMRPC::pause(uint32_t sessionId, uint32_t speechId) {
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

    if(!TextToSpeechServiceCOMRPC::Instance()->pause(serviceid)) {
        TTSLOG_ERROR("Coudn't pause");
        return TTS_FAIL;
    }

    return TTS_OK;
}

TTS_Error TTSClientPrivateCOMRPC::resume(uint32_t sessionId, uint32_t speechId) {
    CHECK_CONNECTION_RETURN_ON_FAIL(TTS_FAIL);
    UNUSED(sessionId);

    if(!m_ttsEnabled) {
        TTSLOG_WARNING("TTS is disabled, nothing to resume");
        return TTS_OK;
    }

    uint32_t serviceid = m_requestedSpeeches.getServiceId(speechId);
    if(!serviceid) {
        TTSLOG_WARNING("No speech in progress");
        return TTS_OK;
    }

    if(!TextToSpeechServiceCOMRPC::Instance()->resume(serviceid)) {
        TTSLOG_ERROR("Coudn't resume");
        return TTS_FAIL;
    }

    return TTS_OK;
}

bool TTSClientPrivateCOMRPC::isSpeaking(uint32_t sessionId) {
    CHECK_CONNECTION_RETURN_ON_FAIL(false);
    UNUSED(sessionId);

    if(m_requestedSpeeches.empty() || !m_lastSpeechId) {
        TTSLOG_WARNING("No speech in progress");
        return false;
    }

    bool isspeaking = false;
    if(!TextToSpeechServiceCOMRPC::Instance()->isSpeaking(m_lastSpeechId, isspeaking)) {
        TTSLOG_ERROR("isspeaking query failed");
        return false;
    }

    return isspeaking;
}

TTS_Error TTSClientPrivateCOMRPC::getSpeechState(uint32_t sessionId, uint32_t speechId, SpeechState &state) {
    CHECK_CONNECTION_RETURN_ON_FAIL(TTS_FAIL);
    UNUSED(sessionId);

    uint32_t serviceid = m_requestedSpeeches.getServiceId(speechId);
    if(!serviceid) {
        TTSLOG_WARNING("No speech in progress");
        return TTS_OK;
    }
    Exchange::ITextToSpeech::SpeechState istate;
    if(!TextToSpeechServiceCOMRPC::Instance()->getSpeechState(serviceid,istate)) {
        TTSLOG_ERROR("Couldn't retrieve speech state");
        return TTS_FAIL;
    }
    state = (SpeechState) istate;
    return TTS_OK;
}

void TTSClientPrivateCOMRPC::onTTSStateChange(bool enabled) {
    m_lastSpeechId = 0;
    m_ttsEnabled = enabled;
    if(m_connectionCallback) {
        TTSLOG_INFO("Got tts_state_changed event from TTS Manager for %p", this);
        m_connectionCallback->onTTSStateChanged(enabled);
    }
}

void TTSClientPrivateCOMRPC::onVoiceChange(std::string voice) {
    if(m_connectionCallback) {
        TTSLOG_INFO("Got voice_changed event from TTS Manager %p, new voice = %s", this, voice.c_str());
        m_connectionCallback->onVoiceChanged(voice);
    }
}

void TTSClientPrivateCOMRPC::onSpeechStart(uint32_t serviceSpeechId) {
    uint32_t clientSpeechId = m_requestedSpeeches.getClientId(serviceSpeechId);
    if(clientSpeechId && m_sessionCallback) {
        SpeechData data(clientSpeechId);
        TTSLOG_INFO("Got started event from session %u", DEFAULT_SESSION_ID);
        m_sessionCallback->onSpeechStart(m_appId, DEFAULT_SESSION_ID, data);
    }
}

void TTSClientPrivateCOMRPC::onSpeechPause(uint32_t serviceSpeechId) {
    uint32_t clientSpeechId = m_requestedSpeeches.getClientId(serviceSpeechId);
    if(clientSpeechId && m_sessionCallback) {
        TTSLOG_INFO("Got paused event from session %u", DEFAULT_SESSION_ID);
        m_sessionCallback->onSpeechPause(m_appId, DEFAULT_SESSION_ID, clientSpeechId);
    }
}

void TTSClientPrivateCOMRPC::onSpeechResume(uint32_t serviceSpeechId) {
    uint32_t clientSpeechId = m_requestedSpeeches.getClientId(serviceSpeechId);
    if(clientSpeechId && m_sessionCallback) {
        TTSLOG_INFO("Got resumed event from session %u", DEFAULT_SESSION_ID);
        m_sessionCallback->onSpeechResume(m_appId, DEFAULT_SESSION_ID, clientSpeechId);
    }
}

void TTSClientPrivateCOMRPC::onSpeechCancel(uint32_t serviceSpeechId) {
    uint32_t clientSpeechId = m_requestedSpeeches.removeServiceId(serviceSpeechId);
    if(clientSpeechId && m_sessionCallback) {
        TTSLOG_INFO("Got cancelled event from session %u, speech id %u", DEFAULT_SESSION_ID);
        m_sessionCallback->onSpeechCancelled(m_appId, DEFAULT_SESSION_ID, clientSpeechId);
    }
}

void TTSClientPrivateCOMRPC::onSpeechInterrupt(uint32_t serviceSpeechId) {
    uint32_t clientSpeechId = m_requestedSpeeches.removeServiceId(serviceSpeechId);
    if(clientSpeechId && m_sessionCallback) {
        TTSLOG_INFO("Got interrupted event from session %u", DEFAULT_SESSION_ID);
        m_sessionCallback->onSpeechInterrupted(m_appId, DEFAULT_SESSION_ID, clientSpeechId);
    }
}

void TTSClientPrivateCOMRPC::onNetworkError(uint32_t serviceSpeechId) {
    uint32_t clientSpeechId = m_requestedSpeeches.removeServiceId(serviceSpeechId);
    if(clientSpeechId && m_sessionCallback) {
        TTSLOG_INFO("Got networkerror event from session %u", DEFAULT_SESSION_ID);
        m_sessionCallback->onNetworkError(m_appId, DEFAULT_SESSION_ID, clientSpeechId);
    }
}

void TTSClientPrivateCOMRPC::onPlaybackError(uint32_t serviceSpeechId) {
    uint32_t clientSpeechId = m_requestedSpeeches.removeServiceId(serviceSpeechId);
    if(clientSpeechId && m_sessionCallback) {
        TTSLOG_INFO("Got playbackerror event from session %u", DEFAULT_SESSION_ID);
        m_sessionCallback->onPlaybackError(m_appId, DEFAULT_SESSION_ID, clientSpeechId);
    }
}

void TTSClientPrivateCOMRPC::onSpeechComplete(uint32_t serviceSpeechId) {
    uint32_t clientSpeechId = m_requestedSpeeches.removeServiceId(serviceSpeechId);
    if(clientSpeechId && m_sessionCallback) {
        SpeechData data(clientSpeechId);
        TTSLOG_INFO("Got spoke event from session %u", DEFAULT_SESSION_ID);
        m_sessionCallback->onSpeechComplete(m_appId, DEFAULT_SESSION_ID, data);
    }
}

} // namespace TTS
