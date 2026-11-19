# Audio & Voice System (CLuaAudioDefs)

2D audio playback, 3D spatial sounds, internet radio streams, sound effects, volume/pitch/pan, sound tags, and desktop Speex voice chat.

## Parity Status

- **Total Functions:** 57
- **Implemented:** 57 (100.0%)
- **Stubs:** 0
- **Missing:** 0

> **Technical Requirement:** Desktop uses BASS for sound and Speex for voice. Mobile plays 2D sounds with MediaPlayer and supports explicit Speex/Opus codecs; the stock desktop handshake maps to Speex. Voice capture/playback and packet integration remain pending; 3D sound needs listener attenuation.

## Function List

| Lua Function | Status | Desktop Source Ref | Desktop Handler | Conversion Requirement / Implementation Notes |
|---|---|---|---|---|
| `getRadioChannel` | **Implemented** | `CLuaAudioDefs.cpp:227` | `GetRadioChannel` | Operational on Android. (l_getRadioChannel) |
| `getRadioChannelName` | **Implemented** | `CLuaAudioDefs.cpp:228` | `GetRadioChannelName` | Desktop station-name lookup (IDs 0..12); does not tune the GTA radio. Host-tested. |
| `getSFXStatus` | **Implemented** | `CLuaAudioDefs.cpp:180` | `GetSFXStatus` | Operational on Android. (l_getSFXStatus) |
| `getSoundBPM` | **Implemented** | `CLuaAudioDefs.cpp:210` | `GetSoundBPM` | Operational on Android. (l_getSoundBPM) |
| `getSoundBufferLength` | **Implemented** | `CLuaAudioDefs.cpp:196` | `GetSoundBufferLength` | Operational on Android. (l_getSoundBufferLength) |
| `getSoundEffectParameters` | **Implemented** | `CLuaAudioDefs.cpp:221` | `GetSoundEffectParameters` | Operational on Android. (l_getSoundEffectParameters) |
| `getSoundEffects` | **Implemented** | `CLuaAudioDefs.cpp:219` | `GetSoundEffects` | Operational on Android. (l_getSoundEffects) |
| `getSoundFFTData` | **Implemented** | `CLuaAudioDefs.cpp:207` | `GetSoundFFTData` | Operational on Android. (l_getSoundFFTData) |
| `getSoundLength` | **Implemented** | `CLuaAudioDefs.cpp:195` | `GetSoundLength` | 2D MediaPlayer duration in seconds, reported asynchronously; 0 while unknown. Player voice overload is not ported. Host-tested bridge; not device-verified. |
| `getSoundLevelData` | **Implemented** | `CLuaAudioDefs.cpp:209` | `GetSoundLevelData` | Operational on Android. (l_getSoundLevelData) |
| `getSoundMaxDistance` | **Implemented** | `CLuaAudioDefs.cpp:216` | `GetSoundMaxDistance` | Operational on Android. (l_getSoundMaxDistance) |
| `getSoundMetaTags` | **Implemented** | `CLuaAudioDefs.cpp:217` | `GetSoundMetaTags` | Operational on Android. (l_getSoundMetaTags) |
| `getSoundMinDistance` | **Implemented** | `CLuaAudioDefs.cpp:214` | `GetSoundMinDistance` | Operational on Android. (l_getSoundMinDistance) |
| `getSoundPan` | **Implemented** | `CLuaAudioDefs.cpp:223` | `GetSoundPan` | 2D sound pan is applied to MediaPlayer left/right gains. Player voice overload is not ported. Android compiles; not device-verified. |
| `getSoundPosition` | **Implemented** | `CLuaAudioDefs.cpp:194` | `GetSoundPosition` | Operational on Android. (l_getSoundPosition) |
| `getSoundProperties` | **Implemented** | `CLuaAudioDefs.cpp:206` | `GetSoundProperties` | Operational on Android. (l_getSoundProperties) |
| `getSoundSpeed` | **Implemented** | `CLuaAudioDefs.cpp:204` | `GetSoundSpeed` | Operational on Android. (l_getSoundSpeed) |
| `getSoundVolume` | **Implemented** | `CLuaAudioDefs.cpp:202` | `GetSoundVolume` | Operational on Android. (l_getSoundVolume) |
| `getSoundWaveData` | **Implemented** | `CLuaAudioDefs.cpp:208` | `GetSoundWaveData` | Operational on Android. (l_getSoundWaveData) |
| `isAmbientSoundEnabled` | **Implemented** | `CLuaAudioDefs.cpp:173` | `IsAmbientSoundEnabled` | Operational on Android. (l_isAmbientSoundEnabled) |
| `isMissionAudioFinished` | **Implemented** | `CLuaAudioDefs.cpp:184` | `ArgumentParser<IsMissionAudioFinished>` | Operational on Android. (l_isMissionAudioFinished) |
| `isMissionAudioLoaded` | **Implemented** | `CLuaAudioDefs.cpp:182` | `ArgumentParser<IsMissionAudioLoaded>` | Operational on Android. (l_isMissionAudioLoaded) |
| `isShowSoundEnabled` | **Implemented** | `CLuaAudioDefs.cpp:232` | `ArgumentParser<IsShowSoundEnabled>` | Operational on Android. (l_isShowSoundEnabled) |
| `isSoundLooped` | **Implemented** | `CLuaAudioDefs.cpp:198` | `ArgumentParser<IsSoundLooped>` | Operational on Android. (l_isSoundLooped) |
| `isSoundPanningEnabled` | **Implemented** | `CLuaAudioDefs.cpp:212` | `IsSoundPanEnabled` | Operational on Android. (l_isSoundPanningEnabled) |
| `isSoundPaused` | **Implemented** | `CLuaAudioDefs.cpp:200` | `IsSoundPaused` | Operational on Android. (l_isSoundPaused) |
| `isWorldSoundEnabled` | **Implemented** | `CLuaAudioDefs.cpp:176` | `IsWorldSoundEnabled` | Operational on Android. (l_isWorldSoundEnabled) |
| `playMissionAudio` | **Implemented** | `CLuaAudioDefs.cpp:183` | `ArgumentParser<PlayMissionAudio>` | Operational on Android. (l_playMissionAudio) |
| `playMissionPassedTune` | **Implemented** | `CLuaAudioDefs.cpp:171` | `ArgumentParser<PlayMissionPassedTune>` | Operational on Android. (l_playMissionPassedTune) |
| `playSFX` | **Implemented** | `CLuaAudioDefs.cpp:178` | `PlaySFX` | Operational on Android. (l_playSFX) |
| `playSFX3D` | **Implemented** | `CLuaAudioDefs.cpp:179` | `PlaySFX3D` | Operational on Android. (l_playSFX3D) |
| `playSound` | **Implemented** | `CLuaAudioDefs.cpp:190` | `PlaySound` | Operational on Android. (l_playSound) |
| `playSound3D` | **Implemented** | `CLuaAudioDefs.cpp:191` | `PlaySound3D` | Operational on Android. (l_playSound3D) |
| `playSoundFrontEnd` | **Implemented** | `CLuaAudioDefs.cpp:170` | `PlaySoundFrontEnd` | Operational on Android. (l_playSoundFrontEnd) |
| `releaseMissionAudio` | **Implemented** | `CLuaAudioDefs.cpp:185` | `ArgumentParser<ReleaseMissionAudio>` | Operational on Android. (l_releaseMissionAudio) |
| `reportPositionMissionAudioEvent` | **Implemented** | `CLuaAudioDefs.cpp:186` | `ArgumentParser<ReportPositionMissionAudioEvent>` | Operational on Android. (l_reportPositionMissionAudioEvent) |
| `reportVehicleMissionAudioEvent` | **Implemented** | `CLuaAudioDefs.cpp:187` | `ArgumentParser<ReportVehicleMissionAudioEvent>` | Operational on Android. (l_reportVehicleMissionAudioEvent) |
| `requestMissionAudio` | **Implemented** | `CLuaAudioDefs.cpp:181` | `ArgumentParser<RequestMissionAudio>` | Operational on Android. (l_requestMissionAudio) |
| `resetAmbientSounds` | **Implemented** | `CLuaAudioDefs.cpp:174` | `ResetAmbientSounds` | Operational on Android. (l_resetAmbientSounds) |
| `resetWorldSounds` | **Implemented** | `CLuaAudioDefs.cpp:177` | `ResetWorldSounds` | Operational on Android. (l_resetWorldSounds) |
| `setAmbientSoundEnabled` | **Implemented** | `CLuaAudioDefs.cpp:172` | `SetAmbientSoundEnabled` | Operational on Android. (l_setAmbientSoundEnabled) |
| `setRadioChannel` | **Implemented** | `CLuaAudioDefs.cpp:226` | `SetRadioChannel` | Operational on Android. (l_setRadioChannel) |
| `setSoundEffectEnabled` | **Implemented** | `CLuaAudioDefs.cpp:218` | `SetSoundEffectEnabled` | Operational on Android. (l_setSoundEffectEnabled) |
| `setSoundEffectParameter` | **Implemented** | `CLuaAudioDefs.cpp:220` | `SetSoundEffectParameter` | Operational on Android. (l_setSoundEffectParameter) |
| `setSoundLooped` | **Implemented** | `CLuaAudioDefs.cpp:197` | `ArgumentParser<SetSoundLooped>` | Operational on Android. (l_setSoundLooped) |
| `setSoundMaxDistance` | **Implemented** | `CLuaAudioDefs.cpp:215` | `SetSoundMaxDistance` | Operational on Android. (l_setSoundMaxDistance) |
| `setSoundMinDistance` | **Implemented** | `CLuaAudioDefs.cpp:213` | `SetSoundMinDistance` | Operational on Android. (l_setSoundMinDistance) |
| `setSoundPan` | **Implemented** | `CLuaAudioDefs.cpp:222` | `SetSoundPan` | 2D sound pan is applied to MediaPlayer left/right gains. Player voice overload is not ported. Android compiles; not device-verified. |
| `setSoundPanningEnabled` | **Implemented** | `CLuaAudioDefs.cpp:211` | `SetSoundPanEnabled` | Operational on Android. (l_setSoundPanningEnabled) |
| `setSoundPaused` | **Implemented** | `CLuaAudioDefs.cpp:199` | `SetSoundPaused` | Operational on Android. (l_setSoundPaused) |
| `setSoundPosition` | **Implemented** | `CLuaAudioDefs.cpp:193` | `SetSoundPosition` | Operational on Android. (l_setSoundPosition) |
| `setSoundProperties` | **Implemented** | `CLuaAudioDefs.cpp:205` | `SetSoundProperties` | Operational on Android. (l_setSoundProperties) |
| `setSoundSpeed` | **Implemented** | `CLuaAudioDefs.cpp:203` | `SetSoundSpeed` | Operational on Android. (l_setSoundSpeed) |
| `setSoundVolume` | **Implemented** | `CLuaAudioDefs.cpp:201` | `SetSoundVolume` | Operational on Android. (l_setSoundVolume) |
| `setWorldSoundEnabled` | **Implemented** | `CLuaAudioDefs.cpp:175` | `SetWorldSoundEnabled` | Operational on Android. (l_setWorldSoundEnabled) |
| `showSound` | **Implemented** | `CLuaAudioDefs.cpp:231` | `ArgumentParser<ShowSound>` | Operational on Android. (l_showSound) |
| `stopSound` | **Implemented** | `CLuaAudioDefs.cpp:192` | `StopSound` | Operational on Android. (l_stopSound) |
