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

#include "TextToSpeechServiceCOMRPC.h"
#include "logger.h"

namespace TTSThunderClient {

#define TEXTTOSPEECH_CALLSIGN "org.rdk.TextToSpeech.1"

void TextToSpeechServiceCOMRPC::AsyncWorker::post(Task task) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tasklist.push_back(task);
    m_condition.notify_one();

    if(!m_thread) {
        m_thread = new std::thread([this](void *) {
            TTSLOG_VERBOSE("Started AsyncWorker thread");
            while(1) {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_condition.wait(lock, [this] { return (m_tasklist.size() > 0) || !m_running; });

                if(!m_running)
                    break;

                Task task = m_tasklist.front();
                m_tasklist.pop_front();
                m_mutex.unlock();

                task(m_service);
            }
            TTSLOG_VERBOSE("Exited from AsyncWorker thread");
        }, this);
    }
}

void TextToSpeechServiceCOMRPC::AsyncWorker::cleanup() {
    m_running = false;
    if(!m_thread)
        return;

    m_condition.notify_one();
    m_thread->join();
    delete m_thread;
    m_thread = nullptr;
}

Core::NodeId getConnectionEndpoint()
{
    string communicatorPath;
    Core::SystemInfo::GetEnvironment(_T("COMMUNICATOR_PATH"), communicatorPath);
    if (communicatorPath.empty())
        communicatorPath = _T("/tmp/communicator");
    return Core::NodeId(communicatorPath.c_str());
}

TextToSpeechServiceCOMRPC *TextToSpeechServiceCOMRPC::Instance()
{
    static TextToSpeechServiceCOMRPC instance;
    return &instance;
}

TextToSpeechServiceCOMRPC::TextToSpeechServiceCOMRPC()
    : m_initialized(false)
    , m_pendingInitAttempts(3)
    , m_registeredSpeechEventHandlers(false)
    , m_engine(Core::ProxyType<RPC::InvokeServerType<1, 0, 4>>::Create())
    , m_comChannel(Core::ProxyType<RPC::CommunicatorClient>::Create(getConnectionEndpoint(), Core::ProxyType<Core::IIPCServer>(m_engine)))
    , m_remoteObject(nullptr)
    , m_notification(this)
    , m_worker(this)
{
#if ((THUNDER_VERSION == 2) || ((THUNDER_VERSION >= 4) && (THUNDER_VERSION_MINOR == 2)))
    m_engine->Announcements(m_comChannel->Announcement());
#endif
}

TextToSpeechServiceCOMRPC::~TextToSpeechServiceCOMRPC()
{
    uninitialize();
}

bool TextToSpeechServiceCOMRPC::isActive(bool /*force*/)
{
    return initialized();
}

void TextToSpeechServiceCOMRPC::initialize(string callsign, bool /*activateIfRequired*/)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if(initialized())
        return;

    if(m_pendingInitAttempts > 0)
        --m_pendingInitAttempts;
    else
        return;

    m_remoteObject = m_comChannel->Open<Exchange::ITextToSpeech>(TEXTTOSPEECH_CALLSIGN);

    if(!m_remoteObject) {
        TTSLOG_ERROR("Couldn't connect to remote object \"%s\"", TEXTTOSPEECH_CALLSIGN);
        return;
    }

    TTSLOG_INFO("Successfully connected to remote object \"%s\"", TEXTTOSPEECH_CALLSIGN);
    m_remoteObject->AddRef();
    m_initialized = true;

    registerSpeechEventHandlers(callsign);

    for(ClientList::iterator it = m_clients.begin(); it != m_clients.end(); ++it)
        ((TextToSpeechServiceCOMRPC::Client*)(*it))->onTTSStateChange(false);
}

void TextToSpeechServiceCOMRPC::uninitialize()
{
    m_worker.cleanup();

    if(m_remoteObject) {
        if(m_registeredSpeechEventHandlers)
            m_remoteObject->Unregister(&m_notification);

        m_remoteObject->Release();
        m_remoteObject = nullptr;
    }
    m_registeredSpeechEventHandlers = false;

    if(m_comChannel.IsValid()) {
        if(m_comChannel->IsOpen())
            m_comChannel->Close(RPC::CommunicationTimeOut);
        m_comChannel.Release();
    }

    if(m_engine.IsValid())
        m_engine.Release();

    m_initialized = false;
}

bool TextToSpeechServiceCOMRPC::initialized()
{
    return m_initialized;
}

void TextToSpeechServiceCOMRPC::registerSpeechEventHandlers(string callsign)
{
    if(!m_registeredSpeechEventHandlers && m_remoteObject) {
        m_registeredSpeechEventHandlers = true;
        m_remoteObject->RegisterWithCallsign(callsign,&m_notification);
        m_callsign = callsign;
        TTSLOG_INFO("register for notification with callsign %s",callsign.c_str());
    }
}

