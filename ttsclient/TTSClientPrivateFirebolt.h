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

#include <iostream>
#include <mutex>
#include <condition_variable>
#include <mutex>
#include <map>

#include "logger.h"
#define _LOG_INFO TTSLOG_INFO

#include "TTSClient.h"
#include "TTSClientPrivateInterface.h"
#include "TextToSpeechServiceFirebolt.h"
#include "TTSCommon.h"

using namespace TTSFirebolt;

namespace TTS {

class FireboltSpeechRequestMap {
public:
    bool add(uint32_t clientid, uint32_t serviceid) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if(m_map.find(clientid) == m_map.end()) {
            m_map[clientid] = serviceid;
            return true;
        }
        return false;
    }

    uint32_t getServiceId(uint32_t clientid) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for(auto it = m_map.begin(); it != m_map.end(); ++it)
            if(it->first == clientid)
                return it->second;
        return 0;
    }

    uint32_t getClientId(uint32_t serviceid) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for(auto it = m_map.begin(); it != m_map.end(); ++it)
            if(it->second == serviceid)
                return it->first;
        return 0;
    }

    uint32_t removeClientId(uint32_t clientid) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for(auto it = m_map.begin(); it != m_map.end(); ++it) {
            if(it->first == clientid) {
                uint32_t serviceid = it->second;
                m_map.erase(it);
                return serviceid;
            }
        }
        return 0;
    }

    uint32_t removeServiceId(uint32_t serviceid) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for(auto it = m_map.begin(); it != m_map.end(); ++it) {
            if(it->second == serviceid) {
                uint32_t clientid = it->first;
                m_map.erase(it);
                return clientid;
            }
        }
        return 0;
    }

    bool empty() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_map.empty();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_map.clear();
    }

private:
    std::map<uint32_t, uint32_t> m_map;
    std::mutex m_mutex;

};

class TTSClientPrivateFirebolt : public TTSClientPrivateInterface,TextToSpeechServiceFirebolt::Client {

public:
    TTSClientPrivateFirebolt(TTSConnectionCallback *client, bool discardRtDispatching=false);
    ~TTSClientPrivateFirebolt();

     // TTS Global APIs
    TTS_Error enableTTS(bool enable) override;
    TTS_Error listVoices(std::string &language, std::vector<std::string> &voices) override;
    TTS_Error setTTSConfiguration(Configuration &config) override;
    TTS_Error getTTSConfiguration(Configuration &config) override;
    bool isTTSEnabled(bool forcefetch) override;
    bool isSessionActiveForApp(uint32_t) override { return true; }

    // Resource management APIs
    TTS_Error acquireResource(uint32_t) override { return TTS_OK; }
    TTS_Error claimResource(uint32_t) override { return TTS_OK; }
    TTS_Error releaseResource(uint32_t) override { return TTS_OK; }

    // Session management APIs
    // Restricted to single session now
    uint32_t /*sessionId*/ createSession(uint32_t sessionId, std::string appName, TTSSessionCallback *callback) override;
    TTS_Error destroySession(uint32_t sessionId) override;
    bool isActiveSession(uint32_t, bool forcefetch=false) override { (void)forcefetch; return true; }
    TTS_Error setPreemptiveSpeak(uint32_t, bool preemptive=true) override { (void)preemptive; return TTS_OK; }
    TTS_Error requestExtendedEvents(uint32_t, uint32_t) override { return TTS_OK; }

    // Speak APIs
    TTS_Error speak(uint32_t sessionId, SpeechData& data) override;
    TTS_Error pause(uint32_t sessionId, uint32_t speechId = 0) override;
    TTS_Error resume(uint32_t sessionId, uint32_t speechId = 0) override;
    TTS_Error abort(uint32_t sessionId, bool clearPending) override;
    bool isSpeaking(uint32_t sessionId) override;
    TTS_Error getSpeechState(uint32_t sessionId, uint32_t speechId, SpeechState &state) override;

    // TextToSpeechService::Client interfaces
    //void onActivation(); override;
    //void onDeactivation(); override;
    void onTTSStateChange(bool enabled) override;
    void onVoiceChange(std::string voice) override;
    void onSpeechStart(uint32_t speeechId) override;
    void onSpeechPause(uint32_t speeechId) override;
    void onSpeechResume(uint32_t speeechId) override;
    void onSpeechCancel(uint32_t speeechId) override;
    void onSpeechInterrupt(uint32_t speeechId) override;
    void onNetworkError(uint32_t speeechId) override;
    void onPlaybackError(uint32_t speeechId) override;
    void onSpeechComplete(uint32_t speeechId) override;
    
private:
    TTSClientPrivateFirebolt(TTSClientPrivateFirebolt&) = delete;

    bool m_ttsEnabled;
    TTSConnectionCallback *m_connectionCallback;
    TTSSessionCallback *m_sessionCallback;

    FireboltSpeechRequestMap m_requestedSpeeches;
    uint32_t m_lastSpeechId;
    uint32_t m_appId;
    bool m_firstQuery;
    std::string m_callsign;
};

}
