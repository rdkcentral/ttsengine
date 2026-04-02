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
#include <firebolt/firebolt.h>
#include <firebolt/texttospeech.h>
#include <firebolt/config.h>
#include <list>
#include <map>
#include <mutex>
#include <optional>
#include <cassert>

namespace TTSFirebolt{

class TextToSpeechServiceFirebolt{

public:
    enum EventType {
        StateChange,
        VoiceChange,
        SpeechStart,
        SpeechPause,
        SpeechResume,
        SpeechCancel,
        SpeechInterrupt,
        NetworkError,
        PlaybackError,
        SpeechComplete
    };

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

    // Firebolt APIs
    bool isActive(bool force=false);

    void registerClient(Client* client);
    void unregisterClient(Client* client);

    bool listVoices(std::string &language, std::vector<std::string> &voices);
    bool isSpeaking(uint32_t &speechid,bool &isspeaking);
    bool getSpeechState(uint32_t &speechid, Firebolt::TextToSpeech::SpeechState &state);
    bool isEnabled(bool &enable) {
        enable = isActive();
        return true;
    }
    bool speak(std::string &text,uint32_t &speechid);
    bool pause(uint32_t &speechid);
    bool resume(uint32_t &speechid);
    bool cancel(uint32_t &speechid);

private:
    TextToSpeechServiceFirebolt();
    ~TextToSpeechServiceFirebolt();

    //Firebolt APIs
    bool createFireboltInstance(const std::string& url);
    bool destroyFireboltInstance();
    bool subscribeEvents();
    void unSubscribeEvents();
    bool initialized();

    void dispatchEvent(EventType event, const std::optional<int32_t>& speechid,const std::optional<bool>& ttsstatus,const std::optional<std::string>& voice);

    bool m_initialized;

    std::map<std::string, Firebolt::SubscriptionId> m_subscriptions;

    ClientList m_clients;
    std::mutex m_mutex;
    static void connectionChanged(const bool, const Firebolt::Error);
    static bool isConnected;

    static void onNetworkErrorCb(const Firebolt::TextToSpeech::SpeechIdEvent& ev);
    static void onPlaybackErrorCb(const Firebolt::TextToSpeech::SpeechIdEvent& ev);
    static void onSpeechStartCb(const Firebolt::TextToSpeech::SpeechIdEvent& ev);
    static void onSpeechCompleteCb(const Firebolt::TextToSpeech::SpeechIdEvent& ev);
    static void onSpeechInterruptedCb(const Firebolt::TextToSpeech::SpeechIdEvent& ev);
    static void onSpeechPauseCb(const Firebolt::TextToSpeech::SpeechIdEvent& ev);
    static void onSpeechResumeCb(const Firebolt::TextToSpeech::SpeechIdEvent& ev);
};

}
