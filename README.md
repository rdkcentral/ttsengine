# TTS Engine
TTS Engine is a simple library written in c / c++ for the purpose of providing a client facing native interface (known as "tts client") that abstracts the underlying TTS Implementation available in the platform.

In RDK-V build, this tts client interface abstracts the complexities involved in invoking the Text To Speech Thunder APIs and thus allows the app run times (such as WPE WebKit) and Native Applications (such as Netflix, Cobalt) to integrate themselves with the platform's TTS capabilities in an easy manner by linking with this library and calling the direct c/c++ APIs.

In RDK-E build, the tts client interface implementation would add support for abstracting TTS Firebolt APIs in such a way that there will not be any change required in the app run time and native apps integration layer (Coming soon!)
