Scriptname DodgeBlink_MCN extends SKI_ConfigBase

Int Property ACTIVATION_MODE_SHOUT = 0 AutoReadOnly
Int Property ACTIVATION_MODE_SPELL = 1 AutoReadOnly
Int Property ACTIVATION_MODE_HOTKEY = 2 AutoReadOnly
Int Property TAKEOVER_STYLE_TK_DEFAULT = 0 AutoReadOnly
Int Property TAKEOVER_STYLE_STEP_AND_FORWARD_ROLL = 1 AutoReadOnly
Int Property TAKEOVER_STYLE_FULL_STEP = 2 AutoReadOnly
Int Property TAKEOVER_PROVIDER_TK = 0 AutoReadOnly
Int Property TAKEOVER_PROVIDER_DMCO = 1 AutoReadOnly
Int Property DMCO_TAKEOVER_STYLE_SET_1 = 0 AutoReadOnly
Int Property DMCO_TAKEOVER_STYLE_SET_2 = 1 AutoReadOnly
Bool _shoutWordsTeachDone = False

Int Function GetVersion()
    return 25
EndFunction

Function InitializeConfigState()
    ModName = "NADS Dodge Blink"
    Pages = new String[3]
    Pages[0] = "Main"
    Pages[1] = "TK Takeover"
    Pages[2] = "DMCO Takeover"
EndFunction

Int Function NormalizeActivationMode(Int mode)
    if mode < ACTIVATION_MODE_SHOUT || mode > ACTIVATION_MODE_HOTKEY
        return ACTIVATION_MODE_HOTKEY
    endif
    return mode
EndFunction

Int Function GetActivationMode()
    return NormalizeActivationMode(DodgeBlinkNative.GetIntConfig("ActivationMode"))
EndFunction

Bool Function GetAllowMultipleTypes()
    return DodgeBlinkNative.GetIntConfig("AllowMultipleActivationTypes") != 0
EndFunction

Bool Function IsTakeoverEnabled()
    return DodgeBlinkNative.GetIntConfig("TakeoverEnabled") != 0
EndFunction

Int Function NormalizeTakeoverStyle(Int style)
    if style < TAKEOVER_STYLE_TK_DEFAULT || style > TAKEOVER_STYLE_FULL_STEP
        return TAKEOVER_STYLE_TK_DEFAULT
    endif
    return style
EndFunction

Int Function GetTakeoverStyle()
    return NormalizeTakeoverStyle(DodgeBlinkNative.GetIntConfig("TakeoverStyle"))
EndFunction

Int Function NormalizeDmcoTakeoverStyle(Int style)
    if style < DMCO_TAKEOVER_STYLE_SET_1 || style > DMCO_TAKEOVER_STYLE_SET_2
        return DMCO_TAKEOVER_STYLE_SET_1
    endif
    return style
EndFunction

Int Function GetDmcoTakeoverStyle()
    return NormalizeDmcoTakeoverStyle(DodgeBlinkNative.GetIntConfig("DmcoTakeoverStyle"))
EndFunction

String Function GetDmcoTakeoverStyleLabel(Int style)
    style = NormalizeDmcoTakeoverStyle(style)
    if style == DMCO_TAKEOVER_STYLE_SET_2
        return "Set 2 (-2 HKX)"
    endif
    return "Set 1 (-1 HKX)"
EndFunction

Int Function NormalizeTakeoverProvider(Int provider)
    if provider < TAKEOVER_PROVIDER_TK || provider > TAKEOVER_PROVIDER_DMCO
        return TAKEOVER_PROVIDER_TK
    endif
    return provider
EndFunction

Int Function GetTakeoverProvider()
    return NormalizeTakeoverProvider(DodgeBlinkNative.GetIntConfig("TakeoverProvider"))
EndFunction

String Function GetTakeoverProviderLabel(Int provider)
    provider = NormalizeTakeoverProvider(provider)
    if provider == TAKEOVER_PROVIDER_DMCO
        return "DMCO"
    endif
    return "TK"
EndFunction

String Function GetTakeoverLockLabel()
    return "Hotkey (Locked by " + GetTakeoverProviderLabel(GetTakeoverProvider()) + ")"
EndFunction

String Function GetTakeoverStyleLabel(Int style)
    style = NormalizeTakeoverStyle(style)
    if style == TAKEOVER_STYLE_STEP_AND_FORWARD_ROLL
        return "Step and Forward Roll"
    elseif style == TAKEOVER_STYLE_FULL_STEP
        return "Full Step"
    endif
    return "TK Default (Full Roll)"
EndFunction

Function EnsureTakeoverActivationInvariant()
    if !IsTakeoverEnabled()
        return
    endif

    Bool changed = False
    if DodgeBlinkNative.GetIntConfig("ActivationMode") != ACTIVATION_MODE_HOTKEY
        DodgeBlinkNative.SetIntConfig("ActivationMode", ACTIVATION_MODE_HOTKEY)
        changed = True
    endif
    if DodgeBlinkNative.GetIntConfig("AllowMultipleActivationTypes") != 0
        DodgeBlinkNative.SetIntConfig("AllowMultipleActivationTypes", 0)
        changed = True
    endif
    if changed
        DodgeBlinkNative.SaveConfig()
    endif
