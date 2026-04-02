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
#include "TextToSpeechServiceFirebolt.h"
#include <mutex>
#include <thread>
#include <chrono>
#include "logger.h"
#include <condition_variable>

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
    if(initialized())
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
        if (cv.wait_for(lock, std::chrono::milliseconds(500), [] { return isConnected; })) {
            m_initialized = true;
            subscribeEvents();
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
    unSubscribeEvents();
    destroyFireboltInstance();
    isConnected = false;
    m_initialized = false;
    TTSLOG_INFO("Firebolt Core deinitialized");
}

bool TextToSpeechServiceFirebolt::createFireboltInstance(const std::string& url){
    Firebolt::Config config;
    config.wsUrl = url;
    config.waitTime_ms = 3000;
    config.log.level = Firebolt::LogLevel::Debug;
    isConnected = false;
    Firebolt::Error errorConnect = Firebolt::IFireboltAccessor::Instance().Connect(config, connectionChanged);
    if(errorConnect == Firebolt::Error::None)
        return true;
    TTSLOG_ERROR("Failed to create FireboltInstance ConnectError:\"%d\"", static_cast<int>(errorConnect));
    return false;
}

TextToSpeechServiceFirebolt::TextToSpeechServiceFirebolt():
    m_initialized(false)
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

bool TextToSpeechServiceFirebolt::initialized(){
    return m_initialized;
}

bool TextToSpeechServiceFirebolt::isActive(bool /*force*/){
    return initialized();
}

/* ### Firebolt Static Event Callbacks ### */
void TextToSpeechServiceFirebolt::onNetworkErrorCb(const Firebolt::TextToSpeech::SpeechIdEvent& ev) {
    TTSLOG_INFO("Received NetworkError for speechId \"%u\"", ev.speechId);
    Instance()->dispatchEvent(EventType::NetworkError, ev.speechId, std::nullopt, std::nullopt);
}

void TextToSpeechServiceFirebolt::onPlaybackErrorCb(const Firebolt::TextToSpeech::SpeechIdEvent& ev) {
    TTSLOG_INFO("Received PlaybackError for speechId \"%u\"", ev.speechId);
    Instance()->dispatchEvent(EventType::PlaybackError, ev.speechId, std::nullopt, std::nullopt);
}

void TextToSpeechServiceFirebolt::onSpeechStartCb(const Firebolt::TextToSpeech::SpeechIdEvent& ev) {
    TTSLOG_INFO("Received SpeechStart for speechId \"%u\"", ev.speechId);
    Instance()->dispatchEvent(EventType::SpeechStart, ev.speechId, std::nullopt, std::nullopt);
}

void TextToSpeechServiceFirebolt::onSpeechCompleteCb(const Firebolt::TextToSpeech::SpeechIdEvent& ev) {
    TTSLOG_INFO("Received SpeechComplete for speechId \"%u\"", ev.speechId);
    Instance()->dispatchEvent(EventType::SpeechComplete, ev.speechId, std::nullopt, std::nullopt);
}

void TextToSpeechServiceFirebolt::onSpeechInterruptedCb(const Firebolt::TextToSpeech::SpeechIdEvent& ev) {
    TTSLOG_INFO("Received SpeechInterrupted for speechId \"%u\"", ev.speechId);
    Instance()->dispatchEvent(EventType::SpeechInterrupt, ev.speechId, std::nullopt, std::nullopt);
}

void TextToSpeechServiceFirebolt::onSpeechPauseCb(const Firebolt::TextToSpeech::SpeechIdEvent& ev) {
    TTSLOG_INFO("Received SpeechPause for speechId \"%u\"", ev.speechId);
    Instance()->dispatchEvent(EventType::SpeechPause, ev.speechId, std::nullopt, std::nullopt);
}

void TextToSpeechServiceFirebolt::onSpeechResumeCb(const Firebolt::TextToSpeech::SpeechIdEvent& ev) {
    TTSLOG_INFO("Received SpeechResume for speechId \"%u\"", ev.speechId);
    Instance()->dispatchEvent(EventType::SpeechResume, ev.speechId, std::nullopt, std::nullopt);
}