void TextToSpeechServiceCOMRPC::registerClient(Client *client)
{
    if(!client)
        return;

    std::unique_lock<std::mutex> lock(m_mutex);
    if(std::find(m_clients.begin(), m_clients.end(), client) == m_clients.end())
        m_clients.push_back(client);
}

void TextToSpeechServiceCOMRPC::unregisterClient(Client *client)
{
    if(!client)
        return;

    std::unique_lock<std::mutex> lock(m_mutex);
    ClientList::iterator it = std::find(m_clients.begin(), m_clients.end(), client);
    if(it != m_clients.end())
        m_clients.erase(it);
}

void TextToSpeechServiceCOMRPC::dispatchEvent(EventType event, const JsonValue &params)
{
    m_worker.post([event, params](TextToSpeechServiceCOMRPC *service) {
        service->dispatchEventOnWorker(event, params);
    });
}

void TextToSpeechServiceCOMRPC::dispatchEventOnWorker(EventType event, const JsonValue params)
{
    int speechid = 0;
    bool enabled = false;
    std::string voice;
    bool dispatch = true;

    if(event == StateChange) {
        enabled = params.Boolean();
        TTSLOG_INFO("%s(StateChange), state=%s", __FUNCTION__, enabled ? "enabled" : "disabled");
    } else if (event == VoiceChange) {
        voice = params.String();
        TTSLOG_INFO("%s(VoiceChange), voice=%s", __FUNCTION__, voice.c_str());
    } else {
        speechid = params.Number();
        TTSLOG_INFO("%s(SpeechEvent-%d), servicespeecid=%d", __FUNCTION__, (int)event, speechid);
    }

    if(dispatch && initialized()) {
        std::unique_lock<std::mutex> lock(m_mutex);
        for(ClientList::iterator it = m_clients.begin(); it != m_clients.end(); ++it) {
            switch(event) {
                case StateChange: ((TextToSpeechServiceCOMRPC::Client*)(*it))->onTTSStateChange(enabled); break;
                case VoiceChange: ((TextToSpeechServiceCOMRPC::Client*)(*it))->onVoiceChange(voice); break;
                case SpeechStart: ((TextToSpeechServiceCOMRPC::Client*)(*it))->onSpeechStart(speechid); break;
                case SpeechPause: ((TextToSpeechServiceCOMRPC::Client*)(*it))->onSpeechPause(speechid); break;
                case SpeechResume: ((TextToSpeechServiceCOMRPC::Client*)(*it))->onSpeechResume(speechid); break;
                case SpeechCancel: ((TextToSpeechServiceCOMRPC::Client*)(*it))->onSpeechCancel(speechid); break;
                case SpeechInterrupt: ((TextToSpeechServiceCOMRPC::Client*)(*it))->onSpeechInterrupt(speechid); break;
                case NetworkError: ((TextToSpeechServiceCOMRPC::Client*)(*it))->onNetworkError(speechid); break;
                case PlaybackError: ((TextToSpeechServiceCOMRPC::Client*)(*it))->onPlaybackError(speechid); break;
                case SpeechComplete: ((TextToSpeechServiceCOMRPC::Client*)(*it))->onSpeechComplete(speechid); break;
            }
        }
    }
}

bool TextToSpeechServiceCOMRPC::setConfiguration(Exchange::ITextToSpeech::Configuration &ttsconfig)
{
    uint32_t ret = Core::ERROR_NONE;
    initialize(m_callsign);
    Exchange::ITextToSpeech::TTSErrorDetail status;
    if(!isActive()) {
        TTSLOG_ERROR("Callsign \"%s\" is not active (or) COM channel is couldn't be opened", TEXTTOSPEECH_CALLSIGN);
        return false;
    }

    ret = m_remoteObject->SetConfiguration(ttsconfig,status);
    return ret == Core::ERROR_NONE;
}

bool TextToSpeechServiceCOMRPC::getSpeechState(uint32_t &speechid,  Exchange::ITextToSpeech::SpeechState &state)
{
    uint32_t ret = Core::ERROR_NONE;
    initialize(m_callsign);
    if(!isActive()) {
        TTSLOG_ERROR("Callsign \"%s\" is not active (or) COM channel is couldn't be opened", TEXTTOSPEECH_CALLSIGN);
        return false;
    }
    ret = m_remoteObject->GetSpeechState(speechid,state);
    return ret == Core::ERROR_NONE;
}

bool TextToSpeechServiceCOMRPC::isSpeaking(uint32_t &speechid,bool &isspeaking)
{
    uint32_t ret = Core::ERROR_NONE;
    Exchange::ITextToSpeech::SpeechState istate;
    initialize(m_callsign);
    if(!isActive()) {
        TTSLOG_ERROR("Callsign \"%s\" is not active (or) COM channel is couldn't be opened", TEXTTOSPEECH_CALLSIGN);
        return false;
    }
    ret = m_remoteObject->GetSpeechState(speechid,istate);
    isspeaking = (istate ==  Exchange::ITextToSpeech::SpeechState::SPEECH_IN_PROGRESS);
    return ret == Core::ERROR_NONE;
}