EndFunction

String Function GetActivationModeLabel(Int mode)
    mode = NormalizeActivationMode(mode)
    if mode == ACTIVATION_MODE_SPELL
        return "Magic"
    elseif mode == ACTIVATION_MODE_HOTKEY
        return "Hotkey"
    endif
    return "Shout"
EndFunction

Function EnsureShoutWordsKnown()
    if GetActivationMode() != ACTIVATION_MODE_SHOUT && !GetAllowMultipleTypes()
        return
    endif
    if _shoutWordsTeachDone
        return
    endif

    WordOfPower[] uniqueWords = new WordOfPower[3]
    Int uniqueCount = 0
    Int idx = 1
    Int foundCount = 0
    while idx <= 3
        WordOfPower word = DodgeBlinkNative.GetNadsShoutWord(idx)
        if word
            foundCount += 1
            Bool duplicate = False
            Int j = 0
            while j < uniqueCount
                if uniqueWords[j] == word
                    duplicate = True
                    j = uniqueCount
                else
                    j += 1
                endif
            endwhile

            if !duplicate
                uniqueWords[uniqueCount] = word
                uniqueCount += 1
            endif
        endif
        idx += 1
    endwhile

    Int teachIdx = 0
    while teachIdx < uniqueCount
        Game.TeachWord(uniqueWords[teachIdx])
        teachIdx += 1
    endwhile

    if foundCount >= 3
        _shoutWordsTeachDone = True
    endif
EndFunction

Event OnConfigInit()
    InitializeConfigState()
    EnsureTakeoverActivationInvariant()
    EnsureShoutWordsKnown()
    Debug.Trace("[DodgeBlink_MCN] OnConfigInit ModName=" + ModName)
EndEvent

Event OnInit()
    InitializeConfigState()
    EnsureTakeoverActivationInvariant()
    EnsureShoutWordsKnown()
    Debug.Trace("[DodgeBlink_MCN] OnInit -> calling Parent.OnInit()")
    ; Preserve SKI_QuestBase startup logic (version/register maintenance).
    Parent.OnInit()
EndEvent

Event OnConfigRegister()
    InitializeConfigState()
    EnsureTakeoverActivationInvariant()
    EnsureShoutWordsKnown()
    Debug.Trace("[DodgeBlink_MCN] OnConfigRegister ModName=" + ModName)
EndEvent

Event OnVersionUpdate(Int aVersion)
    InitializeConfigState()
    EnsureTakeoverActivationInvariant()
    EnsureShoutWordsKnown()
    Debug.Trace("[DodgeBlink_MCN] OnVersionUpdate from=" + aVersion)
EndEvent

Event OnConfigClose()
    DodgeBlinkNative.SaveConfig()
EndEvent

Event OnGameReload()
    InitializeConfigState()
    EnsureTakeoverActivationInvariant()
    EnsureShoutWordsKnown()
    Debug.Trace("[DodgeBlink_MCN] OnGameReload -> calling Parent.OnGameReload()")
    ; Important: keep SkyUI reload maintenance/registration behavior.
    Parent.OnGameReload()
EndEvent

