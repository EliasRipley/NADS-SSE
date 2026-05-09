Scriptname DBS_Blink_AME extends ActiveMagicEffect

Int Property Tier = 1 Auto

Event OnEffectStart(Actor akTarget, Actor akCaster)
    Actor player = Game.GetPlayer()
    if DodgeBlinkNative.GetIntConfig("TakeoverEnabled") != 0
        Debug.Trace("[DodgeBlink] DBS_Blink_AME ignored because Takeover is enabled (hotkey-only lock).")
        return
    endif
    Bool allowMultipleTypes = DodgeBlinkNative.GetIntConfig("AllowMultipleActivationTypes") != 0
    if !allowMultipleTypes && DodgeBlinkNative.GetIntConfig("ActivationMode") != 0
        Debug.Trace("[DodgeBlink] DBS_Blink_AME ignored because ActivationMode is not Shout.")
        return
    endif
    if akCaster != player
        Debug.Trace("[DodgeBlink] DBS_Blink_AME ignored cast from non-player.")
        return
    endif

    Debug.Trace("[DodgeBlink] DBS_Blink_AME OnEffectStart tier=" + Tier)
    DodgeBlinkNative.DoBlinkTierForMode(player, Tier, 0)
EndEvent
