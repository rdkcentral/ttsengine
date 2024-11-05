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

static std::mutex __mutex;

std::condition_variable cv;
std::mutex mtx;
namespace TTSFirebolt {

bool TextToSpeechServiceFirebolt::isConnected;

TextToSpeechServiceFirebolt::OnNetworkerrorNotification TextToSpeechServiceFirebolt::onNetworkerrorNotification;
TextToSpeechServiceFirebolt::OnPlaybackErrorNotification TextToSpeechServiceFirebolt::onPlaybackErrorNotification;
TextToSpeechServiceFirebolt::OnSpeechcompleteNotification TextToSpeechServiceFirebolt::onSpeechcompleteNotification;
TextToSpeechServiceFirebolt::OnSpeechinterruptedNotification TextToSpeechServiceFirebolt::onSpeechinterruptedNotification;
TextToSpeechServiceFirebolt::OnSpeechpauseNotification TextToSpeechServiceFirebolt::onSpeechpauseNotification;
TextToSpeechServiceFirebolt::OnSpeechresumeNotification TextToSpeechServiceFirebolt::onSpeechresumeNotification;
TextToSpeechServiceFirebolt::OnSpeechstartNotification TextToSpeechServiceFirebolt::onSpeechstartNotification;
TextToSpeechServiceFirebolt::OnTtsstatechangedNotification TextToSpeechServiceFirebolt::onTtsstatechangedNotification;
TextToSpeechServiceFirebolt::OnVoicechangedNotification TextToSpeechServiceFirebolt::onVoicechangedNotification;

TextToSpeechServiceFirebolt* TextToSpeechServiceFirebolt::Instance() {
    static TextToSpeechServiceFirebolt instance;
    return &instance;
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
	    /*Wait Time is 200 millisecond*/
        if (cv.wait_for(lock, std::chrono::milliseconds(200), [this] { return isConnected; })) {
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
    const std::string config = "{\
            \"waitTime\": 3000,\
            \"logLevel\": \"Info\",\
            \"workerPool\":{\
            \"queueSize\": 8,\
            \"threadCount\": 3\
            },\
            \"wsUrl\": " +  url + "}";
    isConnected = false;
    Firebolt::Error errorInitialize = Firebolt::IFireboltAccessor::Instance().Initialize(config);
    Firebolt::Error errorConnect = Firebolt::IFireboltAccessor::Instance().Connect(connectionChanged);
    if(errorInitialize == Firebolt::Error::None && errorConnect == Firebolt::Error::None)
        return true;
    TTSLOG_ERROR("Failed to create FireboltInstance InitialzeError:\"%d\" ConnectError:\"%d\"", static_cast<int>(errorInitialize),static_cast<int>(errorConnect));
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
    Firebolt::IFireboltAccessor::Instance().Deinitialize();
    Firebolt::IFireboltAccessor::Instance().Dispose();
    return true;
}

TextToSpeechServiceFirebolt::~TextToSpeechServiceFirebolt(){

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

void TextToSpeechServiceFirebolt::SubscribeVoiceGuidanceSettings(const std::string& moduleName)
{
    Firebolt::Error error = Firebolt::Error::None;
    if(!isActive()) {
       TTSLOG_ERROR("Firebolt is not active (or) channel is couldn't be opened");
       return;
    }
    if(moduleName == "networkerror"){
        Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().subscribe(onNetworkerrorNotification, &error);
    }
    else if(moduleName == "playbackerror"){
        Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().subscribe(onPlaybackErrorNotification, &error);
    }
    else if(moduleName == "speechstart"){
        Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().subscribe(onSpeechstartNotification, &error);
    }
    else if(moduleName == "speechcomplete"){
        Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().subscribe(onSpeechcompleteNotification, &error);
    }
    else if(moduleName == "speechinterupped"){
        Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().subscribe(onSpeechinterruptedNotification, &error);
    }
    else if(moduleName == "speechpause"){
        Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().subscribe(onSpeechpauseNotification, &error);
    }
    else if(moduleName == "speechresume"){
        Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().subscribe(onSpeechresumeNotification, &error);
    }
    else if(moduleName == "ttsstatechange"){
        Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().subscribe(onTtsstatechangedNotification, &error);
    }
    else{
         Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().subscribe(onVoicechangedNotification, &error);
    }
    if (error == Firebolt::Error::None) {
        TTSLOG_INFO("Subscribe Event \"%s\" Sucessfull",moduleName.c_str());
    } else {
        TTSLOG_ERROR("Failed to Subscribe Event: \"%s\" Error: \"%d\" ",moduleName.c_str(),static_cast<int>(error));
    }
}

void TextToSpeechServiceFirebolt::UnsubscribeVoiceGuidanceSettings(const std::string& moduleName)
{
    Firebolt::Error error = Firebolt::Error::None;
    if(!isActive()) {
       TTSLOG_ERROR("Firebolt is not active (or) channel is couldn't be opened");
       return;
    }
    if(moduleName == "networkerror"){
        Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().unsubscribe(onNetworkerrorNotification, &error);   
    }
    else if(moduleName == "playbackerror"){
        Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().unsubscribe(onPlaybackErrorNotification, &error);
    }
    else if(moduleName == "speechstart"){
        Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().unsubscribe(onSpeechstartNotification, &error);
    }
    else if(moduleName == "speechcomplete"){
        Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().unsubscribe(onSpeechcompleteNotification, &error);
    }
    else if(moduleName == "speechinterupped"){
        Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().unsubscribe(onSpeechinterruptedNotification, &error);
    }
    else if(moduleName == "speechpause"){
        Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().unsubscribe(onSpeechpauseNotification, &error);
    }
    else if(moduleName == "speechresume"){
        Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().unsubscribe(onSpeechresumeNotification, &error);
    }
    else if(moduleName == "ttsstatechange"){
        Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().unsubscribe(onTtsstatechangedNotification, &error);
    }
    else{
         Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().unsubscribe(onVoicechangedNotification, &error);
    }
    if (error == Firebolt::Error::None) {
        TTSLOG_INFO("Unsubscribe Event \"%s\" Sucessfull\n",moduleName.c_str());
    } else {
        TTSLOG_ERROR("Failed to unsubscribe Event: \"%s\" Error: \"%d\" \n",moduleName.c_str(),static_cast<int>(error));
    }
}

/* ### Firebolt Event Subscribe & Unsubscribe API ### */
bool TextToSpeechServiceFirebolt::subscribeEvents() {   

   SubscribeVoiceGuidanceSettings("networkerror");
   SubscribeVoiceGuidanceSettings("playbackerror");
   SubscribeVoiceGuidanceSettings("speechstart");
   SubscribeVoiceGuidanceSettings("speechcomplete");
   SubscribeVoiceGuidanceSettings("speechinterupped");
   SubscribeVoiceGuidanceSettings("speechpause");
   SubscribeVoiceGuidanceSettings("speechresume");
   SubscribeVoiceGuidanceSettings("ttsstatechange");
   SubscribeVoiceGuidanceSettings("voicechanged");
   return true;
}

bool TextToSpeechServiceFirebolt::unSubscribeEvents() {
    UnsubscribeVoiceGuidanceSettings("networkerror");
    UnsubscribeVoiceGuidanceSettings("playbackerror");
    UnsubscribeVoiceGuidanceSettings("speechstart");
    UnsubscribeVoiceGuidanceSettings("speechcomplete");
    UnsubscribeVoiceGuidanceSettings("speechinterupped");
    UnsubscribeVoiceGuidanceSettings("speechpause");
    UnsubscribeVoiceGuidanceSettings("speechresume");
    UnsubscribeVoiceGuidanceSettings("ttsstatechange");
    UnsubscribeVoiceGuidanceSettings("voicechanged");
    return true;
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

bool TextToSpeechServiceFirebolt::isEnabled(bool &enable)
{
    if(!isActive()) {
       TTSLOG_ERROR("Firebolt is not active (or) channel is couldn't be opened");
       return false;
    }
    Firebolt::Error error = Firebolt::Error::None;
    Firebolt::TextToSpeech::TTSEnabled ttsEnabled = Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().isttsenabled(&error);
    if( error == Firebolt::Error::None && !ttsEnabled.TTS_status)
    {
        enable = ttsEnabled.isenabled;
        return true;
    }
    if(error != Firebolt::Error::None) {
       TTSLOG_ERROR("isEnabled: Firebolt Error: \"%d\" ",static_cast<int>(error));
    }
    return false;
}

bool TextToSpeechServiceFirebolt::isSpeaking(uint32_t &speechid,bool &isspeaking)
{
    if(!isActive()) {
       TTSLOG_ERROR("Firebolt is not active (or) channel is couldn't be opened");
       return false;
    }
    Firebolt::TextToSpeech::SpeechStateResponse state;
    Firebolt::Error error = Firebolt::Error::None;
    state = Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().getspeechstate(speechid, &error);
    if (error == Firebolt::Error::None && state.success) {
        //state.speechstate -> string type
        isspeaking = false; // Here need to compare the speechstate to "IS_SPEAKING", until that false
        return isspeaking;
    }
    else if(error != Firebolt::Error::None){
        TTSLOG_ERROR("isSpeaking: Firebolt Error: \"%d\" ",static_cast<int>(error));
        return false;
    }
    return state.success;
}


bool TextToSpeechServiceFirebolt::setConfiguration(Firebolt::TextToSpeech::TTSConfiguration &ttsconfig){
    Firebolt::Error error = Firebolt::Error::None;
    if(!isActive()) {
       TTSLOG_ERROR("Firebolt is not active (or) channel is couldn't be opened");
       return false;
    }
    Firebolt::TextToSpeech::TTSStatusResponse ttsStatusResponse = Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().setttsconfiguration(
    ttsconfig.ttsendpoint.value()
    , ttsconfig.ttsendpointsecured.value()
    , ttsconfig.language.value()
    , ttsconfig.voice.value()
    , ttsconfig.volume.value()
    , std::nullopt
    , ttsconfig.rate.value()
    , std::nullopt
    , std::nullopt/*ttsconfig.fallbacktext.value()*/
    , &error);
    if(error == Firebolt::Error::None && ttsStatusResponse.success){
        return true;
    }
    else if(error != Firebolt::Error::None){
        TTSLOG_ERROR("setConfiguration: Firebolt error Status = %d", static_cast<int>(error));
        return false;
    }
    return ttsStatusResponse.success;
}

bool TextToSpeechServiceFirebolt::getConfiguration(Firebolt::TextToSpeech::TTSConfiguration &ttsconfig) {
    Firebolt::Error error = Firebolt::Error::None;
    if(!isActive()) {
       TTSLOG_ERROR("Firebolt is not active (or) channel is couldn't be opened");
       return false;
    }
    ttsconfig = Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().getttsconfiguration(&error);
    if (error == Firebolt::Error::None && ttsconfig.success) {
        return true;
    }
    else if(error != Firebolt::Error::None){
        TTSLOG_ERROR("getConfiguration: Firebolt Error: \"%d\" ",static_cast<int>(error));
        return false;
    }
    return ttsconfig.success;
}

// Firebolt is returning the string, but expectation is a list of strings...
bool TextToSpeechServiceFirebolt::listVoices(std::string &language, std::vector<std::string> &voices) {
    Firebolt::Error error = Firebolt::Error::None;
    Firebolt::TextToSpeech::ListVoicesResponse listVoicesResponse;
    if(!isActive()) {
       TTSLOG_ERROR("Firebolt is not active (or) channel is couldn't be opened");
       return false;
    }
    listVoicesResponse = Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().listvoices(language, &error);
    if(error == Firebolt::Error::None && !listVoicesResponse.TTS_status)
    {
        voices = listVoicesResponse.voices;
        return true;
    }
    else if (error != Firebolt::Error::None) {
        TTSLOG_ERROR("listVoices: Firebolt Error: \"%d\" ",static_cast<int>(error));
    }
    return false;
}

// Firebolt Speak API is not using any speechId parameter instead, it is returning speechResponse structure.
bool TextToSpeechServiceFirebolt::speak(std::string &callsign,std::string &text,uint32_t &speechid){
    if(!isActive()) {
       TTSLOG_ERROR("Firebolt is not active (or) channel is couldn't be opened");
       return false;
    }
    Firebolt::Error error = Firebolt::Error::None;
    Firebolt::TextToSpeech::SpeechResponse speechResponse =
        Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().speak(text, callsign, &error);
    if (error == Firebolt::Error::None && speechResponse.success) {
        speechid = speechResponse.speechid;
        return true;
    }
    else if(error != Firebolt::Error::None){
        TTSLOG_ERROR("speak: Firebolt Error: \"%d\" ",static_cast<int>(error));
        return false;
    }
    return false;
}

bool TextToSpeechServiceFirebolt::pause(uint32_t &speechid) {
    if(!isActive()) {
       TTSLOG_ERROR("Firebolt is not active (or) channel is couldn't be opened");
       return false;
    }
    Firebolt::Error error = Firebolt::Error::None;
    Firebolt::TextToSpeech::TTSStatusResponse speechResponse =
        Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().pause(speechid, &error);
    if (error == Firebolt::Error::None && speechResponse.success) {
        return true;
    }
    else if(error != Firebolt::Error::None){
        TTSLOG_ERROR("pause: Firebolt Error: \"%d\" ",static_cast<int>(error));
        return false;
    }
    return false;
}

bool TextToSpeechServiceFirebolt::resume(uint32_t &speechid) {
    if(!isActive()) {
       TTSLOG_ERROR("Firebolt is not active (or) channel is couldn't be opened");
       return false;
    }
    Firebolt::Error error = Firebolt::Error::None;
    Firebolt::TextToSpeech::TTSStatusResponse speechResponse =
        Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().resume(speechid, &error);
    if (error == Firebolt::Error::None && speechResponse.success) {
        return true;
    }
    else if(error != Firebolt::Error::None){
        TTSLOG_ERROR("resume: Firebolt Error: \"%d\" ",static_cast<int>(error));
        return false;
    }
    return false;
}

bool TextToSpeechServiceFirebolt::cancel(uint32_t &speechid) {
    if(!isActive()) {
       TTSLOG_ERROR("Firebolt is not active (or) channel is couldn't be opened");
       return false;
    }
    Firebolt::Error error = Firebolt::Error::None;
    Firebolt::TextToSpeech::TTSStatusResponse speechResponse =
        Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().cancel(speechid, &error);
    if (error == Firebolt::Error::None && speechResponse.success) {
        return true;
    }
    else if(error != Firebolt::Error::None){
        TTSLOG_ERROR("cancel: Firebolt Error: \"%d\" ",static_cast<int>(error));
        return false;
    }
    return false;
}

// SpeechState is the enum variable; but Firebolt returns a structure "SpeechStateResponse" where speechstate value is string
// Firebolt is accepting the speechid, but the COMRPC and JSON implementation is using serviceId.
bool TextToSpeechServiceFirebolt::getSpeechState(uint32_t &speechid,Firebolt::TextToSpeech::SpeechStateResponse &state) {
    Firebolt::Error error = Firebolt::Error::None;
    state = Firebolt::IFireboltAccessor::Instance().TextToSpeechInterface().getspeechstate(speechid, &error);
    if (error == Firebolt::Error::None && state.success) {
        return true;
    }
    else if(error != Firebolt::Error::None){
        TTSLOG_ERROR("getSpeechState: Firebolt Error: \"%d\" ",static_cast<int>(error));
        return false;
    }
    return false;
}

void TextToSpeechServiceFirebolt::OnNetworkerrorNotification::onNetworkerror(const Firebolt::TextToSpeech::SpeechIdEvent &speechIdEvent) {
    TTSLOG_INFO("Received NetworkError for the speechId \"%d\"",speechIdEvent.speechid);
    TextToSpeechServiceFirebolt::Instance()->dispatchEvent(EventType::NetworkError, speechIdEvent.speechid,std::nullopt,std::nullopt);
}

void TextToSpeechServiceFirebolt::OnPlaybackErrorNotification::onPlaybackError(const Firebolt::TextToSpeech::SpeechIdEvent &speechIdEvent){
    TTSLOG_INFO("Received PlaybackError for the speechId \"%d\"",speechIdEvent.speechid);
    TextToSpeechServiceFirebolt::Instance()->dispatchEvent(EventType::PlaybackError, speechIdEvent.speechid,std::nullopt,std::nullopt);
}

void TextToSpeechServiceFirebolt::OnSpeechcompleteNotification::onSpeechcomplete(const Firebolt::TextToSpeech::SpeechIdEvent &speechIdEvent){
    TTSLOG_INFO("Received SpeechComplete for the speechId \"%d\"",speechIdEvent.speechid);
    TextToSpeechServiceFirebolt::Instance()->dispatchEvent(EventType::SpeechComplete, speechIdEvent.speechid,std::nullopt,std::nullopt);
}

void TextToSpeechServiceFirebolt::OnSpeechinterruptedNotification::onSpeechinterrupted(const Firebolt::TextToSpeech::SpeechIdEvent &speechIdEvent){
    TTSLOG_INFO("Received SpeechInterrupted for the speechId \"%d\"",speechIdEvent.speechid);
    TextToSpeechServiceFirebolt::Instance()->dispatchEvent(EventType::SpeechInterrupt, speechIdEvent.speechid,std::nullopt,std::nullopt);
}

void TextToSpeechServiceFirebolt::OnSpeechpauseNotification::onSpeechpause(const Firebolt::TextToSpeech::SpeechIdEvent &speechIdEvent){
    TTSLOG_INFO("Received SpeechPause for the speechId \"%d\"",speechIdEvent.speechid);
    TextToSpeechServiceFirebolt::Instance()->dispatchEvent(EventType::SpeechPause, speechIdEvent.speechid,std::nullopt,std::nullopt);
}

void TextToSpeechServiceFirebolt::OnSpeechresumeNotification::onSpeechresume(const Firebolt::TextToSpeech::SpeechIdEvent &speechIdEvent){
    TTSLOG_INFO("Received SpeechResume for the speechId \"%d\"",speechIdEvent.speechid);
    TextToSpeechServiceFirebolt::Instance()->dispatchEvent(EventType::SpeechResume, speechIdEvent.speechid,std::nullopt,std::nullopt);
}

void TextToSpeechServiceFirebolt::OnSpeechstartNotification::onSpeechstart(const Firebolt::TextToSpeech::SpeechIdEvent &speechIdEvent){
    TTSLOG_INFO("Received SpeechStart for the speechId \"%d\"",speechIdEvent.speechid);
    TextToSpeechServiceFirebolt::Instance()->dispatchEvent(EventType::SpeechStart,speechIdEvent.speechid,std::nullopt,std::nullopt);
}

void TextToSpeechServiceFirebolt::OnTtsstatechangedNotification::onTTSstatechanged(const Firebolt::TextToSpeech::TTSState &ttsState){
    TTSLOG_INFO("Received TTSStatechanged for the ttsState \"%s\"", ttsState.state ? "true" : "false");
    TextToSpeechServiceFirebolt::Instance()->dispatchEvent(EventType::StateChange,std::nullopt,ttsState.state,std::nullopt);
}

void TextToSpeechServiceFirebolt::OnVoicechangedNotification::onVoicechanged(const Firebolt::TextToSpeech::TTSVoice &ttsVoice){
    TTSLOG_INFO("Received VoiceChanged \"%s\"",ttsVoice.voice);
    TextToSpeechServiceFirebolt::Instance()->dispatchEvent(EventType::VoiceChange,std::nullopt,std::nullopt,ttsVoice.voice);

} // namespace TextToSpeechServiceFirebolt
}