Event OnPageReset(String page)
    SetCursorFillMode(TOP_TO_BOTTOM)
    EnsureTakeoverActivationInvariant()
    Int activationMode = GetActivationMode()
    Bool takeoverEnabled = IsTakeoverEnabled()
    Int takeoverProvider = GetTakeoverProvider()
    Bool tkTakeoverEnabled = takeoverEnabled && takeoverProvider == TAKEOVER_PROVIDER_TK
    Bool dmcoTakeoverEnabled = takeoverEnabled && takeoverProvider == TAKEOVER_PROVIDER_DMCO
    if page == "TK Takeover"
        AddHeaderOption("TK Takeover")
        AddToggleOptionST("TAKEOVER_TK_ENABLED", "Enable TK Takeover", tkTakeoverEnabled)
        AddToggleOptionST("TAKEOVER_ALLOW_SHEATHED", "Allow Sheathed", DodgeBlinkNative.GetIntConfig("TakeoverAllowSheathed") != 0)
        AddMenuOptionST("TAKEOVER_STYLE", "Style Selector", GetTakeoverStyleLabel(GetTakeoverStyle()))
        AddHeaderOption("Dependencies")
        AddTextOption("TK Dodge for RE - For Animation", "")
        return
    endif
    if page == "DMCO Takeover"
        AddHeaderOption("DMCO Takeover")
        AddToggleOptionST("TAKEOVER_DMCO_ENABLED", "Enable DMCO Takeover", dmcoTakeoverEnabled)
        AddToggleOptionST("TAKEOVER_ALLOW_SHEATHED", "Allow Sheathed", DodgeBlinkNative.GetIntConfig("TakeoverAllowSheathed") != 0)
        AddMenuOptionST("TAKEOVER_DMCO_STYLE", "Style Selector", GetDmcoTakeoverStyleLabel(GetDmcoTakeoverStyle()))
        AddHeaderOption("Dependencies")
        AddTextOption("Dodge MCO-DXP - For Animation", "")
        AddTextOption("Nemesis output with DMCO patch - Required", "")
        AddTextOption("8-Way Input Routing - Enabled", "")
        AddTextOption("MO2 Priority: DMCO above NADS", "")
        AddTextOption("DMCO MCM pages are locked by NADS", "")
        return
    endif

    if page != "" && page != "Main"
        return
    endif

    EnsureShoutWordsKnown()

    AddHeaderOption("Status")
    AddHeaderOption("Plugin Version: " + DodgeBlinkNative.GetVersion())
    String takeoverStatusLabel = "Disabled"
    if takeoverEnabled
        takeoverStatusLabel = "Enabled (" + GetTakeoverProviderLabel(takeoverProvider) + ")"
    endif
    AddTextOption("Takeover Status", takeoverStatusLabel)

    AddHeaderOption("Activation")
    Bool allowMultipleTypes = GetAllowMultipleTypes()
    Int activationFlags = 0
    Int multipleFlags = 0
    String activationLabel = GetActivationModeLabel(activationMode)
    if takeoverEnabled
        activationLabel = GetTakeoverLockLabel()
        allowMultipleTypes = False
        activationFlags = OPTION_FLAG_DISABLED
        multipleFlags = OPTION_FLAG_DISABLED
    endif
    AddMenuOptionST("ACTIVATION_MODE", "Activation Mode", activationLabel, activationFlags)
    AddToggleOptionST("ALLOW_MULTIPLE_TYPES", "Allow Multiple Activation Types", allowMultipleTypes, multipleFlags)
    if activationMode == ACTIVATION_MODE_HOTKEY
        AddKeyMapOptionST("HOTKEY_BIND", "Dodge Hotkey", DodgeBlinkNative.GetIntConfig("HotkeyCode"), OPTION_FLAG_WITH_UNMAP)
        AddSliderOptionST("HOTKEY_TIER", "Hotkey Tier", DodgeBlinkNative.GetIntConfig("HotkeyTier") as Float, "{0}")
        AddSliderOptionST("HOTKEY_TAP_MAX_MS", "Hotkey Tap Window (ms)", DodgeBlinkNative.GetIntConfig("HotkeyTapMaxMs") as Float, "{0}")
    elseif activationMode == ACTIVATION_MODE_SPELL
        AddTextOption("Spell Mode", "Use NADS dodge spell/power records")
    else
        AddTextOption("Shout Mode", "Use NADS shout record")
    endif

    AddHeaderOption("General")
    AddToggleOptionST("DODGE_IFRAMES_ENABLED", "Damage Immunity During Dodge", DodgeBlinkNative.GetIntConfig("DodgeIFramesEnabled") != 0)
    AddHeaderOption("Mode Costs")
    AddSliderOptionST("HOTKEY_STAMINA_COST", "Hotkey Stamina Cost", DodgeBlinkNative.GetFloatConfig("HotkeyStaminaCost"), "{0}")
    AddSliderOptionST("SPELL_MAGICKA_COST1", "Spell Magicka Cost T1", DodgeBlinkNative.GetFloatConfig("SpellMagickaCost1"), "{0}")
    AddSliderOptionST("SPELL_MAGICKA_COST2", "Spell Magicka Cost T2", DodgeBlinkNative.GetFloatConfig("SpellMagickaCost2"), "{0}")
    AddSliderOptionST("SPELL_MAGICKA_COST3", "Spell Magicka Cost T3", DodgeBlinkNative.GetFloatConfig("SpellMagickaCost3"), "{0}")
    AddHeaderOption("Shout Cooldowns")
    AddSliderOptionST("SHOUT_COOLDOWN1", "Shout Cooldown T1", DodgeBlinkNative.GetFloatConfig("ShoutCooldown1"), "{2}")
    AddSliderOptionST("SHOUT_COOLDOWN2", "Shout Cooldown T2", DodgeBlinkNative.GetFloatConfig("ShoutCooldown2"), "{2}")
    AddSliderOptionST("SHOUT_COOLDOWN3", "Shout Cooldown T3", DodgeBlinkNative.GetFloatConfig("ShoutCooldown3"), "{2}")

    AddSliderOptionST("INPUT_LATCH_WINDOW_MS", "Shout Input Latch Window (ms)", DodgeBlinkNative.GetIntConfig("InputLatchWindowMs") as Float, "{0}")

    AddHeaderOption("Tier Distances")
    AddSliderOptionST("DIST1", "Tier 1 Distance", DodgeBlinkNative.GetFloatConfig("Dist1"), "{1}")
    AddSliderOptionST("DIST2", "Tier 2 Distance", DodgeBlinkNative.GetFloatConfig("Dist2"), "{1}")
    AddSliderOptionST("DIST3", "Tier 3 Distance", DodgeBlinkNative.GetFloatConfig("Dist3"), "{1}")

    AddHeaderOption("Tier Cooldowns (Spell/Hotkey)")
    AddSliderOptionST("CD1", "Tier 1 Cooldown", DodgeBlinkNative.GetFloatConfig("CD1"), "{2}")
    AddSliderOptionST("CD2", "Tier 2 Cooldown", DodgeBlinkNative.GetFloatConfig("CD2"), "{2}")
    AddSliderOptionST("CD3", "Tier 3 Cooldown", DodgeBlinkNative.GetFloatConfig("CD3"), "{2}")

    AddHeaderOption("Tier Durations")
    AddSliderOptionST("DASHTIME1", "Tier 1 Dash Time", DodgeBlinkNative.GetFloatConfig("DashTime1"), "{2}")
    AddSliderOptionST("DASHTIME2", "Tier 2 Dash Time", DodgeBlinkNative.GetFloatConfig("DashTime2"), "{2}")
    AddSliderOptionST("DASHTIME3", "Tier 3 Dash Time", DodgeBlinkNative.GetFloatConfig("DashTime3"), "{2}")