bool  TextToSpeechServiceCOMRPC::isEnabled(bool &enable)
{
    uint32_t ret = Core::ERROR_NONE;
    initialize(m_callsign);
    if(!isActive()) {
       TTSLOG_ERROR("Callsign \"%s\" is not active (or) COM channel is couldn't be opened", TEXTTOSPEECH_CALLSIGN);
       return false;
    }
    ret = static_cast<const WPEFramework::Exchange::ITextToSpeech*>(m_remoteObject)->Enable(enable);
    return ret == Core::ERROR_NONE;
}

bool  TextToSpeechServiceCOMRPC::enableTTS(bool &enable)
{
    uint32_t ret = Core::ERROR_NONE;
    initialize(m_callsign);
    const bool update = enable;
    if(!isActive()) {
        TTSLOG_ERROR("Callsign \"%s\" is not active (or) COM channel is couldn't be opened", TEXTTOSPEECH_CALLSIGN);
        return false;
    }
    ret = m_remoteObject->Enable(update);
    return ret == Core::ERROR_NONE;
}

bool TextToSpeechServiceCOMRPC::speak(string &callsign, string &text, uint32_t &speechid)
{
    uint32_t ret = Core::ERROR_NONE;
    Exchange::ITextToSpeech::TTSErrorDetail status;
    initialize(m_callsign);
    if(!isActive()) {
        TTSLOG_ERROR("Callsign \"%s\" is not active (or) COM channel is couldn't be opened", TEXTTOSPEECH_CALLSIGN);
        return false;
    }
    ret = m_remoteObject->Speak(callsign,text,speechid,status);
    return ret == Core::ERROR_NONE;
}

bool TextToSpeechServiceCOMRPC::pause(uint32_t &speechid)
{
    uint32_t ret = Core::ERROR_NONE;
    Exchange::ITextToSpeech::TTSErrorDetail status;
    initialize(m_callsign);
    if(!isActive()) {
        TTSLOG_ERROR("Callsign \"%s\" is not active (or) COM channel is couldn't be opened", TEXTTOSPEECH_CALLSIGN);
        return false;
    }
    ret = m_remoteObject->Pause(speechid,status);
    return ret == Core::ERROR_NONE;
}

bool TextToSpeechServiceCOMRPC::resume(uint32_t &speechid)
{
    uint32_t ret = Core::ERROR_NONE;
    Exchange::ITextToSpeech::TTSErrorDetail status;
    initialize(m_callsign);
     if(!isActive()) {
        TTSLOG_ERROR("Callsign \"%s\" is not active (or) COM channel is couldn't be opened", TEXTTOSPEECH_CALLSIGN);
        return false;
    }
    ret = m_remoteObject->Resume(speechid,status);
    return ret == Core::ERROR_NONE;
}

bool TextToSpeechServiceCOMRPC::cancel(uint32_t &speechid)
{
    uint32_t ret = Core::ERROR_NONE;
    initialize(m_callsign);
    if(!isActive()) {
        TTSLOG_ERROR("Callsign \"%s\" is not active (or) COM channel is couldn't be opened", TEXTTOSPEECH_CALLSIGN);
        return false;
    }
    ret = m_remoteObject->Cancel(speechid);
    return ret == Core::ERROR_NONE;
}

bool TextToSpeechServiceCOMRPC::getConfiguration(Exchange::ITextToSpeech::Configuration &ttsconfig)
{
    uint32_t ret = Core::ERROR_NONE;
    initialize(m_callsign);
    if(!isActive()) {
        TTSLOG_ERROR("Callsign \"%s\" is not active (or) COM channel is couldn't be opened", TEXTTOSPEECH_CALLSIGN);
        return false;
    }
    ret = m_remoteObject->GetConfiguration(ttsconfig);
    return ret == Core::ERROR_NONE;
}

bool TextToSpeechServiceCOMRPC::listVoices(string &language,std::vector<std::string> &voices)
{
    uint32_t ret = Core::ERROR_NONE;
    RPC::IStringIterator* voice = nullptr;
    string element;
    if(!isActive()) {
        TTSLOG_ERROR("Callsign \"%s\" is not active (or) COM channel is couldn't be opened", TEXTTOSPEECH_CALLSIGN);
        return false;
    }
    ret = m_remoteObject->ListVoices(language,voice);
    while (voice->Next(element) == true) {
        voices.push_back(element);
    }
    return ret == Core::ERROR_NONE;
}

} // namespace TTSThunderClient