/* ### Firebolt Event Subscribe & Unsubscribe API ### */
bool TextToSpeechServiceFirebolt::subscribeEvents() {
    auto& tts = Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface();

    auto resNetworkError = tts.subscribeOnNetworkError(onNetworkErrorCb);
    if(resNetworkError.has_value()) {
        m_subscriptions["networkerror"] = resNetworkError.value();
        TTSLOG_INFO("Subscribe networkerror Successful");
    }
    else {
        TTSLOG_ERROR("Failed to subscribe networkerror: %d", static_cast<int>(resNetworkError.error()));
    }

    auto resPlaybackError = tts.subscribeOnPlaybackError(onPlaybackErrorCb);
    if(resPlaybackError.has_value()) {
        m_subscriptions["playbackerror"] = resPlaybackError.value();
        TTSLOG_INFO("Subscribe playbackerror Successful");
    }
    else {
        TTSLOG_ERROR("Failed to subscribe playbackerror: %d", static_cast<int>(resPlaybackError.error()));
    }

    auto resSpeechStart = tts.subscribeOnSpeechStart(onSpeechStartCb);
    if(resSpeechStart.has_value()) {
        m_subscriptions["speechstart"] = resSpeechStart.value();
        TTSLOG_INFO("Subscribe speechstart Successful");
    }
    else {
        TTSLOG_ERROR("Failed to subscribe speechstart: %d", static_cast<int>(resSpeechStart.error()));
    }

    auto resSpeechComplete = tts.subscribeOnSpeechComplete(onSpeechCompleteCb);
    if(resSpeechComplete.has_value()) {
        m_subscriptions["speechcomplete"] = resSpeechComplete.value();
        TTSLOG_INFO("Subscribe speechcomplete Successful");
    }
    else {
        TTSLOG_ERROR("Failed to subscribe speechcomplete: %d", static_cast<int>(resSpeechComplete.error()));
    }

    auto resSpeechInterrupted = tts.subscribeOnSpeechInterrupted(onSpeechInterruptedCb);
    if(resSpeechInterrupted.has_value()) {
        m_subscriptions["speechinterrupted"] = resSpeechInterrupted.value();
        TTSLOG_INFO("Subscribe speechinterrupted Successful");
    }
    else {
        TTSLOG_ERROR("Failed to subscribe speechinterrupted: %d", static_cast<int>(resSpeechInterrupted.error()));
    }

    auto resSpeechPause = tts.subscribeOnSpeechPause(onSpeechPauseCb);
    if(resSpeechPause.has_value()) {
        m_subscriptions["speechpause"] = resSpeechPause.value();
        TTSLOG_INFO("Subscribe speechpause Successful");
    }
    else {
        TTSLOG_ERROR("Failed to subscribe speechpause: %d", static_cast<int>(resSpeechPause.error()));
    }

    auto resSpeechResume = tts.subscribeOnSpeechResume(onSpeechResumeCb);
    if(resSpeechResume.has_value()) {
        m_subscriptions["speechresume"] = resSpeechResume.value();
        TTSLOG_INFO("Subscribe speechresume Successful");
    }
    else {
        TTSLOG_ERROR("Failed to subscribe speechresume: %d", static_cast<int>(resSpeechResume.error()));
    }

    return true;
}

void TextToSpeechServiceFirebolt::unSubscribeEvents() {
    auto& tts = Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface();
    for(auto& [name, id] : m_subscriptions) {
        auto res = tts.unsubscribe(id);
        if(res) {
            TTSLOG_INFO("Unsubscribe \"%s\" Successful", name.c_str());
        }
        else {
            TTSLOG_ERROR("Failed to unsubscribe \"%s\": %d", name.c_str(), static_cast<int>(res.error()));
        }
    }
    m_subscriptions.clear();
}

void TextToSpeechServiceFirebolt::dispatchEvent(EventType event, const std::optional<int32_t>& speechId,const std::optional<bool>& ttsstatus,const std::optional<std::string>& voices)
{
    int speechid = 0;
    bool enabled = false;
    std::string voice;
    bool dispatch = true;

    if(event == StateChange) {
        enabled = ttsstatus.value();
        TTSLOG_INFO("%s(StateChange), state=%s", __FUNCTION__, enabled ? "enabled" : "disabled");
    } else if (event == VoiceChange) {
        voice = voices.value();
        TTSLOG_INFO("%s(VoiceChange), voice=%s", __FUNCTION__, voice.c_str());
    } else {
        speechid = speechId.value();
        TTSLOG_INFO("%s(SpeechEvent-%d), servicespeecid=%d", __FUNCTION__, (int)event, speechid);
    }
	    if(dispatch && initialized()) {
        std::unique_lock<std::mutex> lock(m_mutex);
        for(ClientList::iterator it = m_clients.begin(); it != m_clients.end(); ++it) {
            switch(event) {
                case StateChange: ((TextToSpeechServiceFirebolt::Client*)(*it))->onTTSStateChange(enabled); break;
                case VoiceChange: ((TextToSpeechServiceFirebolt::Client*)(*it))->onVoiceChange(voice); break;
                case SpeechStart: ((TextToSpeechServiceFirebolt::Client*)(*it))->onSpeechStart(speechid);break;
                case SpeechPause: ((TextToSpeechServiceFirebolt::Client*)(*it))->onSpeechPause(speechid); break;
                case SpeechResume: ((TextToSpeechServiceFirebolt::Client*)(*it))->onSpeechResume(speechid); break;
                case SpeechCancel: ((TextToSpeechServiceFirebolt::Client*)(*it))->onSpeechCancel(speechid); break;
                case SpeechInterrupt: ((TextToSpeechServiceFirebolt::Client*)(*it))->onSpeechInterrupt(speechid); break;
                case NetworkError: ((TextToSpeechServiceFirebolt::Client*)(*it))->onNetworkError(speechid); break;
                case PlaybackError: ((TextToSpeechServiceFirebolt::Client*)(*it))->onPlaybackError(speechid); break;
                case SpeechComplete: ((TextToSpeechServiceFirebolt::Client*)(*it))->onSpeechComplete(speechid); break;
            }
        }
    }
}