EndEvent

Bool Function SetIntAndSave(String settingKey, Int value)
    Bool ok = DodgeBlinkNative.SetIntConfig(settingKey, value)
    if ok
        DodgeBlinkNative.SaveConfig()
    endif
    return ok
EndFunction

Bool Function SetFloatAndSave(String settingKey, Float value)
    Bool ok = DodgeBlinkNative.SetFloatConfig(settingKey, value)
    if ok
        DodgeBlinkNative.SaveConfig()
    endif
    return ok
EndFunction

Int Function BoolToInt(Bool value)
    if value
        return 1
    endif
    return 0
EndFunction

; State callback stubs required by Papyrus when using state-specific handlers.
Event OnSelectST()
EndEvent

Event OnDefaultST()
EndEvent

Event OnSliderOpenST()
EndEvent

Event OnSliderAcceptST(Float value)
EndEvent

Event OnMenuOpenST()
EndEvent

Event OnMenuAcceptST(Int index)
EndEvent

Event OnKeyMapChangeST(Int keyCode, String conflictControl, String conflictName)
EndEvent

; ---------- Main ----------

State ACTIVATION_MODE
    Event OnMenuOpenST()
        if IsTakeoverEnabled()
            String[] takeoverOptions = new String[1]
            takeoverOptions[0] = GetActivationModeLabel(ACTIVATION_MODE_HOTKEY)
            SetMenuDialogOptions(takeoverOptions)
            SetMenuDialogStartIndex(0)
            SetMenuDialogDefaultIndex(0)
            return
        endif

        String[] options = new String[3]
        options[0] = GetActivationModeLabel(ACTIVATION_MODE_SHOUT)
        options[1] = GetActivationModeLabel(ACTIVATION_MODE_SPELL)
        options[2] = GetActivationModeLabel(ACTIVATION_MODE_HOTKEY)
        SetMenuDialogOptions(options)
        SetMenuDialogStartIndex(GetActivationMode())
        SetMenuDialogDefaultIndex(ACTIVATION_MODE_HOTKEY)
    EndEvent

    Event OnMenuAcceptST(Int index)
        if IsTakeoverEnabled()
            SetIntAndSave("ActivationMode", ACTIVATION_MODE_HOTKEY)
            SetMenuOptionValueST(GetTakeoverLockLabel())
            ForcePageReset()
            return
        endif

        Int activationMode = NormalizeActivationMode(index)
        SetIntAndSave("ActivationMode", activationMode)
        SetMenuOptionValueST(GetActivationModeLabel(activationMode))
        if activationMode == ACTIVATION_MODE_SHOUT
            EnsureShoutWordsKnown()
        endif
        ForcePageReset()
    EndEvent

    Event OnDefaultST()
        if IsTakeoverEnabled()
            SetIntAndSave("ActivationMode", ACTIVATION_MODE_HOTKEY)
            SetMenuOptionValueST(GetTakeoverLockLabel())
            ForcePageReset()
            return
        endif

        SetIntAndSave("ActivationMode", ACTIVATION_MODE_HOTKEY)
        SetMenuOptionValueST(GetActivationModeLabel(ACTIVATION_MODE_HOTKEY))
        ForcePageReset()
    EndEvent
EndState

State ALLOW_MULTIPLE_TYPES
    Event OnSelectST()
        if IsTakeoverEnabled()
            SetIntAndSave("AllowMultipleActivationTypes", 0)
            SetToggleOptionValueST(False)
            ForcePageReset()
            return
        endif

        Bool enabled = DodgeBlinkNative.GetIntConfig("AllowMultipleActivationTypes") == 0
        SetIntAndSave("AllowMultipleActivationTypes", BoolToInt(enabled))
        SetToggleOptionValueST(enabled)
        if enabled
            EnsureShoutWordsKnown()
        endif
        ForcePageReset()
    EndEvent

    Event OnDefaultST()
        if IsTakeoverEnabled()
            SetIntAndSave("AllowMultipleActivationTypes", 0)
            SetToggleOptionValueST(False)
            ForcePageReset()
            return
        endif

        SetIntAndSave("AllowMultipleActivationTypes", 0)
        SetToggleOptionValueST(False)
        ForcePageReset()
    EndEvent
