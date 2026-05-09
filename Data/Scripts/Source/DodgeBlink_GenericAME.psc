Scriptname DodgeBlink_GenericAME extends ActiveMagicEffect
{ Generic active effect script for shout/spell/power/keybind wrappers. }

Int Property Tier = 1 Auto
Bool Property PlayerOnly = True Auto
Bool Property UsePlayerWrapper = True Auto
Int Property RequiredActivationMode = -1 Auto ; -1 = allow any mode, 0 = shout, 1 = spell, 2 = hotkey

Event OnEffectStart(Actor akTarget, Actor akCaster)
    Actor player = Game.GetPlayer()
    Bool takeoverEnabled = DodgeBlinkNative.GetIntConfig("TakeoverEnabled") != 0
    Int activationMode = DodgeBlinkNative.GetIntConfig("ActivationMode")
    Bool allowMultipleTypes = DodgeBlinkNative.GetIntConfig("AllowMultipleActivationTypes") != 0
    if !allowMultipleTypes && RequiredActivationMode >= 0 && activationMode != RequiredActivationMode
        Debug.Trace("[DodgeBlink] DodgeBlink_GenericAME ignored due to activation mode mismatch.")
        return
    endif

    if PlayerOnly && akCaster != player
        Debug.Trace("[DodgeBlink] DodgeBlink_GenericAME ignored non-player caster.")
        return
    endif

    Int resolvedTier = Tier
    if resolvedTier < 1
        resolvedTier = 1
    elseif resolvedTier > 3
        resolvedTier = 3
    endif
    Int resolvedMode = RequiredActivationMode
    if resolvedMode < 0 || resolvedMode > 2
        if allowMultipleTypes
            ; In multimode, generic casts are spell-context unless the record opts in explicitly.
            resolvedMode = 1
        else
            resolvedMode = activationMode
        endif
    endif

    if takeoverEnabled && resolvedMode != 2
        Debug.Trace("[DodgeBlink] DodgeBlink_GenericAME ignored because Takeover is enabled (hotkey-only lock).")
        return
    endif

    if UsePlayerWrapper && akCaster == player
        DodgeBlinkNative.DoBlinkTierForMode(player, resolvedTier, resolvedMode)
    else
        DodgeBlinkNative.DoBlinkTierForMode(akCaster, resolvedTier, resolvedMode)
    endif
EndEvent
