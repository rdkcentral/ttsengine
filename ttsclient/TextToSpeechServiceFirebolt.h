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
#include "texttospeech.h"
#include <list>
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
    // Firebolt APIs

    // Notification for events
    class OnNetworkerrorNotification : public Firebolt::TextToSpeech::ITextToSpeech::IOnNetworkerrorNotification {
    public:
        void onNetworkerror( const Firebolt::TextToSpeech::SpeechIdEvent& ) override;
    };

    class OnPlaybackErrorNotification : public Firebolt::TextToSpeech::ITextToSpeech::IOnPlaybackErrorNotification {
    public:
        void onPlaybackError( const Firebolt::TextToSpeech::SpeechIdEvent& ) override;
    };

    /* onSpeechcomplete - Triggered when the speech completes. */
    class OnSpeechcompleteNotification : public Firebolt::TextToSpeech::ITextToSpeech::IOnSpeechcompleteNotification {
    public:    
        void onSpeechcomplete( const Firebolt::TextToSpeech::SpeechIdEvent& ) override;
    };

    /* onSpeechinterrupted - Triggered when the current speech is interrupted either by a next speech request, by calling cancel or by disabling TTS, when speech is in progress. */
    class OnSpeechinterruptedNotification : public Firebolt::TextToSpeech::ITextToSpeech::IOnSpeechinterruptedNotification {
    public:
        void onSpeechinterrupted( const Firebolt::TextToSpeech::SpeechIdEvent& ) override;
    };

    /* onSpeechpause - Triggered when the ongoing speech pauses. */
    class OnSpeechpauseNotification : public Firebolt::TextToSpeech::ITextToSpeech::IOnSpeechpauseNotification {
    public:
        void onSpeechpause( const Firebolt::TextToSpeech::SpeechIdEvent& ) override;
    };

    /* onSpeechresume - Triggered when any paused speech resumes. */
    class OnSpeechresumeNotification : public Firebolt::TextToSpeech::ITextToSpeech::IOnSpeechresumeNotification {
    public:
        void onSpeechresume( const Firebolt::TextToSpeech::SpeechIdEvent& ) override;
    };

    /* onSpeechstart - Triggered when the speech start. */
    class OnSpeechstartNotification : public Firebolt::TextToSpeech::ITextToSpeech::IOnSpeechstartNotification {
    public:
        void onSpeechstart( const Firebolt::TextToSpeech::SpeechIdEvent& ) override;
    };

    /* onTtsstatechanged - Triggered when TTS is enabled or disabled */
    class OnTtsstatechangedNotification : public Firebolt::TextToSpeech::ITextToSpeech::IOnTTSstatechangedNotification {
    public:
        void onTTSstatechanged( const Firebolt::TextToSpeech::TTSState& ) override;
    };

    /* onVoicechanged - Triggered when the configured voice changes. */
    class OnVoicechangedNotification : public Firebolt::TextToSpeech::ITextToSpeech::IOnVoicechangedNotification {
    public:
        void onVoicechanged( const Firebolt::TextToSpeech::TTSVoice& ) override;
    };

    void initialize();
    void deinitialize();

public:

    static TextToSpeechServiceFirebolt* Instance();    
    //TextToSpeechServiceFirebolt(const TextToSpeechServiceFirebolt&) = delete;
    //TextToSpeechServiceFirebolt& operator=(const TextToSpeechServiceFirebolt&) = delete;    
    virtual ~TextToSpeechServiceFirebolt();
    // Firebolt APIs
    bool isActive(bool force=false);
  
    void registerClient(Client* client);
    void unregisterClient(Client* client);

    bool setConfiguration(Firebolt::TextToSpeech::TTSConfiguration &ttsconfig);
    bool getConfiguration(Firebolt::TextToSpeech::TTSConfiguration &ttsconfig);
    bool listVoices(std::string &language, std::vector<std::string> &voices);
    bool isSpeaking(uint32_t &speechid,bool &isspeaking);
    bool getSpeechState(uint32_t &speechid,Firebolt::TextToSpeech::SpeechStateResponse &state);
    bool isEnabled(bool &enable);
    //bool enableTTS(bool &enable);
    bool speak(std::string &callsign,std::string &text,uint32_t &speechid);
    bool pause(uint32_t &speechid);
    bool resume(uint32_t &speechid);
    bool cancel(uint32_t &speechid);

    void SubscribeVoiceGuidanceSettings(const std::string&);
    void UnsubscribeVoiceGuidanceSettings( const std::string&);

    


    
private:
    TextToSpeechServiceFirebolt();

    //Firebolt APIs
    bool createFireboltInstance(const std::string& url);
    bool destroyFireboltInstance();
    bool subscribeEvents();
    bool unSubscribeEvents();
    bool waitOnConnectionReady();
    bool initialized();

    friend class OnTtsstatechangedNotification;
    friend class OnVoicechangedNotification;
    friend class onNetworkerrorNotification;
    friend class onPlaybackErrorNotification;
    friend class onSpeechcompleteNotification;
    friend class onSpeechinterruptedNotification;
    friend class onSpeechpauseNotification;
    friend class onSpeechresumeNotification;
    friend class onSpeechstartNotification;
    friend class onWillspeakNotification;

    void dispatchEvent(EventType event, const std::optional<int32_t>& speechid,const std::optional<bool>& ttsstatus,const std::optional<std::string>& voice);

    bool m_initialized;
        
    ClientList m_clients;
    std::mutex m_mutex;
    static void connectionChanged(const bool, const Firebolt::Error);
    static bool isConnected;
    static OnNetworkerrorNotification onNetworkerrorNotification;
    static OnPlaybackErrorNotification onPlaybackErrorNotification;
    static OnSpeechcompleteNotification onSpeechcompleteNotification;
    static OnSpeechinterruptedNotification onSpeechinterruptedNotification;
    static OnSpeechpauseNotification onSpeechpauseNotification;
    static OnSpeechresumeNotification onSpeechresumeNotification;
    static OnSpeechstartNotification onSpeechstartNotification;
    static OnTtsstatechangedNotification onTtsstatechangedNotification;
    static OnVoicechangedNotification onVoicechangedNotification;
};

}