EndState

State HOTKEY_BIND
    Event OnKeyMapChangeST(Int keyCode, String conflictControl, String conflictName)
        if conflictControl != ""
            String conflictLabel = conflictControl
            if conflictName != ""
                conflictLabel = conflictControl + " (" + conflictName + ")"
            endif
            Bool continueChange = ShowMessage("Selected key conflicts with " + conflictLabel + ". Continue?")
            if !continueChange
                return
            endif
        endif

        SetIntAndSave("HotkeyCode", keyCode)
        SetKeyMapOptionValueST(keyCode)
    EndEvent

    Event OnDefaultST()
        SetIntAndSave("HotkeyCode", -1)
        SetKeyMapOptionValueST(-1)
    EndEvent
EndState

State HOTKEY_TIER
    Event OnSliderOpenST()
        SetSliderDialogStartValue(DodgeBlinkNative.GetIntConfig("HotkeyTier") as Float)
        SetSliderDialogDefaultValue(1.0)
        SetSliderDialogRange(1.0, 3.0)
        SetSliderDialogInterval(1.0)
    EndEvent

    Event OnSliderAcceptST(Float value)
        Int tier = value as Int
        if tier < 1
            tier = 1
        elseif tier > 3
            tier = 3
        endif
        SetIntAndSave("HotkeyTier", tier)
        SetSliderOptionValueST(tier as Float, "{0}")
    EndEvent

    Event OnDefaultST()
        SetIntAndSave("HotkeyTier", 1)
        SetSliderOptionValueST(1.0, "{0}")
    EndEvent
EndState

State HOTKEY_TAP_MAX_MS
    Event OnSliderOpenST()
        SetSliderDialogStartValue(DodgeBlinkNative.GetIntConfig("HotkeyTapMaxMs") as Float)
        SetSliderDialogDefaultValue(200.0)
        SetSliderDialogRange(50.0, 1000.0)
        SetSliderDialogInterval(10.0)
    EndEvent

    Event OnSliderAcceptST(Float value)
        Int tapMaxMs = value as Int
        if tapMaxMs < 50
            tapMaxMs = 50
        elseif tapMaxMs > 1000
            tapMaxMs = 1000
        endif
        SetIntAndSave("HotkeyTapMaxMs", tapMaxMs)
        SetSliderOptionValueST(tapMaxMs as Float, "{0}")
    EndEvent

    Event OnDefaultST()
        SetIntAndSave("HotkeyTapMaxMs", 200)
        SetSliderOptionValueST(200.0, "{0}")
    EndEvent
EndState

State HOTKEY_STAMINA_COST
    Event OnSliderOpenST()
        SetSliderDialogStartValue(DodgeBlinkNative.GetFloatConfig("HotkeyStaminaCost"))
        SetSliderDialogDefaultValue(20.0)
        SetSliderDialogRange(0.0, 500.0)
        SetSliderDialogInterval(1.0)
    EndEvent

    Event OnSliderAcceptST(Float value)
        SetFloatAndSave("HotkeyStaminaCost", value)
        SetSliderOptionValueST(value, "{0}")
    EndEvent

    Event OnDefaultST()
        SetFloatAndSave("HotkeyStaminaCost", 20.0)
        SetSliderOptionValueST(20.0, "{0}")
    EndEvent
EndState

State SPELL_MAGICKA_COST1
    Event OnSliderOpenST()
        SetSliderDialogStartValue(DodgeBlinkNative.GetFloatConfig("SpellMagickaCost1"))
        SetSliderDialogDefaultValue(20.0)
        SetSliderDialogRange(0.0, 500.0)
        SetSliderDialogInterval(1.0)
    EndEvent

    Event OnSliderAcceptST(Float value)
        SetFloatAndSave("SpellMagickaCost1", value)
        SetSliderOptionValueST(value, "{0}")
    EndEvent

    Event OnDefaultST()
        SetFloatAndSave("SpellMagickaCost1", 20.0)
        SetSliderOptionValueST(20.0, "{0}")
    EndEvent
EndState

State SPELL_MAGICKA_COST2
    Event OnSliderOpenST()
        SetSliderDialogStartValue(DodgeBlinkNative.GetFloatConfig("SpellMagickaCost2"))
        SetSliderDialogDefaultValue(25.0)
        SetSliderDialogRange(0.0, 500.0)
        SetSliderDialogInterval(1.0)
    EndEvent

    Event OnSliderAcceptST(Float value)
        SetFloatAndSave("SpellMagickaCost2", value)
        SetSliderOptionValueST(value, "{0}")
    EndEvent

    Event OnDefaultST()
        SetFloatAndSave("SpellMagickaCost2", 25.0)
        SetSliderOptionValueST(25.0, "{0}")
    EndEvent
EndState

