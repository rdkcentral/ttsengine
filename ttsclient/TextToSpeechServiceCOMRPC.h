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
#ifndef _TEXTTOSPEECH_SERVICES_COMRPC_H_
#define _TEXTTOSPEECH_SERVICES_COMRPC_H_

#ifndef MODULE_NAME
#define MODULE_NAME ttsengine
#endif

#include <WPEFramework/core/core.h>
#include <WPEFramework/plugins/Service.h>

#include <com/com.h>
#include <core/core.h>
#include <plugins/plugins.h>

#undef LOG
#include <condition_variable>
#include <thread>
#include <mutex>
#include <list>

#include <unistd.h>
#include <sys/syscall.h>

#ifndef _LOG_INFO
#define _LOG_INFO(fmt, ...) do { \
    printf("000000-00:00:00.000 [INFO] [tid=%ld] %s:%s:%d " fmt "\n", \
    syscall(__NR_gettid), \
    __FUNCTION__, basename(__FILE__), __LINE__, \
    __VA_ARGS__); \
} while(0)
#endif

#include <interfaces/ITextToSpeech.h>

namespace TTSThunderClient {


using namespace WPEFramework;

class TextToSpeechServiceCOMRPC
{
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

    // To activate & initialize on service crash
    // Those should be done on a separate thread other than
    // the callback thread
    struct AsyncWorker {
        using Task = std::function<void (TextToSpeechServiceCOMRPC *service)>;
        using TaskList = std::list<Task>;

        AsyncWorker(TextToSpeechServiceCOMRPC *service) : m_service(service), m_thread(nullptr), m_running(true) {}
        ~AsyncWorker() { cleanup(); }

        void post(Task task);
        void cleanup();

        private:
        TextToSpeechServiceCOMRPC *m_service;
        std::thread *m_thread;

        bool m_running;
        TaskList m_tasklist;
        std::condition_variable m_condition;
        std::mutex m_mutex;

        friend class std::thread;
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

    class Notification
        : public Exchange::ITextToSpeech::INotification
    {
        private:
            Notification() = delete;
            Notification(const Notification&) = delete;
            Notification& operator=(const Notification&) = delete;

        public:
            explicit Notification(TextToSpeechServiceCOMRPC* parent)
                : _parent(*parent) { ASSERT(parent != nullptr); }

            virtual ~Notification() { }

        public:
            // ITextToSpeech::INotification
            virtual void Enabled(const bool state) override {
                _parent.dispatchEvent(EventType::StateChange,JsonValue((bool)state));
            }

            virtual void VoiceChanged(const string voice) override {
                _parent.dispatchEvent(EventType::VoiceChange,JsonValue((std::string)voice));
            }

            virtual void WillSpeak(const uint32_t ) {
                // Ignore
            }

            virtual void SpeechStart(const uint32_t speechid) override {
                _parent.dispatchEvent(EventType::SpeechStart,JsonValue((int)speechid));
            }

            virtual void SpeechPause(const uint32_t speechid) override {
                _parent.dispatchEvent(EventType::SpeechPause, JsonValue((int)speechid));
            }

            virtual void SpeechResume(const uint32_t speechid) override {
                _parent.dispatchEvent(EventType::SpeechResume, JsonValue((int)speechid));
            }

            virtual void SpeechInterrupted(const uint32_t speechid) override {
                _parent.dispatchEvent(EventType::SpeechInterrupt, JsonValue((int)speechid));
            }

            virtual void NetworkError(const uint32_t speechid) override {
                _parent.dispatchEvent(EventType::NetworkError, JsonValue((int)speechid));
            }

            virtual void PlaybackError(const uint32_t speechid) override {
                _parent.dispatchEvent(EventType::PlaybackError, JsonValue((int)speechid));
            }

            virtual void SpeechComplete(const uint32_t speechid) override {
                _parent.dispatchEvent(EventType::SpeechComplete, JsonValue((int)speechid));
            }

            BEGIN_INTERFACE_MAP(Notification)
                INTERFACE_ENTRY(Exchange::ITextToSpeech::INotification)
            END_INTERFACE_MAP

        private:
            TextToSpeechServiceCOMRPC& _parent;
    };

public:
    static TextToSpeechServiceCOMRPC* Instance();
    TextToSpeechServiceCOMRPC(const TextToSpeechServiceCOMRPC&) = delete;
    TextToSpeechServiceCOMRPC& operator=(const TextToSpeechServiceCOMRPC&) = delete;
    virtual ~TextToSpeechServiceCOMRPC();

    bool isActive(bool force=false);
    void initialize(string callsign,bool activateIfRequired = false);
    void uninitialize();
    bool initialized();
    void registerSpeechEventHandlers(string callsign);

    void registerClient(Client *client);
    void unregisterClient(Client *client);

    bool setConfiguration(Exchange::ITextToSpeech::Configuration &ttsconfig);
    bool getConfiguration(Exchange::ITextToSpeech::Configuration &ttsconfig);
    bool listVoices(string &language,std::vector<std::string> &voices);
    bool isSpeaking(uint32_t &speechid,bool &isspeaking);
    bool getSpeechState(uint32_t &speechid,Exchange::ITextToSpeech::SpeechState &state);
    bool isEnabled(bool &enable);
    bool enableTTS(bool &enable);
    bool speak(string &callsign,string &text,uint32_t &speechid);
    bool pause(uint32_t &speechid);
    bool resume(uint32_t &speechid);
    bool cancel(uint32_t &speechid);

private:
    TextToSpeechServiceCOMRPC();

    void dispatchEvent(EventType event, const JsonValue &params);
    void dispatchEventOnWorker(EventType event, const JsonValue params);

    bool m_initialized;
    uint8_t m_pendingInitAttempts;
    bool m_registeredSpeechEventHandlers;
    ClientList m_clients;
    std::mutex m_mutex;
    string m_callsign;

    Core::ProxyType<RPC::InvokeServerType<1, 0, 4>> m_engine;
    Core::ProxyType<RPC::CommunicatorClient> m_comChannel;
    Exchange::ITextToSpeech *m_remoteObject { nullptr };
    Core::Sink<Notification> m_notification;

    AsyncWorker m_worker;

    friend class Notification;
};

} // namespace TTSThunderClient

#undef _LOG_INFO

#endif  // _TEXTTOSPEECH_SERVICES_COMRPC_H_