bool TextToSpeechServiceFirebolt::isSpeaking(uint32_t &speechid,bool &isspeaking)
{
    if(!isActive()) {
       TTSLOG_ERROR("Firebolt is not active (or) channel is couldn't be opened");
       return false;
    }
    auto result = Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().getSpeechState(speechid);
    if (result.has_value()) {
        isspeaking = (result.value().speechState == Firebolt::TextToSpeech::SpeechState::IN_PROGRESS);
        return true;
    }
    else {
        TTSLOG_ERROR("isSpeaking: Firebolt Error: \"%d\"", static_cast<int>(result.error()));
        return false;
    }
}

bool TextToSpeechServiceFirebolt::listVoices(std::string &language, std::vector<std::string> &voices) {
    if(!isActive()) {
       TTSLOG_ERROR("Firebolt is not active (or) channel is couldn't be opened");
       return false;
    }
    auto result = Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().listVoices(language);
    if(result.has_value()) {
        voices = result.value().voices;
        return true;
    }
    else {
        TTSLOG_ERROR("listVoices: Firebolt Error: \"%d\"", static_cast<int>(result.error()));
        return false;
    }
}

bool TextToSpeechServiceFirebolt::speak(std::string &text,uint32_t &speechid){
    if(!isActive()) {
       TTSLOG_ERROR("Firebolt is not active (or) channel is couldn't be opened");
       return false;
    }
    auto result = Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().speak(text);
    if (result.has_value() && result.value().success) {
        speechid = result.value().speechId;
        return true;
    }
    else {
        TTSLOG_ERROR("speak: Firebolt Error: \"%d\"", static_cast<int>(result.error()));
        return false;
    }
}

bool TextToSpeechServiceFirebolt::pause(uint32_t &speechid) {
    if(!isActive()) {
       TTSLOG_ERROR("Firebolt is not active (or) channel is couldn't be opened");
       return false;
    }
    auto result = Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().pause(speechid);
    if (result.has_value() && result.value().success) {
        return true;
    }
    else {
        TTSLOG_ERROR("pause: Firebolt Error: \"%d\"", static_cast<int>(result.error()));
        return false;
    }
}

bool TextToSpeechServiceFirebolt::resume(uint32_t &speechid) {
    if(!isActive()) {
       TTSLOG_ERROR("Firebolt is not active (or) channel is couldn't be opened");
       return false;
    }
    auto result = Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().resume(speechid);
    if (result.has_value() && result.value().success) {
        return true;
    }
    else {
        TTSLOG_ERROR("resume: Firebolt Error: \"%d\"", static_cast<int>(result.error()));
        return false;
    }
}

bool TextToSpeechServiceFirebolt::cancel(uint32_t &speechid) {
    if(!isActive()) {
       TTSLOG_ERROR("Firebolt is not active (or) channel is couldn't be opened");
       return false;
    }
    auto result = Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().cancel(speechid);
    if (result.has_value() && result.value().success) {
        return true;
    }
    else {
        TTSLOG_ERROR("cancel: Firebolt Error: \"%d\"", static_cast<int>(result.error()));
        return false;
    }
}

bool TextToSpeechServiceFirebolt::getSpeechState(uint32_t &speechid, Firebolt::TextToSpeech::SpeechState &state) {
    auto result = Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().getSpeechState(speechid);
    if (result.has_value()) {
        state = result.value().speechState;
        return true;
    }
    else {
        TTSLOG_ERROR("getSpeechState: Firebolt Error: \"%d\"", static_cast<int>(result.error()));
        return false;
    }
}

} // namespace TTSFirebolt