State SPELL_MAGICKA_COST3
    Event OnSliderOpenST()
        SetSliderDialogStartValue(DodgeBlinkNative.GetFloatConfig("SpellMagickaCost3"))
        SetSliderDialogDefaultValue(30.0)
        SetSliderDialogRange(0.0, 500.0)
        SetSliderDialogInterval(1.0)
    EndEvent

    Event OnSliderAcceptST(Float value)
        SetFloatAndSave("SpellMagickaCost3", value)
        SetSliderOptionValueST(value, "{0}")
    EndEvent

    Event OnDefaultST()
        SetFloatAndSave("SpellMagickaCost3", 30.0)
        SetSliderOptionValueST(30.0, "{0}")
    EndEvent
EndState

State SHOUT_COOLDOWN1
    Event OnSliderOpenST()
        SetSliderDialogStartValue(DodgeBlinkNative.GetFloatConfig("ShoutCooldown1"))
        SetSliderDialogDefaultValue(3.0)
        SetSliderDialogRange(0.0, 120.0)
        SetSliderDialogInterval(0.10)
    EndEvent

    Event OnSliderAcceptST(Float value)
        SetFloatAndSave("ShoutCooldown1", value)
        SetSliderOptionValueST(value, "{2}")
    EndEvent

    Event OnDefaultST()
        SetFloatAndSave("ShoutCooldown1", 3.0)
        SetSliderOptionValueST(3.0, "{2}")
    EndEvent
EndState

State SHOUT_COOLDOWN2
    Event OnSliderOpenST()
        SetSliderDialogStartValue(DodgeBlinkNative.GetFloatConfig("ShoutCooldown2"))
        SetSliderDialogDefaultValue(4.0)
        SetSliderDialogRange(0.0, 120.0)
        SetSliderDialogInterval(0.10)
    EndEvent

    Event OnSliderAcceptST(Float value)
        SetFloatAndSave("ShoutCooldown2", value)
        SetSliderOptionValueST(value, "{2}")
    EndEvent

    Event OnDefaultST()
        SetFloatAndSave("ShoutCooldown2", 4.0)
        SetSliderOptionValueST(4.0, "{2}")
    EndEvent
EndState

State SHOUT_COOLDOWN3
    Event OnSliderOpenST()
        SetSliderDialogStartValue(DodgeBlinkNative.GetFloatConfig("ShoutCooldown3"))
        SetSliderDialogDefaultValue(5.0)
        SetSliderDialogRange(0.0, 120.0)
        SetSliderDialogInterval(0.10)
    EndEvent

    Event OnSliderAcceptST(Float value)
        SetFloatAndSave("ShoutCooldown3", value)
        SetSliderOptionValueST(value, "{2}")
    EndEvent

    Event OnDefaultST()
        SetFloatAndSave("ShoutCooldown3", 5.0)
        SetSliderOptionValueST(5.0, "{2}")
    EndEvent
EndState

State INPUT_LATCH_WINDOW_MS
    Event OnSliderOpenST()
        SetSliderDialogStartValue(DodgeBlinkNative.GetIntConfig("InputLatchWindowMs") as Float)
        SetSliderDialogDefaultValue(250.0)
        SetSliderDialogRange(0.0, 2000.0)
        SetSliderDialogInterval(10.0)
    EndEvent

    Event OnSliderAcceptST(Float value)
        Int windowMs = value as Int
        SetIntAndSave("InputLatchWindowMs", windowMs)
        SetSliderOptionValueST(windowMs as Float, "{0}")
    EndEvent

    Event OnDefaultST()
        SetIntAndSave("InputLatchWindowMs", 250)
        SetSliderOptionValueST(250.0, "{0}")
    EndEvent
EndState

State DODGE_IFRAMES_ENABLED
    Event OnSelectST()
        Bool enabled = DodgeBlinkNative.GetIntConfig("DodgeIFramesEnabled") == 0
        SetIntAndSave("DodgeIFramesEnabled", BoolToInt(enabled))
        SetToggleOptionValueST(enabled)
    EndEvent

    Event OnDefaultST()
        SetIntAndSave("DodgeIFramesEnabled", 1)
        SetToggleOptionValueST(True)
    EndEvent
EndState

State DIST1
    Event OnSliderOpenST()
        SetSliderDialogStartValue(DodgeBlinkNative.GetFloatConfig("Dist1"))
        SetSliderDialogDefaultValue(175.0)
        SetSliderDialogRange(32.0, 2020.0)
        SetSliderDialogInterval(1.0)
    EndEvent

    Event OnSliderAcceptST(Float value)
        SetFloatAndSave("Dist1", value)
        SetSliderOptionValueST(value, "{1}")
    EndEvent

    Event OnDefaultST()
        SetFloatAndSave("Dist1", 175.0)
        SetSliderOptionValueST(175.0, "{1}")
    EndEvent
EndState

State DIST2
    Event OnSliderOpenST()
        SetSliderDialogStartValue(DodgeBlinkNative.GetFloatConfig("Dist2"))
        SetSliderDialogDefaultValue(275.0)
        SetSliderDialogRange(32.0, 2020.0)
        SetSliderDialogInterval(1.0)
    EndEvent

    Event OnSliderAcceptST(Float value)
        SetFloatAndSave("Dist2", value)
        SetSliderOptionValueST(value, "{1}")
    EndEvent

    Event OnDefaultST()
        SetFloatAndSave("Dist2", 275.0)
        SetSliderOptionValueST(275.0, "{1}")
    EndEvent
