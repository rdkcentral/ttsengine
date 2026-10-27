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
#include <unistd.h>
#include <iomanip>
#include "TextToSpeechServiceNativeFirebolt.h"
#include "accessibility.h"
#include <mutex>
#include <thread>
#include <chrono>
#include "logger.h"
#include <condition_variable>

static std::mutex __mutex;

std::condition_variable cv;
std::mutex mtx;
namespace TTSFirebolt {

bool TextToSpeechServiceFirebolt::isConnected;

TextToSpeechServiceFirebolt* TextToSpeechServiceFirebolt::Instance() {
    // Allocating static object to heap; memory reclaimed at process exit
    __attribute__((used)) static TextToSpeechServiceFirebolt* instance = new TextToSpeechServiceFirebolt();
    return instance;
}

void TextToSpeechServiceFirebolt::initialize() {
    std::unique_lock<std::mutex> lock(m_mutex);
    if(connected())
        return;
    const char* firebolt_endpoint = std::getenv("FIREBOLT_ENDPOINT");
    if(firebolt_endpoint != nullptr) {
        std::string url = firebolt_endpoint;
        if(!createFireboltInstance(url)) {
            TTSLOG_ERROR("Failed to create FireboltInstance URL: [%s]", url.c_str());
            return;
        }
        std::unique_lock<std::mutex> lock(mtx);
	/*Wait Time is 500 millisecond*/
        if (cv.wait_for(lock, std::chrono::milliseconds(500), [this] { return isConnected; })) {
            m_connected = true;
            SubscribeVoiceGuidanceEvents();
            TTSLOG_INFO("Firebolt Core Intiailized URL: [%s]", url.c_str());
	    }
	    else {
	        TTSLOG_ERROR("Firebolt Core Intiailized URL: [%s] Failed(Timeout)", url.c_str());
	    }
    }
    else {
        TTSLOG_ERROR("No Firebolt endpoint; initialization failed");
    }
}

void TextToSpeechServiceFirebolt::deinitialize() {
    UnsubscribeVoiceGuidanceEvents();
    destroyFireboltInstance();
    isConnected = false;
    TTSLOG_INFO("Firebolt Core deinitialized");
}

bool TextToSpeechServiceFirebolt::createFireboltInstance(const std::string& url){
    Firebolt::Config cfg;
    cfg.wsUrl = url;
    cfg.waitTime_ms = 3000;
    cfg.log.level   = Firebolt::LogLevel::Info;
    
    isConnected = false;
    Firebolt::Error errorConnect = Firebolt::IFireboltAccessor::Instance().Connect(cfg, connectionChanged);
    if(errorConnect == Firebolt::Error::None)
        return true;
    TTSLOG_ERROR("Failed to create FireboltInstance ConnectError:\"%d\"", static_cast<int>(errorConnect));
    return false;
}

TextToSpeechServiceFirebolt::TextToSpeechServiceFirebolt():
    m_connected(false)
    {
}

void TextToSpeechServiceFirebolt::connectionChanged(const bool connected, const Firebolt::Error error){
    TTSLOG_INFO("Firebolt connection : %d Error : %d",connected,static_cast<int>(error));
    {
        std::lock_guard<std::mutex> lock(mtx);
        isConnected = connected;
    }
    cv.notify_one();
}

bool TextToSpeechServiceFirebolt::destroyFireboltInstance(){
    Firebolt::IFireboltAccessor::Instance().Disconnect();
    return true;
}

TextToSpeechServiceFirebolt::~TextToSpeechServiceFirebolt(){
    deinitialize();
}

void TextToSpeechServiceFirebolt::registerClient(Client* client){
    if(!client)
        return;

    std::unique_lock<std::mutex> lock(m_mutex);
    if(std::find(m_clients.begin(), m_clients.end(), client) == m_clients.end())
        m_clients.push_back(client);

}
void TextToSpeechServiceFirebolt::unregisterClient(Client* client){
    if(!client)
        return;

    std::unique_lock<std::mutex> lock(m_mutex);
    ClientList::iterator it = std::find(m_clients.begin(), m_clients.end(), client);
    if(it != m_clients.end())
        m_clients.erase(it);
}

bool TextToSpeechServiceFirebolt::connected(){
    return m_connected;
}

bool TextToSpeechServiceFirebolt::isActive(bool /*force*/){
    return connected();
}

void TextToSpeechServiceFirebolt::onUtteranceEvent( const Firebolt::SpeechSynthesis::UtteranceEvent& event)
{
    TTSLOG_INFO("Utterance event %d received: speechId=%d ", event.event, event.utteranceId);
    TextToSpeechServiceFirebolt::Instance()->dispatchEvent((EventType)event.event, event.utteranceId, std::nullopt, std::nullopt);
}

void TextToSpeechServiceFirebolt::onVGStateChanged( const Firebolt::Accessibility::VoiceGuidanceSettings& vgSettings)
{
    TextToSpeechServiceFirebolt::Instance()->dispatchEvent(EventType::stateChange, std::nullopt, vgSettings.enabled, std::nullopt);
}


void TextToSpeechServiceFirebolt::onVoicesChanged( const std::pmr::vector<Firebolt::SpeechSynthesis::Voice>& voices)
{
    if (!voices.empty()) {
        for (const Firebolt::SpeechSynthesis::Voice& voice : voices)
        {
            TTSLOG_INFO("Received VoiceChanged \"%s\"", voice.name);
        }
        const Firebolt::SpeechSynthesis::Voice& changedVoice = voices.front();
        TextToSpeechServiceFirebolt::Instance()->dispatchEvent(EventType::voiceChange, std::nullopt, std::nullopt, changedVoice.name);
    }
}

void TextToSpeechServiceFirebolt::SubscribeVoiceGuidanceEvents()
{
    if(!isActive()) {
       TTSLOG_ERROR("Firebolt is not active (or) channel is couldn't be opened");
       return;
    }
    auto id = Firebolt::IFireboltAccessor::Instance().SpeechSynthesisInterface().subscribeOnVoicesChanged(
        [this](const std::pmr::vector<Firebolt::SpeechSynthesis::Voice>& voices) {
                    this->onVoicesChanged(voices);
                });

    if(id)
    {
        onVoiceChangedSubId = *id;
        TTSLOG_INFO("Subscribe Event for Voice change Sucessfull");
    }
    else
    {
        TTSLOG_ERROR("Failed to Subscribe Event for Voice change");
    }

    id = Firebolt::IFireboltAccessor::Instance().SpeechSynthesisInterface().subscribeOnUtteranceEvent(
        [this](const Firebolt::SpeechSynthesis::UtteranceEvent& event) {
                    this->onUtteranceEvent(event);
                });
    
    if(id)
    {
        onUtteranceEventSubId = *id;
        TTSLOG_INFO("Subscribe Event for utterance Sucessfull");
    }
    else
    {
        TTSLOG_ERROR("Failed to Subscribe Event for utterance");
    }

    id = Firebolt::IFireboltAccessor::Instance().AccessibilityInterface().subscribeOnVoiceGuidanceSettingsChanged(
        [this](const Firebolt::Accessibility::VoiceGuidanceSettings& vgSettings) {
                    this->onVGStateChanged(vgSettings);
                });

    if(id)
    {
        onVGStateChangedSubId = *id;
        TTSLOG_INFO("Subscribe Event for Voice Guidance Settings Sucessfull");
    }
    else
    {
        TTSLOG_ERROR("Failed to Subscribe Event for Voice Guidance Settings");
    }
}

void TextToSpeechServiceFirebolt::UnsubscribeVoiceGuidanceEvents()
{
    if(!isActive()) {
       TTSLOG_ERROR("Firebolt is not active (or) channel is couldn't be opened");
       return;
    }
    if (onUtteranceEventSubId)
    {
        Firebolt::IFireboltAccessor::Instance().SpeechSynthesisInterface().unsubscribe(onUtteranceEventSubId);
        TTSLOG_INFO("Unsubscribed utteranceEvent successfully");
        onUtteranceEventSubId = 0;
    }
    if (onVoiceChangedSubId)
    {
        Firebolt::IFireboltAccessor::Instance().SpeechSynthesisInterface().unsubscribe(onVoiceChangedSubId);
        TTSLOG_INFO("Unsubscribed Voice change event successfully");
        onVoiceChangedSubId = 0;
    }
    if (onVGStateChangedSubId)
    {
        Firebolt::IFireboltAccessor::Instance().AccessibilityInterface().unsubscribe(onVGStateChangedSubId);
        TTSLOG_INFO("Unsubscribed Voice Guidance Settings event successfully");
        onVGStateChangedSubId = 0;
    }
}

void TextToSpeechServiceFirebolt::dispatchEvent(EventType event, const std::optional<int32_t>& speechId,const std::optional<bool>& ttsstatus,const std::optional<std::string>& voices)
{
    int speechid = 0;
    bool enabled = false;
    std::string voice;

    if(event == stateChange) {
        enabled = ttsstatus.value();
        TTSLOG_INFO("%s(StateChange), state=%s", __FUNCTION__, enabled ? "enabled" : "disabled");
    } else if (event == voiceChange) {
        voice = voices.value();
        TTSLOG_INFO("%s(VoiceChange), voice=%s", __FUNCTION__, voice.c_str());
    } else {
        speechid = speechId.value();
        TTSLOG_INFO("%s(SpeechEvent-%d), servicespeecid=%d", __FUNCTION__, (int)event, speechid);
    }
    if(connected()) {
        std::unique_lock<std::mutex> lock(m_mutex);
        for(ClientList::iterator it = m_clients.begin(); it != m_clients.end(); ++it) {
            switch(event) {
                case synthesisStarting: break;
                case playbackStarting: ((TextToSpeechServiceFirebolt::Client*)(*it))->onSpeechStart(speechid);break;
                case paused: ((TextToSpeechServiceFirebolt::Client*)(*it))->onSpeechPause(speechid); break;
                case resumed: ((TextToSpeechServiceFirebolt::Client*)(*it))->onSpeechResume(speechid); break;
                case completed: ((TextToSpeechServiceFirebolt::Client*)(*it))->onSpeechComplete(speechid); break;
                case interrupted: ((TextToSpeechServiceFirebolt::Client*)(*it))->onSpeechInterrupt(speechid); break;
                case networkFailed: ((TextToSpeechServiceFirebolt::Client*)(*it))->onNetworkError(speechid); break;
                case synthesisFailed: break;
                case playbackFailed: ((TextToSpeechServiceFirebolt::Client*)(*it))->onPlaybackError(speechid); break;
                case stateChange: ((TextToSpeechServiceFirebolt::Client*)(*it))->onTTSStateChange(enabled); break;
                case voiceChange: ((TextToSpeechServiceFirebolt::Client*)(*it))->onVoiceChange(voice); break;
            }
        }
    }
}

bool TextToSpeechServiceFirebolt::isEnabled(bool &enable)
{
    if(!isActive()) {
       TTSLOG_ERROR("Firebolt is not active (or) channel is couldn't be opened");
       return false;
    }
    auto result = Firebolt::IFireboltAccessor::Instance().AccessibilityInterface().voiceGuidanceSettings();
        if (!result) {
            TTSLOG_ERROR("Failed to get VG settings");
            return false;
        }
    const auto& vgSettings = *result;
    TTSLOG_INFO("VG settings retrieved successfully, enabled=%d", vgSettings.enabled);
    enable = vgSettings.enabled;

    return true;
}

bool TextToSpeechServiceFirebolt::listVoices(std::string &, std::vector<std::string> &voices) {
    auto result = Firebolt::IFireboltAccessor::Instance().SpeechSynthesisInterface().voices();
    if (!result) {
        TTSLOG_ERROR("Failed to get voice list");
        return false;
    }
    const auto& voiceList = *result;
    if (voiceList.empty()) {
        TTSLOG_ERROR("voice list is empty");
        return false;
    }
    for (const auto &voice : voiceList) {
        voices.push_back(voice.name);
    }
    return true;
}

// Firebolt Speak API is not using any speechId parameter instead, it is returning speechResponse structure.
bool TextToSpeechServiceFirebolt::speak(std::string &, uint32_t &speechid, std::string text, std::optional<double> rate, std::optional<double> vol, std::optional<double> pitch, \
    std::optional<std::string> lang, std::optional<std::string> voice){
    if(!isActive()) {
       TTSLOG_ERROR("Firebolt is not active (or) channel is couldn't be opened");
       return false;
    }
    auto result = Firebolt::IFireboltAccessor::Instance().SpeechSynthesisInterface().speak(text, lang, voice, vol, rate, pitch, std::nullopt);
    if (!result) {
    TTSLOG_ERROR("Firebolt speak request failed");
    return false;
    }

    speechid = *result;
    TTSLOG_INFO("firebolt speak returned %d\n", speechid);
    return true;
}

bool TextToSpeechServiceFirebolt::pause(uint32_t &speechid) {
    if(!isActive()) {
       TTSLOG_ERROR("Firebolt is not active (or) channel is couldn't be opened");
       return false;
    }
    Firebolt::IFireboltAccessor::Instance().SpeechSynthesisInterface().pause(speechid);
    TTSLOG_INFO("firebolt paused speech id %d\n", speechid);
    return true;
}

bool TextToSpeechServiceFirebolt::resume(uint32_t &speechid) {
    if(!isActive()) {
       TTSLOG_ERROR("Firebolt is not active (or) channel is couldn't be opened");
       return false;
    }
    Firebolt::IFireboltAccessor::Instance().SpeechSynthesisInterface().resume(speechid);
    TTSLOG_INFO("firebolt resumed speech id %d\n", speechid);
    return true;
}

bool TextToSpeechServiceFirebolt::cancel(uint32_t &speechid) {
    if(!isActive()) {
       TTSLOG_ERROR("Firebolt is not active (or) channel is couldn't be opened");
       return false;
    }
    Firebolt::IFireboltAccessor::Instance().SpeechSynthesisInterface().cancel(speechid);
    TTSLOG_INFO("firebolt canceled speech id %d\n", speechid);
    return true;
}

}