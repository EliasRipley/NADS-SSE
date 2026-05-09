Scriptname DodgeBlinkNative
{ Native Papyrus bindings implemented by the SKSE plugin. }

; Performs a blink by resolving a tier from range and starting native controller dash.
; Timing and optional animation hooks are configured by INI.
; @param akActor     Actor to blink (intended: player)
; @param range       Distance in game units (roughly 1 unit = 1/70th of a meter; Skyrim uses Havok units)
; @param maxAngleDeg Currently unused by native tiered dash path
; @param stepDeg     Currently unused by native tiered dash path
; @param clearance   Currently unused by native tiered dash path
Function DoBlink(Actor akActor, Float range, Float maxAngleDeg, Float stepDeg, Float clearance) Global Native

; Performs a tiered blink (1/2/3 words) using values loaded by the SKSE plugin.
; @param akActor Actor to blink (intended: player)
; @param tier   1..3 (word index)
Function DoBlinkTier(Actor akActor, Int tier) Global Native

; Performs a tiered blink with explicit activation mode context.
; activationMode: 0=Shout, 1=Spell, 2=Hotkey
Function DoBlinkTierForMode(Actor akActor, Int tier, Int activationMode) Global Native

; Convenience wrapper that resolves the player actor and performs tiered blink.
Function DoBlinkPlayerTier(Int tier) Global Native

; Returns true while the native dash gate is active.
Bool Function IsDashActive() Global Native

; Runtime config mutation/getters (key names mirror INI keys, case-insensitive).
Bool Function SetFloatConfig(String key, Float value) Global Native
Bool Function SetIntConfig(String key, Int value) Global Native
Float Function GetFloatConfig(String key) Global Native
Int Function GetIntConfig(String key) Global Native

; Persist current runtime config to Data/SKSE/Plugins/DodgeBlinkShout.ini
Bool Function SaveConfig() Global Native

; Reload config from INI into runtime.
Bool Function ReloadConfig() Global Native

; Returns true when directional third-person replacement assets are present in the NADS OAR pack.
Bool Function HasDirectionalThirdPersonAnimations() Global Native

; Debug helper for validating input direction conversion.
Function TestInputDirection() Global Native

; Returns the NADS shout word form by 1-based index (1..3), or None when unavailable.
WordOfPower Function GetNadsShoutWord(Int index) Global Native

; Optional helper: returns plugin version as an int (e.g. 100 for v1.0.0)
Int Function GetVersion() Global Native