EndState

State DIST3
    Event OnSliderOpenST()
        SetSliderDialogStartValue(DodgeBlinkNative.GetFloatConfig("Dist3"))
        SetSliderDialogDefaultValue(375.0)
        SetSliderDialogRange(32.0, 2020.0)
        SetSliderDialogInterval(1.0)
    EndEvent

    Event OnSliderAcceptST(Float value)
        SetFloatAndSave("Dist3", value)
        SetSliderOptionValueST(value, "{1}")
    EndEvent

    Event OnDefaultST()
        SetFloatAndSave("Dist3", 375.0)
        SetSliderOptionValueST(375.0, "{1}")
    EndEvent
EndState

State CD1
    Event OnSliderOpenST()
        SetSliderDialogStartValue(DodgeBlinkNative.GetFloatConfig("CD1"))
        SetSliderDialogDefaultValue(1.25)
        SetSliderDialogRange(0.0, 120.0)
        SetSliderDialogInterval(0.05)
    EndEvent

    Event OnSliderAcceptST(Float value)
        SetFloatAndSave("CD1", value)
        SetSliderOptionValueST(value, "{2}")
    EndEvent

    Event OnDefaultST()
        SetFloatAndSave("CD1", 1.25)
        SetSliderOptionValueST(1.25, "{2}")
    EndEvent
EndState

State CD2
    Event OnSliderOpenST()
        SetSliderDialogStartValue(DodgeBlinkNative.GetFloatConfig("CD2"))
        SetSliderDialogDefaultValue(2.50)
        SetSliderDialogRange(0.0, 120.0)
        SetSliderDialogInterval(0.05)
    EndEvent

    Event OnSliderAcceptST(Float value)
        SetFloatAndSave("CD2", value)
        SetSliderOptionValueST(value, "{2}")
    EndEvent

    Event OnDefaultST()
        SetFloatAndSave("CD2", 2.50)
        SetSliderOptionValueST(2.50, "{2}")
    EndEvent
EndState

State CD3
    Event OnSliderOpenST()
        SetSliderDialogStartValue(DodgeBlinkNative.GetFloatConfig("CD3"))
        SetSliderDialogDefaultValue(4.00)
        SetSliderDialogRange(0.0, 120.0)
        SetSliderDialogInterval(0.05)
    EndEvent

    Event OnSliderAcceptST(Float value)
        SetFloatAndSave("CD3", value)
        SetSliderOptionValueST(value, "{2}")
    EndEvent

    Event OnDefaultST()
        SetFloatAndSave("CD3", 4.00)
        SetSliderOptionValueST(4.00, "{2}")
    EndEvent
EndState

State DASHTIME1
    Event OnSliderOpenST()
        SetSliderDialogStartValue(DodgeBlinkNative.GetFloatConfig("DashTime1"))
        SetSliderDialogDefaultValue(0.30)
        SetSliderDialogRange(0.10, 2.00)
        SetSliderDialogInterval(0.01)
    EndEvent

    Event OnSliderAcceptST(Float value)
        SetFloatAndSave("DashTime1", value)
        SetSliderOptionValueST(value, "{2}")
    EndEvent

    Event OnDefaultST()
        SetFloatAndSave("DashTime1", 0.30)
        SetSliderOptionValueST(0.30, "{2}")
    EndEvent
EndState

State DASHTIME2
    Event OnSliderOpenST()
        SetSliderDialogStartValue(DodgeBlinkNative.GetFloatConfig("DashTime2"))
        SetSliderDialogDefaultValue(0.38)
        SetSliderDialogRange(0.10, 2.00)
        SetSliderDialogInterval(0.01)
    EndEvent

    Event OnSliderAcceptST(Float value)
        SetFloatAndSave("DashTime2", value)
        SetSliderOptionValueST(value, "{2}")
    EndEvent

    Event OnDefaultST()
        SetFloatAndSave("DashTime2", 0.38)
        SetSliderOptionValueST(0.38, "{2}")
    EndEvent
EndState

State DASHTIME3
    Event OnSliderOpenST()
        SetSliderDialogStartValue(DodgeBlinkNative.GetFloatConfig("DashTime3"))
        SetSliderDialogDefaultValue(0.46)
        SetSliderDialogRange(0.10, 2.00)
        SetSliderDialogInterval(0.01)
    EndEvent

    Event OnSliderAcceptST(Float value)
        SetFloatAndSave("DashTime3", value)
        SetSliderOptionValueST(value, "{2}")
    EndEvent

    Event OnDefaultST()
        SetFloatAndSave("DashTime3", 0.46)
        SetSliderOptionValueST(0.46, "{2}")
    EndEvent
EndState

; ---------- TK Takeover ----------

