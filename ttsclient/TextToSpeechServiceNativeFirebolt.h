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

#pragma once

#include <iostream>
#include "firebolt.h"
#include "speechsynthesis.h"
#include <list>
#include <mutex>
#include <optional>
#include <cassert>

namespace TTSFirebolt{

class TextToSpeechServiceFirebolt{

    enum EventType
    {
        synthesisStarting,
        playbackStarting,
        paused,
        resumed,
        completed,
        interrupted,
        networkFailed,
        synthesisFailed,
        playbackFailed,
        stateChange,
        voiceChange
    };

public:
    struct Client {
        virtual void onTTSStateChange(bool /*enabled*/) {};
        virtual void onVoiceChange(std::string /*voice*/) {};
        virtual void onSpeechStart(uint32_t /*speeechId*/) {};
        virtual void onSpeechPause(uint32_t /*speeechId*/) {};
        virtual void onSpeechResume(uint32_t /*speeechId*/) {};
        virtual void onSpeechCancel(uint32_t /*speeechId*/) {};
        virtual void onSpeechInterrupt(uint32_t /*speeechId*/) {};
        virtual void onNetworkError(uint32_t /*speeechId*/) {};
        virtual void onPlaybackError(uint32_t /*speeechId*/) {};
        virtual void onSpeechComplete(uint32_t /*speeechId*/) {};
    };
    
    using ClientList = std::list<Client*>;
    void initialize();
    void deinitialize();

public:

    static TextToSpeechServiceFirebolt* Instance();    
    //TextToSpeechServiceFirebolt(const TextToSpeechServiceFirebolt&) = delete;
    //TextToSpeechServiceFirebolt& operator=(const TextToSpeechServiceFirebolt&) = delete;    

    // Firebolt APIs
    bool isActive(bool force=false);
    bool isEnabled(bool &enable);
    void registerClient(Client* client);
    void unregisterClient(Client* client);
    bool listVoices(std::string &language, std::vector<std::string> &voices);
    bool speak(std::string &callsign, uint32_t &speechid, std::string text, std::optional<double> rate = std::nullopt, std::optional<double> volume = std::nullopt, std::optional<double> pitch = std::nullopt, \
        std::optional<std::string> lang = std::nullopt, std::optional<std::string> voice = std::nullopt);
    bool pause(uint32_t &speechid);
    bool resume(uint32_t &speechid);
    bool cancel(uint32_t &speechid);

private:
    TextToSpeechServiceFirebolt();
    ~TextToSpeechServiceFirebolt();

    //Firebolt APIs
    bool createFireboltInstance(const std::string& url);
    bool destroyFireboltInstance();
    void SubscribeVoiceGuidanceEvents();
    void UnsubscribeVoiceGuidanceEvents();
    bool waitOnConnectionReady();
    bool connected();
    Firebolt::SubscriptionId onVoiceChangedSubId{ 0 };
    Firebolt::SubscriptionId onUtteranceEventSubId{ 0 };
    Firebolt::SubscriptionId onVGStateChangedSubId{ 0 };
    void onVoicesChanged(const std::pmr::vector<Firebolt::SpeechSynthesis::Voice>& voices);
    void onUtteranceEvent( const Firebolt::SpeechSynthesis::UtteranceEvent& event);
    void onVGStateChanged(const Firebolt::Accessibility::VoiceGuidanceSettings& vgSettings);

    void dispatchEvent(EventType event, const std::optional<int32_t>& speechid,const std::optional<bool>& ttsstatus,const std::optional<std::string>& voice);

    bool m_connected;
        
    ClientList m_clients;
    std::mutex m_mutex;
    static void connectionChanged(const bool, const Firebolt::Error);
    static bool isConnected;
};

}