State TAKEOVER_TK_ENABLED
    Event OnSelectST()
        Bool enabled = !IsTakeoverEnabled() || GetTakeoverProvider() != TAKEOVER_PROVIDER_TK
        if enabled
            SetIntAndSave("TakeoverProvider", TAKEOVER_PROVIDER_TK)
            SetIntAndSave("TakeoverEnabled", 1)
            SetIntAndSave("ActivationMode", ACTIVATION_MODE_HOTKEY)
            SetIntAndSave("AllowMultipleActivationTypes", 0)
        elseif IsTakeoverEnabled() && GetTakeoverProvider() == TAKEOVER_PROVIDER_TK
            SetIntAndSave("TakeoverEnabled", 0)
        endif
        SetToggleOptionValueST(enabled)
        ForcePageReset()
    EndEvent

    Event OnDefaultST()
        if IsTakeoverEnabled() && GetTakeoverProvider() == TAKEOVER_PROVIDER_TK
            SetIntAndSave("TakeoverEnabled", 0)
        endif
        SetToggleOptionValueST(False)
        ForcePageReset()
    EndEvent
EndState

State TAKEOVER_DMCO_ENABLED
    Event OnSelectST()
        Bool enabled = !IsTakeoverEnabled() || GetTakeoverProvider() != TAKEOVER_PROVIDER_DMCO
        if enabled
            SetIntAndSave("TakeoverProvider", TAKEOVER_PROVIDER_DMCO)
            SetIntAndSave("TakeoverEnabled", 1)
            SetIntAndSave("ActivationMode", ACTIVATION_MODE_HOTKEY)
            SetIntAndSave("AllowMultipleActivationTypes", 0)
        elseif IsTakeoverEnabled() && GetTakeoverProvider() == TAKEOVER_PROVIDER_DMCO
            SetIntAndSave("TakeoverEnabled", 0)
        endif
        SetToggleOptionValueST(enabled)
        ForcePageReset()
    EndEvent

    Event OnDefaultST()
        if IsTakeoverEnabled() && GetTakeoverProvider() == TAKEOVER_PROVIDER_DMCO
            SetIntAndSave("TakeoverEnabled", 0)
        endif
        SetToggleOptionValueST(False)
        ForcePageReset()
    EndEvent
EndState

State TAKEOVER_ALLOW_SHEATHED
    Event OnSelectST()
        Bool enabled = DodgeBlinkNative.GetIntConfig("TakeoverAllowSheathed") == 0
        SetIntAndSave("TakeoverAllowSheathed", BoolToInt(enabled))
        SetToggleOptionValueST(enabled)
    EndEvent

    Event OnDefaultST()
        SetIntAndSave("TakeoverAllowSheathed", 0)
        SetToggleOptionValueST(False)
    EndEvent
EndState

State TAKEOVER_STYLE
    Event OnMenuOpenST()
        String[] options = new String[3]
        options[0] = GetTakeoverStyleLabel(TAKEOVER_STYLE_TK_DEFAULT)
        options[1] = GetTakeoverStyleLabel(TAKEOVER_STYLE_STEP_AND_FORWARD_ROLL)
        options[2] = GetTakeoverStyleLabel(TAKEOVER_STYLE_FULL_STEP)
        SetMenuDialogOptions(options)
        SetMenuDialogStartIndex(GetTakeoverStyle())
        SetMenuDialogDefaultIndex(TAKEOVER_STYLE_TK_DEFAULT)
    EndEvent

    Event OnMenuAcceptST(Int index)
        Int style = NormalizeTakeoverStyle(index)
        SetIntAndSave("TakeoverStyle", style)
        SetMenuOptionValueST(GetTakeoverStyleLabel(style))
    EndEvent

    Event OnDefaultST()
        SetIntAndSave("TakeoverStyle", TAKEOVER_STYLE_TK_DEFAULT)
        SetMenuOptionValueST(GetTakeoverStyleLabel(TAKEOVER_STYLE_TK_DEFAULT))
    EndEvent
EndState

State TAKEOVER_DMCO_STYLE
    Event OnMenuOpenST()
        String[] options = new String[2]
        options[0] = GetDmcoTakeoverStyleLabel(DMCO_TAKEOVER_STYLE_SET_1)
        options[1] = GetDmcoTakeoverStyleLabel(DMCO_TAKEOVER_STYLE_SET_2)
        SetMenuDialogOptions(options)
        SetMenuDialogStartIndex(GetDmcoTakeoverStyle())
        SetMenuDialogDefaultIndex(DMCO_TAKEOVER_STYLE_SET_1)
    EndEvent

    Event OnMenuAcceptST(Int index)
        Int style = NormalizeDmcoTakeoverStyle(index)
        SetIntAndSave("DmcoTakeoverStyle", style)
        SetMenuOptionValueST(GetDmcoTakeoverStyleLabel(style))
    EndEvent

    Event OnDefaultST()
        SetIntAndSave("DmcoTakeoverStyle", DMCO_TAKEOVER_STYLE_SET_1)
        SetMenuOptionValueST(GetDmcoTakeoverStyleLabel(DMCO_TAKEOVER_STYLE_SET_1))
    EndEvent
EndState
