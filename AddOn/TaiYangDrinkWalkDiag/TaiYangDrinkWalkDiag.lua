-- TaiYangDrinkWalk v0.5.4
-- Formal user UI:
--   * SPAM is the default runtime mode.
--   * Two independent HUD switches: adaptive Tick Timer and Adaptive Resource/Spark.
--   * Item name/ID, hardware key binding, /zchihe macro entry, optional FuBar.
-- Native DrinkWalk API remains the v0.4.1/API24 implementation.
-- V0.5.2 adds Food/Drink classification, Energy-tick action timing, binding replacement/unbind,
-- and a recovery path for a FuBar icon hidden through FuBarPlugin's Enabled menu.
-- V0.5.3 adds a fixed Tick-zero marker and a brief confirmation flash so the
-- actual walk-drink timing point is easier to read at a glance.
-- Hotfix: capture only the first UnitPowerType return before calling tonumber (WoW 1.12 multi-return safe).
-- WoW 1.12 / Turtle WoW compatible Lua style (global event/arg1/this).

TaiYangDrinkWalkDiagDB = TaiYangDrinkWalkDiagDB or {}

local DW = {}
local VERSION = "0.5.4"
local PERIOD_MS = 2000
local SETTINGS_REFRESH_SEC = 0.20
local PHASE_TOLERANCE_MS = 450
local MIN_SAMPLE_GAP_MS = 700
local MAX_PHASE_ERRORS = 12
local PROVISIONAL_LOCK_SAMPLES = 3
local LOCK_TOLERANCE_MIN_MS = 120
local LOCK_TOLERANCE_MAX_MS = 280
local MAX_LOGS = 5
local MAX_RAW_LOGS = 5
local RAW_GAIN_FLASH_MS = 1800
local EXEC_CONFIRM_WINDOW_MS = 900
local UI_FSR_MS = 5000

-- Adaptive resource HUD / Turtle WoW energy model.
-- Turtle 1.18 Blade Rush uses a 2.0s base energy tick and reduces the
-- interval by 0.0006s * talent rank * agility (rank 2 = 0.0012s/agi).
-- The public pfUI-Turtle implementation uses the same formula.  We keep
-- the formula as the initial model and then phase-calibrate from real
-- +16/+20 style energy gains so the Spark follows the client/server rhythm.
local ENERGY_BASE_TICK_MS = 2000
local ENERGY_MIN_TICK_MS = 500
local ENERGY_MAX_TICK_MS = 2200
local ENERGY_REDUCTION_MS_PER_AGI_PER_RANK = 0.6
local ENERGY_GAIN_MIN = 14
local ENERGY_GAIN_MAX = 22
local ENERGY_SAMPLE_MIN_MS = 350
local ENERGY_SAMPLE_MAX_MULT = 4.5
local ENERGY_SAMPLE_TOLERANCE_MIN_MS = 140
local ENERGY_SAMPLE_TOLERANCE_MAX_MS = 360
local ENERGY_SAMPLE_COUNT = 8

local UI_MANA_BAR_WIDTH = 306
local UI_MANA_BAR_HEIGHT = 12
local UI_TICK_BAR_WIDTH = 306
local UI_TICK_BAR_HEIGHT = 26

local DEFAULT_WATCH_LEAD_MS = 220
local DEFAULT_SPAM_OPEN_MS = 320
local DEFAULT_SPAM_CLOSE_MS = 170
local DEFAULT_SPAM_RETRY_MS = 75
local DEFAULT_DRINK_OPEN_PCT = 84.0
local DEFAULT_DRINK_CLOSE_PCT = 91.5
local DEFAULT_AUTO_LEAD_MS = 320
local AUTO_LEAD_MIN_MS = 200
local AUTO_LEAD_MAX_MS = 380
local AUTO_LEAD_STEP_DOWN_MS = 10
local AUTO_LEAD_STEP_UP_MS = 20
local AUTO_LEAD_SUCCESS_STREAK = 6
local AUTO_LEAD_MISS_STREAK = 2
local AUTO_LEAD_MAX_JITTER_FOR_DOWN_MS = 140

BINDING_HEADER_TAIYANGDRINKWALK = "太阳神殿 DrinkWalk"
BINDING_NAME_TAIYANGDRINKWALK_ACTION = "吃喝动作（与 /zchihe 相同）"

local function Clamp(v, lo, hi)
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

local function Round(v)
    if v >= 0 then return math.floor(v + 0.5) end
    return math.ceil(v - 0.5)
end

local function PushBounded(list, value, maxCount)
    table.insert(list, value)
    while table.getn(list) > maxCount do table.remove(list, 1) end
end

local function Median(values)
    local n = table.getn(values)
    if n == 0 then return 0 end
    local copy = {}
    local i
    for i = 1, n do copy[i] = values[i] end
    table.sort(copy)
    if (n - math.floor(n / 2) * 2) == 1 then return copy[(n + 1) / 2] end
    return (copy[n / 2] + copy[n / 2 + 1]) * 0.5
end

local function NowMS()
    return GetTime() * 1000
end

local function CharKey()
    local name = UnitName("player") or "?"
    local realm = ""
    if type(GetRealmName) == "function" then realm = GetRealmName() or "" end
    return name .. "@" .. realm
end

local function Trim(text)
    text = tostring(text or "")
    local _, _, out = string.find(text, "^%s*(.-)%s*$")
    return out or text
end

local function StateZh(s)
    if s == "UNSYNCED" then return "未同步" end
    if s == "PROVISIONAL" then return "候选同步" end
    if s == "LOCKED" then return "已锁定" end
    if s == "VERIFY" then return "等待验证" end
    return tostring(s or "?")
end

local function ModeZh()
    if DW.mode == "SPAM" then return "狂按模式 [SPAM]" end
    return "观察模式 [WATCH]"
end

local function FormatMS(ms)
    ms = tonumber(ms)
    if not ms then return "--" end
    if math.abs(ms) >= 1000 then return string.format("%.3fs", ms / 1000) end
    return string.format("%.1fms", ms)
end

local function FormatClock(ms)
    ms = tonumber(ms)
    if not ms then return "--" end
    return string.format("%.3f", ms / 1000)
end

local function SignedMana(delta)
    delta = tonumber(delta) or 0
    if delta > 0 then return "+" .. tostring(delta) end
    return tostring(delta)
end

-- ---------------------------------------------------------------------------
-- Adaptive resource layer
-- ---------------------------------------------------------------------------

local function PlayerClassToken()
    if DW.playerClassToken then return DW.playerClassToken end
    local _, token = UnitClass("player")
    DW.playerClassToken = token or ""
    return DW.playerClassToken
end

local function CurrentPowerType()
    if type(UnitPowerType) == "function" then
        -- WoW 1.12/Turtle may return more than one value here.
        -- Never pass the function call directly to tonumber(), otherwise
        -- the second return value is interpreted as tonumber's base argument.
        local rawPowerType = UnitPowerType("player")
        local p = tonumber(rawPowerType)
        if p ~= nil then return p end
    end

    -- Vanilla/Turtle exposes UnitPowerType.  These fallbacks only protect
    -- against unusual client bridges that omit it.
    local class = PlayerClassToken()
    if class == "WARRIOR" then return 1 end
    if class == "ROGUE" then return 3 end
    return 0
end

local function PowerTypeToResource(powerType)
    powerType = tonumber(powerType) or 0
    if powerType == 1 then return "RAGE" end
    if powerType == 3 then return "ENERGY" end
    return "MANA"
end

local function ResourceNameZh(kind)
    if kind == "ENERGY" then return "能量" end
    if kind == "RAGE" then return "怒气" end
    return "法力"
end

local function ResourceColor(kind)
    if kind == "ENERGY" then return 0.95, 0.76, 0.08 end
    if kind == "RAGE" then return 0.82, 0.10, 0.10 end
    return 0.05, 0.35, 0.95
end

local function ResourceBGColor(kind)
    if kind == "ENERGY" then return 0.12, 0.08, 0.01, 0.90 end
    if kind == "RAGE" then return 0.12, 0.02, 0.02, 0.90 end
    return 0.03, 0.03, 0.09, 0.88
end

local function DisplayResourceValues(kind, current, maximum)
    current = tonumber(current) or 0
    maximum = tonumber(maximum) or 0

    -- Some vanilla-compatible bridges expose rage internally as 0..1000.
    -- Normalize only the displayed numbers; the status-bar ratio is unchanged.
    if kind == "RAGE" and maximum > 100 then
        return current / 10, maximum / 10
    end
    return current, maximum
end

local function PlayerAgility()
    if type(UnitStat) ~= "function" then return 0 end
    local agility = UnitStat("player", 2)
    return tonumber(agility) or 0
end

local function BladeRushRank()
    if PlayerClassToken() ~= "ROGUE" then return 0 end
    if type(GetTalentInfo) ~= "function" then return 0 end
    local _, _, _, _, rank = GetTalentInfo(2, 16)
    rank = tonumber(rank) or 0
    if rank < 0 then rank = 0 end
    if rank > 2 then rank = 2 end
    return rank
end

local function TheoryEnergyTickMS()
    local rank = BladeRushRank()
    local agility = PlayerAgility()
    local tickMS = ENERGY_BASE_TICK_MS
    if rank > 0 then
        tickMS = tickMS - (agility * ENERGY_REDUCTION_MS_PER_AGI_PER_RANK * rank)
    end
    tickMS = Clamp(tickMS, ENERGY_MIN_TICK_MS, ENERGY_MAX_TICK_MS)
    return tickMS, rank, agility
end

local function ResetEnergyRuntime(reason)
    DW.energyAnchorMS = nil
    DW.energyLastAcceptedGainMS = nil
    DW.energySamples = {}
    DW.energyObservedTickMS = nil
    DW.energySyncReason = reason or "RESET"
end

local function RecomputeEnergyModel(force)
    local theoryMS, rank, agility = TheoryEnergyTickMS()
    local changed = force or DW.energyTheoryTickMS == nil
        or math.abs((DW.energyTheoryTickMS or theoryMS) - theoryMS) > 0.5
        or DW.energyBladeRushRank ~= rank
        or DW.energyAgility ~= agility

    if changed then
        local oldTheory = DW.energyTheoryTickMS
        DW.energyTheoryTickMS = theoryMS
        DW.energyBladeRushRank = rank
        DW.energyAgility = agility

        -- Samples gathered at a different agility/talent rank describe an old
        -- cadence.  Drop the interval estimate but keep the last phase anchor;
        -- the next real energy tick will re-lock the phase immediately.
        if oldTheory and math.abs(oldTheory - theoryMS) > 0.5 then
            DW.energySamples = {}
            DW.energyObservedTickMS = nil
        end
    end

    if DW.energyObservedTickMS then
        DW.energyTickMS = Clamp(DW.energyObservedTickMS, ENERGY_MIN_TICK_MS, ENERGY_MAX_TICK_MS)
    else
        DW.energyTickMS = theoryMS
    end
    return DW.energyTickMS
end

local function EffectiveEnergyTickMS()
    if not DW.energyTickMS then return RecomputeEnergyModel(true) end
    return Clamp(DW.energyTickMS, ENERGY_MIN_TICK_MS, ENERGY_MAX_TICK_MS)
end

local function RefreshResourceContext(force)
    local kind = PowerTypeToResource(CurrentPowerType())
    -- Capture API return values first for strict Lua 5.0 / multi-return safety.
    local rawCurrent = UnitMana("player")
    local rawMaximum = UnitManaMax("player")
    local current = tonumber(rawCurrent) or 0
    local maximum = tonumber(rawMaximum) or 0
    local changed = force or DW.resourceType ~= kind

    if changed then
        local previous = DW.resourceType
        DW.resourceType = kind
        DW.resourceEventValue = current
        DW.resourceSwitchMS = NowMS()

        if kind == "ENERGY" then
            ResetEnergyRuntime("POWER_SWITCH")
            RecomputeEnergyModel(true)
        elseif previous == "ENERGY" then
            ResetEnergyRuntime("LEAVE_ENERGY")
        end

        if kind == "MANA" then
            -- Avoid treating the first value after a druid form switch as a
            -- mana gain/spend sample.
            DW.lastMana = current
            DW.maxMana = maximum
        end
    end

    DW.resourceValue = current
    DW.resourceMax = maximum
    return kind, current, maximum, changed
end

local function EnergyNextTickMS(nowMS)
    if not DW.energyAnchorMS then return nil end
    local period = EffectiveEnergyTickMS()
    local n = math.floor((nowMS - DW.energyAnchorMS) / period) + 1
    if n < 0 then n = 0 end
    local t = DW.energyAnchorMS + n * period
    while t <= nowMS do t = t + period end
    return t, period
end

local function SetResourceGainFlash(kind, delta, timestampMS)
    if not delta or delta <= 0 then return end
    DW.resourceFlashKind = kind
    DW.resourceFlashDelta = delta
    DW.resourceFlashMS = timestampMS
    DW.resourceFlashUntilMS = timestampMS + RAW_GAIN_FLASH_MS
end

local function OnEnergyChanged(oldValue, newValue, timestampMS)
    local delta = (tonumber(newValue) or 0) - (tonumber(oldValue) or 0)
    DW.energyLastDelta = delta
    DW.energyLastEventMS = timestampMS
    if delta <= 0 then return end

    SetResourceGainFlash("ENERGY", delta, timestampMS)

    -- Natural Turtle energy ticks are +20 normally and +16 under effects that
    -- reduce energy regeneration by 20% (for example Blade Flurry).  Restrict
    -- phase-locking to this narrow window so +25/+100 instant energy effects do
    -- not move the Spark.
    if delta < ENERGY_GAIN_MIN or delta > ENERGY_GAIN_MAX then return end

    local theory = RecomputeEnergyModel(false)
    local reference = EffectiveEnergyTickMS()
    local last = DW.energyLastAcceptedGainMS

    if not last then
        DW.energyLastAcceptedGainMS = timestampMS
        DW.energyAnchorMS = timestampMS
        DW.energySyncReason = "FIRST_GAIN"
        return
    end

    local gap = timestampMS - last
    if gap < ENERGY_SAMPLE_MIN_MS then return end

    -- A tick can be invisible at/near the energy cap.  Normalize gaps that are
    -- close to 2x/3x/4x the expected period instead of losing phase lock.
    local n = Round(gap / reference)
    if n < 1 then n = 1 end
    if n > 4 then n = 4 end
    if gap > reference * ENERGY_SAMPLE_MAX_MULT then return end

    local normalized = gap / n
    local tolerance = Clamp(reference * 0.28, ENERGY_SAMPLE_TOLERANCE_MIN_MS, ENERGY_SAMPLE_TOLERANCE_MAX_MS)
    local theoryTolerance = Clamp(theory * 0.32, ENERGY_SAMPLE_TOLERANCE_MIN_MS, ENERGY_SAMPLE_TOLERANCE_MAX_MS)

    if math.abs(normalized - reference) <= tolerance or math.abs(normalized - theory) <= theoryTolerance then
        PushBounded(DW.energySamples, normalized, ENERGY_SAMPLE_COUNT)
        if table.getn(DW.energySamples) >= 2 then
            DW.energyObservedTickMS = Median(DW.energySamples)
        end
        RecomputeEnergyModel(false)
        DW.energyLastAcceptedGainMS = timestampMS
        DW.energyAnchorMS = timestampMS
        DW.energySyncReason = "REAL_GAIN"
    end
end

local function OnRageChanged(oldValue, newValue, timestampMS)
    DW.rageLastDelta = (tonumber(newValue) or 0) - (tonumber(oldValue) or 0)
    DW.rageLastEventMS = timestampMS
end

local function ResourceSummaryText()
    local kind = DW.resourceType or PowerTypeToResource(CurrentPowerType())
    local current, maximum = DisplayResourceValues(kind, DW.resourceValue or 0, DW.resourceMax or 0)
    local base = string.format("资源：%s %d/%d", ResourceNameZh(kind), Round(current), Round(maximum))
    if kind == "ENERGY" then
        return string.format("%s  Tick %.2fs", base, EffectiveEnergyTickMS() / 1000)
    end
    return base
end

local function AddLog(text)
    table.insert(DW.logs, 1, tostring(text or ""))
    while table.getn(DW.logs) > MAX_LOGS do table.remove(DW.logs) end
    if DW.logLines then
        local i
        for i = 1, MAX_LOGS do
            if DW.logLines[i] then DW.logLines[i]:SetText(DW.logs[i] or "") end
        end
    end
end

local function RefreshRawLogLines()
    if not DW.rawLogLines then return end
    local i
    for i = 1, MAX_RAW_LOGS do
        local row = DW.rawManaLogs[i]
        local line = DW.rawLogLines[i]
        if row then
            line:SetText(row.text or "")
            if row.delta and row.delta > 0 then
                line:SetTextColor(0.35, 1.00, 0.35)
            elseif row.delta and row.delta < 0 then
                line:SetTextColor(1.00, 0.48, 0.35)
            else
                line:SetTextColor(0.78, 0.78, 0.78)
            end
        else
            line:SetText("")
            line:SetTextColor(0.78, 0.78, 0.78)
        end
    end
end

local function AddRawManaLog(oldMana, newMana, maxMana, timestampMS)
    local delta = (newMana or 0) - (oldMana or 0)
    local gainGap = nil
    local class = "MANA_CHANGE"

    if delta > 0 then
        if DW.lastManaGainMS then gainGap = timestampMS - DW.lastManaGainMS end
        DW.lastManaGainMS = timestampMS
        DW.lastManaGainGapMS = gainGap
        DW.lastGainDelta = delta
        DW.lastGainMS = timestampMS
        DW.manaGainCount = (DW.manaGainCount or 0) + 1
        DW.manaFlashUntilMS = timestampMS + RAW_GAIN_FLASH_MS
        if gainGap and gainGap >= 1600 and gainGap <= 2400 then
            class = "约2秒上涨"
        elseif gainGap then
            class = "非2秒上涨"
        else
            class = "首次上涨"
        end
    elseif delta < 0 then
        class = "耗蓝"
        -- UI-only adapter state: reuse the already observed mana decrease to
        -- drive the FiveSecondRule-style reverse spark. This does not feed
        -- back into phase classification, DrinkWalk mode logic, or native API.
        DW.uiFSRStartMS = timestampMS
        DW.uiFSREndMS = timestampMS + UI_FSR_MS
    end

    DW.rawManaEventCount = (DW.rawManaEventCount or 0) + 1
    DW.lastRawManaMS = timestampMS
    DW.lastRawManaDelta = delta
    DW.lastRawManaOld = oldMana
    DW.lastRawManaNew = newMana

    local gapText = ""
    if delta > 0 and gainGap then gapText = " gainGap=" .. FormatMS(gainGap) end
    local text = string.format("t=%s %s %d->%d/%d%s [%s]",
        FormatClock(timestampMS), SignedMana(delta), oldMana or 0, newMana or 0,
        maxMana or 0, gapText, class)
    table.insert(DW.rawManaLogs, 1, {text=text, delta=delta})
    while table.getn(DW.rawManaLogs) > MAX_RAW_LOGS do table.remove(DW.rawManaLogs) end
    RefreshRawLogLines()
end

local function ResetClassifierRuntime()
    DW.phaseErrors = {}
    DW.phaseBiasMS = 0
    DW.jitterMS = 0
    DW.dynamicToleranceMS = PHASE_TOLERANCE_MS
    DW.classifierAccepted = 0
    DW.classifierRejected = 0
    DW.classifierOutlierStreak = 0
    DW.lastClassifierClass = "NONE"
    DW.lastClassifierReason = "--"
    DW.provisionalRejects = 0
end

local function ClearPhase(reason)
    DW.state = "UNSYNCED"
    DW.anchorMS = nil
    DW.lastSampleMS = nil
    DW.lastAcceptedMS = nil
    DW.lastSampleDelta = nil
    DW.lastPhaseErrorMS = nil
    DW.confidence = 0
    DW.goodSamples = 0
    DW.badSamples = 0
    DW.resumeReason = reason or "RESET"
    DW.spamTickMS = nil
    DW.spamConfirmedTickMS = nil
    DW.spamAttemptsThisTick = 0
    DW.uiFSRStartMS = nil
    DW.uiFSREndMS = nil
    ResetClassifierRuntime()
end

local function WrappedPhaseError(sampleMS, anchorMS)
    if not anchorMS then return nil end
    local n = Round((sampleMS - anchorMS) / PERIOD_MS)
    local predicted = anchorMS + n * PERIOD_MS
    return sampleMS - predicted, predicted, n
end

local function RecomputeRobustPhaseStats()
    local n = table.getn(DW.phaseErrors or {})
    if n == 0 then
        DW.phaseBiasMS = 0
        DW.jitterMS = 0
        return
    end
    local med = Median(DW.phaseErrors)
    local dev = {}
    local i
    for i = 1, n do dev[i] = math.abs(DW.phaseErrors[i] - med) end
    local mad = Median(dev)
    local sigma = mad * 1.4826
    if n < 4 then
        local sum = 0
        for i = 1, n do sum = sum + math.abs(DW.phaseErrors[i] - med) end
        local meanDev = sum / n
        if sigma < meanDev then sigma = meanDev end
    end
    DW.phaseBiasMS = med
    DW.jitterMS = sigma
end

local function DynamicToleranceMS()
    if DW.state ~= "LOCKED" then return PHASE_TOLERANCE_MS end
    local n = table.getn(DW.phaseErrors or {})
    if n < 4 then return LOCK_TOLERANCE_MAX_MS end
    local tol = 90 + (DW.jitterMS or 0) * 2.0
    return Clamp(tol, LOCK_TOLERANCE_MIN_MS, LOCK_TOLERANCE_MAX_MS)
end

local function SetClassifier(className, reason, accepted)
    DW.lastClassifierClass = className or "?"
    DW.lastClassifierReason = reason or ""
    if accepted then
        DW.classifierAccepted = (DW.classifierAccepted or 0) + 1
    else
        DW.classifierRejected = (DW.classifierRejected or 0) + 1
    end
end

local function AcceptSample(sampleMS, deltaMana, phaseError)
    DW.goodSamples = DW.goodSamples + 1
    DW.badSamples = 0
    DW.classifierOutlierStreak = 0
    DW.provisionalRejects = 0
    DW.lastPhaseErrorMS = phaseError or 0
    PushBounded(DW.phaseErrors, phaseError or 0, MAX_PHASE_ERRORS)
    RecomputeRobustPhaseStats()
    DW.dynamicToleranceMS = DynamicToleranceMS()
    DW.confidence = Clamp(DW.confidence + ((DW.state == "LOCKED") and 0.08 or 0.16), 0, 1)
    DW.lastSampleMS = sampleMS
    DW.lastAcceptedMS = sampleMS
    DW.lastSampleDelta = deltaMana
    SetClassifier("PERIODIC_ACCEPT", "phase=" .. FormatMS(phaseError or 0), true)
end

local function StartCandidate(sampleMS, deltaMana, why)
    DW.anchorMS = sampleMS
    DW.state = "PROVISIONAL"
    DW.goodSamples = 1
    DW.badSamples = 0
    DW.confidence = 0.28
    DW.lastPhaseErrorMS = 0
    DW.lastSampleMS = sampleMS
    DW.lastAcceptedMS = sampleMS
    DW.lastSampleDelta = deltaMana
    DW.phaseErrors = {0}
    DW.phaseBiasMS = 0
    DW.jitterMS = 0
    DW.dynamicToleranceMS = PHASE_TOLERANCE_MS
    DW.provisionalRejects = 0
    SetClassifier("SEED", why or "首个候选", true)
    AddLog(string.format("候选 +%d t=%s %s", deltaMana or 0, FormatClock(sampleMS), why or ""))
end

local function RejectSample(className, reason, sampleMS, deltaMana)
    DW.badSamples = (DW.badSamples or 0) + 1
    DW.classifierOutlierStreak = (DW.classifierOutlierStreak or 0) + 1
    DW.confidence = Clamp(DW.confidence - ((DW.state == "LOCKED") and 0.04 or 0.10), 0, 1)
    SetClassifier(className, reason, false)
    AddLog(string.format("拒绝 +%d %s %s", deltaMana or 0, tostring(className or "?"), tostring(reason or "")))
end

local function ProcessManaGain(sampleMS, deltaMana)
    if not DW.anchorMS or DW.state == "UNSYNCED" then
        StartCandidate(sampleMS, deltaMana, "首个样本")
        return
    end

    local acceptedGap = DW.lastAcceptedMS and (sampleMS - DW.lastAcceptedMS) or nil
    if acceptedGap and acceptedGap < MIN_SAMPLE_GAP_MS then
        RejectSample("TOO_CLOSE", "gap=" .. FormatMS(acceptedGap), sampleMS, deltaMana)
        return
    end

    local err = WrappedPhaseError(sampleMS, DW.anchorMS)
    local tolerance = DynamicToleranceMS()
    DW.dynamicToleranceMS = tolerance

    if math.abs(err) <= tolerance then
        local alpha = (DW.state == "LOCKED") and 0.08 or 0.16
        local clipped = Clamp(err, -tolerance, tolerance)
        DW.anchorMS = DW.anchorMS + clipped * alpha
        AcceptSample(sampleMS, deltaMana, err)

        if DW.state == "PROVISIONAL" then
            if DW.goodSamples >= PROVISIONAL_LOCK_SAMPLES then
                DW.state = "LOCKED"
                DW.confidence = math.max(DW.confidence, 0.78)
                DW.dynamicToleranceMS = DynamicToleranceMS()
                AddLog("三点周期一致 -> LOCKED")
            end
        elseif DW.state == "VERIFY" then
            DW.state = "LOCKED"
            DW.confidence = math.max(DW.confidence, 0.72)
            DW.dynamicToleranceMS = DynamicToleranceMS()
            AddLog("旧phase验证通过 -> LOCKED")
        end
        return
    end

    -- A single non-periodic mana gain must never knock a healthy locked phase
    -- out of lock.  This is the main classifier hardening over v0.3.
    if DW.state == "LOCKED" then
        RejectSample("PHASE_OUTLIER", "err=" .. FormatMS(err) .. " tol=" .. FormatMS(tolerance), sampleMS, deltaMana)
        return
    end

    if DW.state == "VERIFY" then
        DW.provisionalRejects = (DW.provisionalRejects or 0) + 1
        RejectSample("VERIFY_OUTLIER", "err=" .. FormatMS(err), sampleMS, deltaMana)
        if DW.provisionalRejects >= 2 then
            StartCandidate(sampleMS, deltaMana, "旧phase连续不符，重新建候选")
        end
        return
    end

    DW.provisionalRejects = (DW.provisionalRejects or 0) + 1
    RejectSample("PROVISIONAL_OUTLIER", "err=" .. FormatMS(err), sampleMS, deltaMana)
    if DW.provisionalRejects >= 2 then
        StartCandidate(sampleMS, deltaMana, "连续两次不符，重建候选")
    end
end

local function OnManaChanged(oldMana, newMana, maxMana, timestampMS, source)
    DW.lastManaOld = oldMana
    DW.lastManaNew = newMana
    DW.maxMana = maxMana
    DW.lastManaEventMS = timestampMS
    DW.lastSource = source or "LUA"
    AddRawManaLog(oldMana, newMana, maxMana, timestampMS)
    local delta = (newMana or 0) - (oldMana or 0)
    if delta > 0 then ProcessManaGain(timestampMS, delta) end
end

local function NextTickMS(nowMS)
    if not DW.anchorMS then return nil end
    local n = math.floor((nowMS - DW.anchorMS) / PERIOD_MS) + 1
    if n < 0 then n = 0 end
    local t = DW.anchorMS + n * PERIOD_MS
    while t <= nowMS do t = t + PERIOD_MS end
    return t
end

local function WatchLeadMS()
    return Clamp(tonumber(TaiYangDrinkWalkDiagDB.watchLeadMS) or DEFAULT_WATCH_LEAD_MS, 0, 600)
end

local function SpamOpenMS()
    return Clamp(tonumber(TaiYangDrinkWalkDiagDB.spamOpenMS) or DEFAULT_SPAM_OPEN_MS, 50, 1950)
end

local function SpamCloseMS()
    return Clamp(tonumber(TaiYangDrinkWalkDiagDB.spamCloseMS) or DEFAULT_SPAM_CLOSE_MS, 0, 1900)
end

local function SpamRetryMS()
    return Clamp(tonumber(TaiYangDrinkWalkDiagDB.spamRetryMS) or DEFAULT_SPAM_RETRY_MS, 0, 250)
end

local function LeadMSToProgressPct(leadMS)
    local lead = Clamp(tonumber(leadMS) or 0, 0, PERIOD_MS)
    return Clamp((1 - (lead / PERIOD_MS)) * 100, 0, 100)
end

local function ProgressPctToLeadMS(pct)
    pct = Clamp(tonumber(pct) or 0, 0, 100)
    return Round(PERIOD_MS * (1 - pct / 100))
end

local function DrinkOpenPct()
    if TaiYangDrinkWalkDiagDB.drinkOpenPct ~= nil then
        return Clamp(tonumber(TaiYangDrinkWalkDiagDB.drinkOpenPct) or DEFAULT_DRINK_OPEN_PCT, 2.5, 98)
    end
    return LeadMSToProgressPct(SpamOpenMS())
end

local function DrinkClosePct()
    if TaiYangDrinkWalkDiagDB.drinkClosePct ~= nil then
        return Clamp(tonumber(TaiYangDrinkWalkDiagDB.drinkClosePct) or DEFAULT_DRINK_CLOSE_PCT, 5, 99.5)
    end
    return LeadMSToProgressPct(SpamCloseMS())
end

local function AutoLeadEnabled()
    if TaiYangDrinkWalkDiagDB.autoLeadEnabled == nil then return true end
    return TaiYangDrinkWalkDiagDB.autoLeadEnabled and true or false
end

local function AutoLeadMS()
    local v = tonumber(TaiYangDrinkWalkDiagDB.autoLeadMS) or DEFAULT_AUTO_LEAD_MS
    local floorMS = math.min(AUTO_LEAD_MAX_MS, math.max(AUTO_LEAD_MIN_MS, SpamCloseMS() + 30))
    return Clamp(v, floorMS, AUTO_LEAD_MAX_MS)
end

local function EffectiveSpamOpenMS()
    if AutoLeadEnabled() then return AutoLeadMS() end
    return SpamOpenMS()
end

local function SetAutoLeadMS(v, why)
    local floorMS = math.min(AUTO_LEAD_MAX_MS, math.max(AUTO_LEAD_MIN_MS, SpamCloseMS() + 30))
    v = Clamp(tonumber(v) or DEFAULT_AUTO_LEAD_MS, floorMS, AUTO_LEAD_MAX_MS)
    TaiYangDrinkWalkDiagDB.autoLeadMS = v
    DW.autoLeadLastChange = why or "MANUAL"
    if DW.autoLeadEdit then DW.autoLeadEdit:SetText(tostring(v)) end
end

local function SetAutoLeadEnabled(enabled)
    TaiYangDrinkWalkDiagDB.autoLeadEnabled = enabled and true or false
    DW.autoLeadSuccessStreak = 0
    DW.autoLeadMissStreak = 0
    AddLog("Auto Lead " .. (AutoLeadEnabled() and "ON" or "OFF"))
end

local function ResetAutoLead()
    TaiYangDrinkWalkDiagDB.autoLeadMS = DEFAULT_AUTO_LEAD_MS
    TaiYangDrinkWalkDiagDB.autoLeadEnabled = true
    DW.autoLeadSuccessStreak = 0
    DW.autoLeadMissStreak = 0
    DW.autoLeadConfirmedTicks = 0
    DW.autoLeadAdjustDown = 0
    DW.autoLeadAdjustUp = 0
    DW.autoLeadLastChange = "RESET"
    if DW.autoLeadEdit then DW.autoLeadEdit:SetText(tostring(DEFAULT_AUTO_LEAD_MS)) end
    AddLog("Auto Lead 重置为 " .. tostring(DEFAULT_AUTO_LEAD_MS) .. "ms")
end

local function NormalizeSpamWindow(openMS, closeMS, retryMS)
    openMS = Clamp(tonumber(openMS) or DEFAULT_SPAM_OPEN_MS, 50, 1950)
    closeMS = Clamp(tonumber(closeMS) or DEFAULT_SPAM_CLOSE_MS, 0, 1900)
    retryMS = Clamp(tonumber(retryMS) or DEFAULT_SPAM_RETRY_MS, 0, 250)
    if closeMS >= openMS then closeMS = math.max(0, openMS - 50) end
    return openMS, closeMS, retryMS
end

local function SetWatchLead(v)
    v = Clamp(tonumber(v) or DEFAULT_WATCH_LEAD_MS, 0, 600)
    TaiYangDrinkWalkDiagDB.watchLeadMS = v
    if DW.watchEdit then DW.watchEdit:SetText(tostring(v)) end
    AddLog("WATCH提示提前量=" .. tostring(v) .. "ms")
end

local function SetSpamWindow(openMS, closeMS, retryMS)
    openMS, closeMS, retryMS = NormalizeSpamWindow(openMS, closeMS, retryMS)
    TaiYangDrinkWalkDiagDB.spamOpenMS = openMS
    TaiYangDrinkWalkDiagDB.spamCloseMS = closeMS
    TaiYangDrinkWalkDiagDB.spamRetryMS = retryMS
    if DW.windowEdit then DW.windowEdit:SetText(string.format("%d,%d,%d", openMS, closeMS, retryMS)) end
    AddLog(string.format("SPAM窗口=%d~%dms retry=%dms", openMS, closeMS, retryMS))
end

local function SetSpamWindowSpec(text)
    text = tostring(text or "")
    text = string.gsub(text, "，", ",")
    local nums = {}
    local iter = string.gfind or string.gmatch
    local token
    if iter then
        for token in iter(text, "[^,]+") do table.insert(nums, tonumber(Trim(token))) end
    end
    if table.getn(nums) < 2 or not nums[1] or not nums[2] then
        AddLog("SPAM窗口格式：开窗,关窗,重试，例如 320,170,75")
        return
    end
    SetSpamWindow(nums[1], nums[2], nums[3] or SpamRetryMS())
end

local function SetDrinkWindowRatio(openPct, closePct)
    openPct = Clamp(tonumber(openPct) or DEFAULT_DRINK_OPEN_PCT, 2.5, 98)
    closePct = Clamp(tonumber(closePct) or DEFAULT_DRINK_CLOSE_PCT, 5, 99.5)
    if closePct <= openPct then closePct = math.min(99.5, openPct + 1) end

    TaiYangDrinkWalkDiagDB.drinkOpenPct = openPct
    TaiYangDrinkWalkDiagDB.drinkClosePct = closePct

    -- A user-entered percentage is authoritative. Convert it back to the
    -- existing SPAM timing contract so the native/API layer remains unchanged.
    local openMS = ProgressPctToLeadMS(openPct)
    local closeMS = ProgressPctToLeadMS(closePct)
    SetSpamWindow(openMS, closeMS, SpamRetryMS())
    SetAutoLeadEnabled(false)

    if DW.ratioOpenEdit then DW.ratioOpenEdit:SetText(string.format("%.1f", openPct)) end
    if DW.ratioCloseEdit then DW.ratioCloseEdit:SetText(string.format("%.1f", closePct)) end
    AddLog(string.format("吃喝窗口比例=%.1f%%~%.1f%% (%d~%dms before tick)", openPct, closePct, openMS, closeMS))
end

local function SetDrinkWindowRatioSpec(openText, closeText)
    local openPct = tonumber(Trim(openText or ""))
    local closePct = tonumber(Trim(closeText or ""))
    if not openPct or not closePct then
        AddLog("吃喝窗口比例格式错误，例如 84 / 91.5")
        return false
    end
    SetDrinkWindowRatio(openPct, closePct)
    return true
end

local function SavePhaseForReload()
    TaiYangDrinkWalkDiagDB.reloadRequested = true
    TaiYangDrinkWalkDiagDB.characterKey = CharKey()
    TaiYangDrinkWalkDiagDB.phaseAnchorMS = DW.anchorMS
    TaiYangDrinkWalkDiagDB.phaseState = DW.state
    TaiYangDrinkWalkDiagDB.phaseConfidence = DW.confidence
    TaiYangDrinkWalkDiagDB.phaseJitterMS = DW.jitterMS
    TaiYangDrinkWalkDiagDB.savedAtClientMS = NowMS()
end

local function RestoreReloadPhase()
    local db = TaiYangDrinkWalkDiagDB
    local valid = db.reloadRequested and db.characterKey == CharKey() and tonumber(db.phaseAnchorMS)
    db.reloadRequested = false
    if not valid then return false end
    DW.anchorMS = tonumber(db.phaseAnchorMS)
    DW.state = "VERIFY"
    DW.confidence = Clamp(tonumber(db.phaseConfidence) or 0.45, 0.20, 0.80)
    DW.jitterMS = tonumber(db.phaseJitterMS) or 0
    DW.phaseErrors = {0}
    DW.phaseBiasMS = 0
    DW.dynamicToleranceMS = PHASE_TOLERANCE_MS
    DW.goodSamples = 0
    DW.badSamples = 0
    DW.provisionalRejects = 0
    DW.classifierOutlierStreak = 0
    DW.resumeReason = "RELOAD_VERIFY"
    AddLog("/reload恢复旧相位 -> VERIFY")
    return true
end

local function HookReloadUI()
    if DW.reloadHooked or type(ReloadUI) ~= "function" then return end
    DW.reloadHooked = true
    DW.originalReloadUI = ReloadUI
    ReloadUI = function()
        SavePhaseForReload()
        return DW.originalReloadUI()
    end
end

local function ItemIdFromLink(link)
    if not link then return nil end
    local _, _, id = string.find(link, "item:(%d+)")
    return id and tonumber(id) or nil
end

local function ItemNameFromLink(link)
    if not link then return nil end
    if type(GetItemInfo) == "function" then
        local name = GetItemInfo(link)
        if name then return name end
    end
    local _, _, name = string.find(link, "%[(.-)%]")
    return name
end

local function ParseItemSpec()
    local raw = tostring(TaiYangDrinkWalkDiagDB.itemSpec or "")
    raw = string.gsub(raw, "，", ",")
    raw = string.gsub(raw, "；", ",")
    raw = string.gsub(raw, ";", ",")
    local ids = {}
    local names = {}
    local iter = string.gfind or string.gmatch
    local token
    if not iter then return ids, names end
    for token in iter(raw, "[^,]+") do
        token = Trim(token)
        if token ~= "" then
            local id = tonumber(token)
            if id and math.floor(id) == id and id > 0 then table.insert(ids, id)
            else table.insert(names, token) end
        end
    end
    return ids, names
end

local function ItemMatches(link, wantedId, wantedName)
    if not link then return false end
    local id = ItemIdFromLink(link)
    local name = ItemNameFromLink(link)
    if wantedId and id == wantedId then return true, id, name end
    if wantedName and name and string.lower(name) == string.lower(wantedName) then return true, id, name end
    return false, id, name
end

local function TooltipTextForBagItem(bag, slot)
    if not DW.scanTooltip or type(DW.scanTooltip.SetBagItem) ~= "function" then return "" end
    DW.scanTooltip:ClearLines()
    DW.scanTooltip:SetBagItem(bag, slot)
    local i
    local text = ""
    for i = 1, 12 do
        local obj = getglobal("TaiYangDrinkWalkScanTooltipTextLeft" .. i)
        if obj and obj:GetText() then text = text .. " " .. string.lower(obj:GetText()) end
        obj = getglobal("TaiYangDrinkWalkScanTooltipTextRight" .. i)
        if obj and obj:GetText() then text = text .. " " .. string.lower(obj:GetText()) end
    end
    return text
end

local function ConsumableKindFromBagSlot(bag, slot)
    local text = TooltipTextForBagItem(bag, slot)
    if text == "" then return "UNKNOWN" end

    local mana = string.find(text, "mana") or string.find(text, "法力")
    local health = string.find(text, "health") or string.find(text, "生命")
    local drinkWord = string.find(text, "drink") or string.find(text, "drinking") or string.find(text, "饮用") or string.find(text, "饮料") or string.find(text, "喝")
    local foodWord = string.find(text, "eat") or string.find(text, "eating") or string.find(text, "food") or string.find(text, "进食") or string.find(text, "食用") or string.find(text, "食物") or string.find(text, "吃")
    local restore = string.find(text, "restore") or string.find(text, "regener") or string.find(text, "恢复") or string.find(text, "回复")

    local isDrink = mana and (drinkWord or restore)
    local isFood = health and (foodWord or restore)
    if isDrink and isFood then return "BOTH" end
    if isDrink then return "DRINK" end
    if isFood then return "FOOD" end
    return "UNKNOWN"
end

local function ConsumableKindZh(kind)
    if kind == "FOOD" then return "食物" end
    if kind == "DRINK" then return "饮料" end
    if kind == "BOTH" then return "食物+饮料" end
    return "未识别"
end

local function PlayerConsumableNeeds()
    local hp = tonumber(UnitHealth("player")) or 0
    local hpMax = tonumber(UnitHealthMax("player")) or 0
    local healthNeed = hpMax > 0 and hp < hpMax

    local kind, current, maximum = RefreshResourceContext(false)
    local manaNeed = kind == "MANA" and maximum > 0 and current < maximum
    return healthNeed, manaNeed, hp, hpMax, current, maximum, kind
end

local function ConsumableNeedScore(item, healthNeed, manaNeed)
    if not item then return -1 end
    local kind = item.kind or "UNKNOWN"
    if kind == "BOTH" then
        if healthNeed and manaNeed then return 120 end
        if healthNeed or manaNeed then return 100 end
        return 10
    end
    if kind == "FOOD" then
        if healthNeed then return 90 end
        return 5
    end
    if kind == "DRINK" then
        if manaNeed then return 90 end
        return 5
    end
    if healthNeed or manaNeed then return 50 end
    return 1
end

local function FindConfiguredItem()
    DW.item = nil
    local ids, names = ParseItemSpec()
    if table.getn(ids) == 0 and table.getn(names) == 0 then
        DW.itemStatus = "未配置物品名/ID"
        return false
    end

    local function FindById(wantedId)
        local bag, slot, first, total
        first = nil
        total = 0
        for bag = 0, 4 do
            local slots = GetContainerNumSlots(bag) or 0
            for slot = 1, slots do
                local link = GetContainerItemLink(bag, slot)
                local ok, id, name = ItemMatches(link, wantedId, nil)
                if ok then
                    local texture, count, locked = GetContainerItemInfo(bag, slot)
                    count = tonumber(count) or 1
                    total = total + count
                    if not first then
                        first = {bag=bag, slot=slot, id=id, name=name or ("item:" .. tostring(id)), count=count, locked=locked, match="ID"}
                        first.kind = ConsumableKindFromBagSlot(bag, slot)
                    end
                end
            end
        end
        if first then first.totalCount = total end
        return first
    end

    local function FindByName(wantedName)
        local bag, slot, first, total
        first = nil
        total = 0
        for bag = 0, 4 do
            local slots = GetContainerNumSlots(bag) or 0
            for slot = 1, slots do
                local link = GetContainerItemLink(bag, slot)
                local ok, id, name = ItemMatches(link, nil, wantedName)
                if ok then
                    local texture, count, locked = GetContainerItemInfo(bag, slot)
                    count = tonumber(count) or 1
                    total = total + count
                    if not first then
                        first = {bag=bag, slot=slot, id=id, name=name or wantedName, count=count, locked=locked, match="NAME"}
                        first.kind = ConsumableKindFromBagSlot(bag, slot)
                    end
                end
            end
        end
        if first then first.totalCount = total end
        return first
    end

    local candidates = {}
    local i
    for i = 1, table.getn(ids) do
        local candidate = FindById(ids[i])
        if candidate then table.insert(candidates, candidate) end
    end
    for i = 1, table.getn(names) do
        local candidate = FindByName(names[i])
        if candidate then table.insert(candidates, candidate) end
    end

    if table.getn(candidates) > 0 then
        local healthNeed, manaNeed = PlayerConsumableNeeds()
        local best = candidates[1]
        local bestScore = ConsumableNeedScore(best, healthNeed, manaNeed)
        for i = 2, table.getn(candidates) do
            local score = ConsumableNeedScore(candidates[i], healthNeed, manaNeed)
            if score > bestScore then
                best = candidates[i]
                bestScore = score
            end
        end
        DW.item = best
    end

    if DW.item then
        local a = DW.item
        DW.itemStatus = string.format("%s [%s] (ID:%s) bag%d/%d x%d", tostring(a.name or "?"), ConsumableKindZh(a.kind), tostring(a.id or "?"), a.bag or -1, a.slot or -1, a.totalCount or a.count or 0)
        return true
    end
    DW.itemStatus = "背包中未找到精确匹配"
    return false
end

local function SetItemSpec(text)
    text = Trim(text)
    TaiYangDrinkWalkDiagDB.itemSpec = text
    if DW.itemEdit and DW.itemEdit:GetText() ~= text then DW.itemEdit:SetText(text) end
    FindConfiguredItem()
end

local function CountItemIdentity(item)
    if not item then return 0 end
    local total = 0
    local bag, slot
    for bag = 0, 4 do
        local slots = GetContainerNumSlots(bag) or 0
        for slot = 1, slots do
            local link = GetContainerItemLink(bag, slot)
            local matched = false
            if item.match == "ID" then
                matched = ItemMatches(link, item.id, nil)
            else
                matched = ItemMatches(link, nil, item.name)
            end
            if matched then
                local texture, count = GetContainerItemInfo(bag, slot)
                total = total + (tonumber(count) or 1)
            end
        end
    end
    return total
end

local function TooltipConsumableAuraKind(buffIndex)
    if not DW.scanTooltip or type(DW.scanTooltip.SetPlayerBuff) ~= "function" then return nil end
    DW.scanTooltip:ClearLines()
    DW.scanTooltip:SetPlayerBuff(buffIndex)
    local i
    local text = ""
    for i = 1, 8 do
        local obj = getglobal("TaiYangDrinkWalkScanTooltipTextLeft" .. i)
        if obj and obj:GetText() then text = text .. " " .. string.lower(obj:GetText()) end
        obj = getglobal("TaiYangDrinkWalkScanTooltipTextRight" .. i)
        if obj and obj:GetText() then text = text .. " " .. string.lower(obj:GetText()) end
    end

    local drinkWord = string.find(text, "drink") or string.find(text, "drinking") or string.find(text, "饮用") or string.find(text, "喝水") or string.find(text, "喝")
    local foodWord = string.find(text, "eat") or string.find(text, "eating") or string.find(text, "food") or string.find(text, "进食") or string.find(text, "食用") or string.find(text, "吃")
    if drinkWord and foodWord then return "BOTH" end
    if drinkWord then return "DRINK" end
    if foodWord then return "FOOD" end
    return nil
end

local function ScanDrinkAura()
    DW.drinkState = "UNKNOWN"
    DW.foodState = "UNKNOWN"
    DW.consumeState = "NONE"
    DW.drinkDetection = "事件扫描/食物饮料启发式"
    if type(GetPlayerBuff) ~= "function" then return end
    local i
    local foundAny = false
    for i = 0, 31 do
        local buffIndex = GetPlayerBuff(i, "HELPFUL")
        if not buffIndex or buffIndex < 0 then break end
        foundAny = true
        local kind = TooltipConsumableAuraKind(buffIndex)
        if kind == "DRINK" then
            DW.drinkState = "YES"
            DW.consumeState = (DW.foodState == "YES") and "BOTH" or "DRINK"
        elseif kind == "FOOD" then
            DW.foodState = "YES"
            DW.consumeState = (DW.drinkState == "YES") and "BOTH" or "FOOD"
        elseif kind == "BOTH" then
            DW.drinkState = "YES"
            DW.foodState = "YES"
            DW.consumeState = "BOTH"
        end
    end
    if foundAny then
        if DW.drinkState ~= "YES" then DW.drinkState = "NO" end
        if DW.foodState ~= "YES" then DW.foodState = "NO" end
    end
end

local function MatchingConsumableAuraActive(itemKind)
    if itemKind == "FOOD" then return DW.foodState == "YES" end
    if itemKind == "DRINK" then return DW.drinkState == "YES" end
    if itemKind == "BOTH" then return DW.foodState == "YES" or DW.drinkState == "YES" end
    return DW.foodState == "YES" or DW.drinkState == "YES"
end

local function ProbeCore()
    if type(TaiYangShenDian) ~= "function" then return "未检测到 DLL" end
    local ok, ver, api, build = pcall(TaiYangShenDian, "Core.Version")
    if not ok then return "Core.Version 调用失败" end
    local suffix = ""
    if tonumber(api or 0) >= 24 then
        local sok, st = pcall(TaiYangShenDian, "DrinkWalk.Status")
        if sok and type(st) == "table" then suffix = st.available and " / Native READY" or " / Native FAIL" end
    end
    return tostring(ver or "?") .. " / API " .. tostring(api or "?") .. suffix
end

local function NativeReady()
    if type(TaiYangShenDian) ~= "function" then return false, "DLL_MISSING" end
    local ok, st = pcall(TaiYangShenDian, "DrinkWalk.Status")
    if not ok or type(st) ~= "table" then return false, "STATUS_UNAVAILABLE" end
    if not st.available then return false, tostring(st.lastCode or "NATIVE_NOT_READY") end
    return true, "READY"
end

local function SpamBindingText()
    if type(GetBindingKey) ~= "function" then return "未知" end
    local k1, k2 = GetBindingKey("TAIYANGDRINKWALK_ACTION")
    if k1 and k2 then return tostring(k1) .. " / " .. tostring(k2) end
    if k1 then return tostring(k1) end
    if k2 then return tostring(k2) end
    return "未绑定"
end

local function SaveCurrentBindings()
    if type(SaveBindings) == "function" and type(GetCurrentBindingSet) == "function" then
        pcall(SaveBindings, GetCurrentBindingSet())
    end
end

local function ClearActionBindings()
    if type(GetBindingKey) ~= "function" or type(SetBinding) ~= "function" then return false end
    local k1, k2 = GetBindingKey("TAIYANGDRINKWALK_ACTION")
    if k1 then pcall(SetBinding, k1) end
    if k2 and k2 ~= k1 then pcall(SetBinding, k2) end
    return true
end

local function UnbindActionKey(quiet)
    if type(SetBinding) ~= "function" then
        DW.lastKeyResult = "SetBinding API不可用，请到游戏按键设置解绑"
        return false
    end
    ClearActionBindings()
    SaveCurrentBindings()
    TaiYangDrinkWalkDiagDB.bindKey = ""
    if DW.bindEdit then DW.bindEdit:SetText("") end
    DW.lastKeyResult = "已解绑吃喝动作全部快捷键"
    if not quiet then AddLog(DW.lastKeyResult) end
    return true
end

local function BindActionKey(key, quiet)
    key = string.upper(Trim(key))
    if key == "" then
        return UnbindActionKey(quiet)
    end
    if type(SetBinding) ~= "function" then
        DW.lastKeyResult = "SetBinding API不可用，请到游戏按键设置绑定"
        return false
    end

    -- SetBinding(command) adds a second key if the old key is left in place.
    -- Always remove every old key for this command first, then install exactly
    -- one replacement key. This fixes the V0.5.1 F + G + H accumulation bug.
    local old1, old2 = nil, nil
    if type(GetBindingKey) == "function" then old1, old2 = GetBindingKey("TAIYANGDRINKWALK_ACTION") end
    ClearActionBindings()

    local ok, result = pcall(SetBinding, key, "TAIYANGDRINKWALK_ACTION")
    local verified = false
    if ok and type(GetBindingKey) == "function" then
        local k1, k2 = GetBindingKey("TAIYANGDRINKWALK_ACTION")
        verified = (k1 == key or k2 == key) and true or false
    elseif ok then
        verified = true
    end

    if not ok or not verified then
        -- Best-effort rollback so a failed new key does not silently erase the
        -- user's previous working binding.
        if old1 then pcall(SetBinding, old1, "TAIYANGDRINKWALK_ACTION") end
        if old2 and old2 ~= old1 then pcall(SetBinding, old2, "TAIYANGDRINKWALK_ACTION") end
        SaveCurrentBindings()
        DW.lastKeyResult = "绑定失败：" .. tostring(result or "无效按键")
        if not quiet then AddLog(DW.lastKeyResult) end
        return false
    end

    SaveCurrentBindings()
    TaiYangDrinkWalkDiagDB.bindKey = key
    if DW.bindEdit then DW.bindEdit:SetText(key) end
    DW.lastKeyResult = "已替换绑定为 " .. key .. "；旧按键已清除"
    if not quiet then AddLog(DW.lastKeyResult) end
    return true
end

local function NormalizeStoredBinding()
    if TaiYangDrinkWalkDiagDB.bindKey == nil then return end
    local key = string.upper(Trim(TaiYangDrinkWalkDiagDB.bindKey))
    if key == "" then UnbindActionKey(true) else BindActionKey(key, true) end
end

local function SetMode(mode)
    if mode ~= "SPAM" then mode = "WATCH" end
    DW.mode = mode
    TaiYangDrinkWalkDiagDB.lastSelectedMode = mode
    if mode == "SPAM" then
        if not FindConfiguredItem() then
            DW.mode = "WATCH"
            DW.lastKeyResult = "没有可用吃喝物品，保持WATCH：" .. tostring(DW.itemStatus)
            AddLog(DW.lastKeyResult)
            return false
        end
        local ready, why = NativeReady()
        if not ready then
            DW.mode = "WATCH"
            DW.lastKeyResult = "Native未就绪，保持WATCH：" .. tostring(why)
            AddLog(DW.lastKeyResult)
            return false
        end
        DW.lastKeyResult = "SPAM已开启：自己负责松方向键，只管狂按绑定键"
        AddLog(DW.lastKeyResult)
        return true
    end
    DW.lastKeyResult = "WATCH：只观察提示，不会使用物品"
    AddLog(DW.lastKeyResult)
    return true
end

local function AutoLeadCanLearnDown()
    return (DW.state == "LOCKED")
        and (DW.confidence or 0) >= 0.80
        and (DW.jitterMS or 9999) <= AUTO_LEAD_MAX_JITTER_FOR_DOWN_MS
end

local function FinalizeSpamTick()
    if not DW.spamTickMS then return end
    if not AutoLeadEnabled() then return end

    if DW.spamTickLearnableSuccess then
        DW.autoLeadConfirmedTicks = (DW.autoLeadConfirmedTicks or 0) + 1
        DW.autoLeadSuccessStreak = (DW.autoLeadSuccessStreak or 0) + 1
        DW.autoLeadMissStreak = 0
        if DW.spamTickSuccessLeadMS then
            PushBounded(DW.autoLeadSuccessLeads, DW.spamTickSuccessLeadMS, 20)
        end
        if DW.autoLeadSuccessStreak >= AUTO_LEAD_SUCCESS_STREAK and AutoLeadCanLearnDown() then
            local old = AutoLeadMS()
            local new = math.max(math.max(AUTO_LEAD_MIN_MS, SpamCloseMS() + 30), old - AUTO_LEAD_STEP_DOWN_MS)
            if new < old then
                SetAutoLeadMS(new, "SUCCESS_STREAK_DOWN")
                DW.autoLeadAdjustDown = (DW.autoLeadAdjustDown or 0) + 1
                AddLog(string.format("AutoLead学习：%d次稳定命中 %d->%dms", AUTO_LEAD_SUCCESS_STREAK, old, new))
            end
            DW.autoLeadSuccessStreak = 0
        end
        return
    end

    if DW.spamTickHadRequest and not DW.spamTickContextRejected then
        DW.autoLeadMissStreak = (DW.autoLeadMissStreak or 0) + 1
        DW.autoLeadSuccessStreak = 0
        if DW.autoLeadMissStreak >= AUTO_LEAD_MISS_STREAK then
            local old = AutoLeadMS()
            local new = math.min(AUTO_LEAD_MAX_MS, old + AUTO_LEAD_STEP_UP_MS)
            if new > old then
                SetAutoLeadMS(new, "UNCONFIRMED_UP")
                DW.autoLeadAdjustUp = (DW.autoLeadAdjustUp or 0) + 1
                AddLog(string.format("AutoLead保护：连续未确认 %d->%dms", old, new))
            end
            DW.autoLeadMissStreak = 0
        end
        return
    end

    -- No request, or the game explicitly rejected the attempt (commonly still
    -- moving / item unavailable).  Those ticks are not timing evidence.
    DW.autoLeadSuccessStreak = 0
end

local function ResetSpamTickLearning(nextTick)
    DW.spamTickMS = nextTick
    DW.spamConfirmedTickMS = nil
    DW.spamAttemptsThisTick = 0
    DW.lastDispatchMS = nil
    DW.spamTickHadRequest = false
    DW.spamTickContextRejected = false
    DW.spamTickLearnableSuccess = false
    DW.spamTickSuccessLeadMS = nil
    DW.recentDispatch = nil
    DW.lastKeyResult = "新Tick：等待进入狂按窗口"
end

local function EnsureSpamTick(nextTick)
    if not nextTick then return end
    if not DW.spamTickMS then
        ResetSpamTickLearning(nextTick)
        return
    end
    if math.abs(nextTick - DW.spamTickMS) > 500 then
        FinalizeSpamTick()
        ResetSpamTickLearning(nextTick)
    end
end

local function MarkSpamConfirmed(reason, detail, tickMS)
    tickMS = tickMS or DW.spamTickMS
    if not tickMS then return end
    DW.spamConfirmedTickMS = tickMS
    DW.spamConfirmCount = (DW.spamConfirmCount or 0) + 1
    -- UI-only feedback: briefly emphasize the fixed Tick-zero marker when
    -- item consumption/Aura confirmation arrives. This does not change timing.
    DW.tickMarkerFlashUntilMS = NowMS() + 420

    local r = DW.recentDispatch
    if r and r.tickMS and math.abs(r.tickMS - tickMS) < 500 then
        DW.spamTickLearnableSuccess = true
        DW.spamTickSuccessLeadMS = tonumber(r.leadMS)
    end

    DW.lastKeyResult = "CONFIRMED [" .. tostring(reason or "?") .. "] " .. tostring(detail or "")
    AddLog("SPAM " .. DW.lastKeyResult)
end

local function RefreshCurrentItemSlot()
    local a = DW.item
    if not a then return false, false, 0 end
    local texture, count, locked = GetContainerItemInfo(a.bag, a.slot)
    if not texture then
        FindConfiguredItem()
        a = DW.item
        if not a then return false, false, 0 end
        texture, count, locked = GetContainerItemInfo(a.bag, a.slot)
    end
    a.count = tonumber(count) or a.count or 1
    a.locked = locked
    return true, locked, a.count
end

local function NativeUseFromHardwareKey(nowMS, tickMS)
    if not DW.item and not FindConfiguredItem() then
        DW.lastKeyResult = "找不到配置物品"
        return false
    end

    -- Re-select only when the cached item no longer matches the player's
    -- current need. This allows a configured "water,food" list to switch from
    -- water to food when Mana is full but Health is missing, without scanning
    -- all bags on every spammed hardware press.
    local healthNeed, manaNeed = PlayerConsumableNeeds()
    if DW.item and ConsumableNeedScore(DW.item, healthNeed, manaNeed) < 50 then
        FindConfiguredItem()
    end
    if not DW.item then
        DW.lastKeyResult = "找不到适合当前状态的配置物品"
        return false
    end

    local okSlot, locked = RefreshCurrentItemSlot()
    if not okSlot then
        DW.lastKeyResult = "当前物品槽无效"
        return false
    end
    if locked then
        DW.lastKeyResult = "物品锁定，继续狂按"
        return false
    end

    local itemKind = DW.item.kind or "UNKNOWN"
    if MatchingConsumableAuraActive(itemKind) then
        MarkSpamConfirmed("CONSUME_AURA", "已经在" .. ((itemKind == "DRINK") and "喝水" or "进食/恢复"), tickMS)
        return true
    end

    local healthNeed, manaNeed, hp, hpMax, mana, manaMax, resourceKind = PlayerConsumableNeeds()
    if itemKind == "FOOD" and not healthNeed then
        DW.lastKeyResult = "生命已满，不使用食物"
        return false
    end
    if itemKind == "DRINK" then
        if resourceKind ~= "MANA" then
            DW.lastKeyResult = "当前形态/职业无Mana，不使用饮料"
            return false
        end
        if not manaNeed then
            DW.lastKeyResult = "Mana已满，不使用饮料"
            return false
        end
    end
    if itemKind == "BOTH" and not healthNeed and not manaNeed then
        DW.lastKeyResult = "生命/Mana均满，不使用双恢复物品"
        return false
    end
    if itemKind == "UNKNOWN" and not healthNeed and not manaNeed then
        DW.lastKeyResult = "生命/Mana均无需恢复，不使用未识别物品"
        return false
    end

    if DW.lastDispatchMS and nowMS - DW.lastDispatchMS < SpamRetryMS() then
        DW.lastKeyResult = "重试保护 " .. FormatMS(SpamRetryMS() - (nowMS - DW.lastDispatchMS))
        return false
    end

    local ready, why = NativeReady()
    if not ready then
        DW.lastKeyResult = "Native不可用：" .. tostring(why)
        return false
    end

    local a = DW.item
    local beforeTotal = CountItemIdentity(a)
    local lead = tickMS - nowMS
    DW.lastDispatchMS = nowMS
    DW.spamAttemptsThisTick = (DW.spamAttemptsThisTick or 0) + 1
    DW.spamRequestCount = (DW.spamRequestCount or 0) + 1
    DW.spamTickHadRequest = true
    DW.recentDispatch = {
        requestedMS = nowMS,
        tickMS = tickMS,
        leadMS = lead,
        beforeTotal = beforeTotal,
        itemId = a.id,
        itemName = a.name,
        itemKind = a.kind,
        match = a.match
    }
    DW.lastKeyResult = string.format("真实按键尝试 #%d Lead=%s", DW.spamAttemptsThisTick, FormatMS(lead))

    local ok, dispatched, code, engineRet = pcall(TaiYangShenDian, "DrinkWalk.UseItem", a.bag, a.slot)
    if not ok then
        DW.lastKeyResult = "Native API_ERROR：" .. tostring(dispatched or "unknown")
        AddLog("SPAM " .. DW.lastKeyResult)
        return false
    end
    if not dispatched then
        DW.lastKeyResult = "Native未分派：" .. tostring(code or "?")
        return false
    end
    DW.lastNativeCode = code
    DW.lastNativeReturn = engineRet
    return true
end

function DW:HandleActionKey()
    local now = NowMS()
    self.keyPressCount = (self.keyPressCount or 0) + 1
    self.lastKeyPressMS = now

    if self.mode ~= "SPAM" then
        self.lastKeyResult = "WATCH模式：按键不使用物品"
        return
    end

    if not self.item and not FindConfiguredItem() then
        self.lastKeyResult = "找不到配置物品"
        return
    end

    local healthNeed, manaNeed = PlayerConsumableNeeds()
    if self.item and ConsumableNeedScore(self.item, healthNeed, manaNeed) < 50 then
        FindConfiguredItem()
    end
    if not self.item then
        self.lastKeyResult = "找不到适合当前状态的配置物品"
        return
    end

    local itemKind = self.item.kind or "UNKNOWN"
    local resourceKind = self.resourceType or PowerTypeToResource(CurrentPowerType())
    local nextTick = nil
    local timingKind = resourceKind

    if resourceKind == "MANA" then
        if self.state ~= "LOCKED" then
            -- Food must not be held hostage by the mana classifier. A full-mana
            -- caster may have no observable mana-gain samples at all, yet still
            -- needs to eat. In that case use the real hardware press directly.
            if itemKind == "FOOD" or (itemKind == "BOTH" and healthNeed and not manaNeed) or (itemKind == "UNKNOWN" and healthNeed and not manaNeed) then
                self.lastKeyResult = "Mana Tick 未LOCKED，但当前只需进食：直接硬件尝试"
                NativeUseFromHardwareKey(now, now + SpamCloseMS() + 1)
                return
            end
            self.lastKeyResult = "Mana Tick 未LOCKED：饮水窗口尚未建立"
            return
        end
        nextTick = NextTickMS(now)
    elseif resourceKind == "ENERGY" then
        nextTick = EnergyNextTickMS(now)
        if not nextTick then
            -- At full energy there may be no visible natural +16/+20 event to
            -- establish an anchor. Food must still be usable: keep hardware
            -- gating and retry protection, but skip phase-learning until a real
            -- Energy Tick appears.
            self.lastKeyResult = "Energy尚无Tick锚点：直接硬件尝试"
            NativeUseFromHardwareKey(now, now + SpamCloseMS() + 1)
            return
        end
    else
        -- Rage has no stable periodic regeneration tick. Do not fabricate one.
        -- Food remains manually usable from the real hardware key; the native
        -- layer/game still decides whether movement/context allows the use.
        self.lastKeyResult = "Rage无周期Tick：直接硬件尝试"
        NativeUseFromHardwareKey(now, now + SpamCloseMS() + 1)
        return
    end

    if not nextTick then
        self.lastKeyResult = "没有下一" .. tostring(timingKind) .. " Tick预测"
        return
    end
    EnsureSpamTick(nextTick)

    if self.spamConfirmedTickMS and math.abs(self.spamConfirmedTickMS - nextTick) < 500 then
        self.lastKeyResult = "本Tick已确认吃喝，继续按也不会重复"
        return
    end

    local tickIn = nextTick - now
    if tickIn > EffectiveSpamOpenMS() then
        self.lastKeyResult = "太早：" .. tostring(timingKind) .. "窗口未开"
        return
    end
    if tickIn < SpamCloseMS() then
        self.lastKeyResult = "太晚：本Tick不再尝试"
        return
    end

    -- IMPORTANT: no movement control here. If the player is still moving,
    -- the client/server may reject this attempt. The next real key press can
    -- retry after retryGuard; release the movement key and keep spamming.
    NativeUseFromHardwareKey(now, nextTick)
end

function TaiYangDrinkWalk_ActionKey()
    if DW and DW.HandleActionKey then return DW:HandleActionKey() end
end

local function ConfirmFromBagUpdate()
    local r = DW.recentDispatch
    if not r or not DW.item then return end
    local now = NowMS()
    if now - (r.requestedMS or 0) > EXEC_CONFIRM_WINDOW_MS then return end
    local afterTotal = CountItemIdentity(DW.item)
    if afterTotal < (tonumber(r.beforeTotal) or afterTotal) then
        MarkSpamConfirmed("ITEM_COUNT_DECREASE", tostring(r.beforeTotal) .. "->" .. tostring(afterTotal), r.tickMS)
    end
end

local function ConfirmFromDrinkAura()
    local r = DW.recentDispatch
    if not r then return end
    local now = NowMS()
    if now - (r.requestedMS or 0) > EXEC_CONFIRM_WINDOW_MS then return end
    local kind = r.itemKind or "UNKNOWN"
    if MatchingConsumableAuraActive(kind) then
        MarkSpamConfirmed("CONSUME_AURA", ConsumableKindZh(kind) .. " Aura出现", r.tickMS)
    end
end

local function ResetDiagnostics()
    ClearPhase("MANUAL_RESET")
    DW.logs = {}
    DW.rawManaLogs = {}
    RefreshRawLogLines()
    DW.lastKeyResult = "已重置相位"
    AddLog(DW.lastKeyResult)
end

local function CreateButton(parent, text, x, y, w, fn)
    local b = CreateFrame("Button", nil, parent, "UIPanelButtonTemplate")
    b:SetWidth(w or 60)
    b:SetHeight(20)
    b:SetPoint("TOPLEFT", parent, "TOPLEFT", x, y)
    b:SetText(text)
    b:SetScript("OnClick", fn)
    return b
end

local function CreateLabel(parent, x, y, width, font)
    local fs = parent:CreateFontString(nil, "OVERLAY", font or "GameFontNormalSmall")
    fs:SetPoint("TOPLEFT", parent, "TOPLEFT", x, y)
    fs:SetWidth(width or 430)
    fs:SetJustifyH("LEFT")
    return fs
end

local function ShowTickTimerEnabled()
    if TaiYangDrinkWalkDiagDB.showTickTimer == nil then return true end
    return TaiYangDrinkWalkDiagDB.showTickTimer and true or false
end

local function ShowManaSparkEnabled()
    if TaiYangDrinkWalkDiagDB.showManaSpark == nil then return true end
    return TaiYangDrinkWalkDiagDB.showManaSpark and true or false
end

local function UserStatusText()
    local spec = Trim(TaiYangDrinkWalkDiagDB.itemSpec or "")
    if spec == "" then return "未设置物品" end
    if not DW.item then return "未找到物品" end
    if type(TaiYangShenDian) ~= "function" then return "未检测到 DLL" end
    return "就绪"
end

local function CurrentItemText()
    if DW.item then
        return string.format("当前：%s  [%s]  (ID:%s)  x%d", tostring(DW.item.name or "?"), ConsumableKindZh(DW.item.kind), tostring(DW.item.id or "?"), tonumber(DW.item.totalCount or DW.item.count or 0) or 0)
    end
    local spec = Trim(TaiYangDrinkWalkDiagDB.itemSpec or "")
    if spec == "" then return "当前：未设置" end
    return "当前：" .. tostring(DW.itemStatus or "未找到物品")
end

local function SetTickTimerEnabled(enabled)
    TaiYangDrinkWalkDiagDB.showTickTimer = enabled and true or false
    if DW.tickCheck then DW.tickCheck:SetChecked(ShowTickTimerEnabled()) end
end

local function SetManaSparkEnabled(enabled)
    TaiYangDrinkWalkDiagDB.showManaSpark = enabled and true or false
    if DW.manaCheck then DW.manaCheck:SetChecked(ShowManaSparkEnabled()) end
end

local function SetManaTimerSpark(progress)
    if not DW.manaTimerSpark or not DW.manaTimerBar then return end
    progress = Clamp(tonumber(progress) or 0, 0, 1)
    local width = DW.manaTimerBar:GetWidth() or UI_MANA_BAR_WIDTH
    local x = width * progress

    DW.manaTimerSpark:ClearAllPoints()
    DW.manaTimerSpark:SetPoint("CENTER", DW.manaTimerBar, "LEFT", x, 0)
    DW.manaTimerSpark:Show()

    -- Keep a thin, high-contrast cursor in addition to the glow texture.
    -- Some 1.12 clients render the stock CastingBar spark faintly; the line
    -- guarantees that ManaBar and Spark are visually distinct.
    if DW.manaTimerSparkLine then
        DW.manaTimerSparkLine:ClearAllPoints()
        DW.manaTimerSparkLine:SetPoint("CENTER", DW.manaTimerBar, "LEFT", x, 0)
        DW.manaTimerSparkLine:Show()
    end
end

local function SaveHUDPosition(frame, prefix)
    if not frame or not prefix then return end
    local fx, fy = frame:GetCenter()
    local ux, uy = UIParent:GetCenter()
    if not fx or not fy or not ux or not uy then return end
    TaiYangDrinkWalkDiagDB[prefix .. "X"] = fx - ux
    TaiYangDrinkWalkDiagDB[prefix .. "Y"] = fy - uy
end

local function ApplyHUDPosition(frame, prefix, defaultX, defaultY)
    if not frame then return end
    local x = tonumber(TaiYangDrinkWalkDiagDB[prefix .. "X"])
    local y = tonumber(TaiYangDrinkWalkDiagDB[prefix .. "Y"])
    if x == nil then x = defaultX or 0 end
    if y == nil then y = defaultY or 0 end
    frame:ClearAllPoints()
    frame:SetPoint("CENTER", UIParent, "CENTER", x, y)
end

local function EnableShiftLeftDrag(frame, prefix)
    if not frame then return end
    frame:SetMovable(true)
    frame:EnableMouse(true)
    frame:SetScript("OnMouseDown", function()
        if arg1 == "LeftButton" and type(IsShiftKeyDown) == "function" and IsShiftKeyDown() then
            this:StartMoving()
            this.dwDragging = true
        end
    end)
    frame:SetScript("OnMouseUp", function()
        if this.dwDragging then
            this:StopMovingOrSizing()
            this.dwDragging = nil
            SaveHUDPosition(this, prefix)
        end
    end)
    frame:SetScript("OnHide", function()
        if this.dwDragging then
            this:StopMovingOrSizing()
            this.dwDragging = nil
            SaveHUDPosition(this, prefix)
        end
    end)
end

local function LayoutHUD()
    if not DW.tickTimerFrame or not DW.manaTimerFrame then return end
    local tickY = -170
    local manaY = -170
    if ShowTickTimerEnabled() and ShowManaSparkEnabled() then
        tickY = -155
        manaY = -190
    elseif ShowTickTimerEnabled() then
        tickY = -170
        manaY = -190
    end
    ApplyHUDPosition(DW.tickTimerFrame, "tickHUD", 0, tickY)
    ApplyHUDPosition(DW.manaTimerFrame, "manaHUD", 0, manaY)
end

local function RenderTickTimerUI(nowMS, nextTickMS, tickInMS, tickPeriodMS, tickKind)
    if not DW.tickTimerFrame or not DW.tickTimerBar then return end
    if not ShowTickTimerEnabled() or not nextTickMS or not tickInMS or not tickPeriodMS then
        DW.tickTimerFrame:Hide()
        return
    end

    local remain = Clamp(tickInMS, 0, tickPeriodMS)
    local r, g, b = ResourceColor(tickKind or DW.resourceType or "MANA")
    DW.tickTimerBar:SetStatusBarColor(r, g, b)
    DW.tickTimerBar:SetValue(remain / math.max(1, tickPeriodMS))
    DW.tickTimerText:SetText(string.format("%.2fs", remain / 1000))

    -- The countdown bar drains from full to zero. Therefore the real Tick
    -- instant (0 ms remaining) is the LEFT edge, not the right edge. Keep a
    -- fixed red line there so the walk-drink timing target is always obvious.
    if DW.tickEndMarker then
        DW.tickEndMarker:Show()
    end
    if DW.tickEndMarkerGlow then
        if DW.tickMarkerFlashUntilMS and nowMS <= DW.tickMarkerFlashUntilMS then
            DW.tickEndMarkerGlow:SetAlpha(0.85)
            DW.tickEndMarkerGlow:Show()
        else
            DW.tickEndMarkerGlow:Hide()
        end
    end

    DW.tickTimerFrame:Show()
end

local function RenderManaTimerUI(nowMS, nextTickMS, tickInMS, tickPeriodMS, tickKind)
    if not DW.manaTimerFrame or not DW.manaTimerBar then return nil, nil end
    local kind = DW.resourceType or "MANA"
    local current = tonumber(DW.resourceValue or 0) or 0
    local maximum = tonumber(DW.resourceMax or 0) or 0

    if not ShowManaSparkEnabled() or maximum <= 0 then
        DW.manaTimerFrame:Hide()
        return nil, nil
    end

    DW.manaTimerFrame:Show()
    local ratio = Clamp(current / maximum, 0, 1)
    DW.manaTimerBar:SetValue(ratio)

    local r, g, b = ResourceColor(kind)
    local br, bg, bb, ba = ResourceBGColor(kind)
    DW.manaTimerBar:SetStatusBarColor(r, g, b)
    if DW.manaTimerBG then DW.manaTimerBG:SetVertexColor(br, bg, bb, ba) end

    local displayCurrent, displayMax = DisplayResourceValues(kind, current, maximum)
    if DW.resourceText then
        -- Rage sits on a dark-red bar and the old small white text looked too
        -- faint in combat. Give Rage its own high-contrast presentation while
        -- leaving Mana/Energy visually unchanged.
        if kind == "RAGE" then
            if type(STANDARD_TEXT_FONT) == "string" then
                DW.resourceText:SetFont(STANDARD_TEXT_FONT, 13, "OUTLINE")
            end
            DW.resourceText:SetTextColor(1.00, 0.88, 0.18)
            DW.resourceText:SetAlpha(1.00)
            if DW.resourceText.SetShadowColor then
                DW.resourceText:SetShadowColor(0, 0, 0, 1)
            end
            if DW.resourceText.SetShadowOffset then
                DW.resourceText:SetShadowOffset(1, -1)
            end
        else
            if type(STANDARD_TEXT_FONT) == "string" then
                DW.resourceText:SetFont(STANDARD_TEXT_FONT, 10, "")
            end
            DW.resourceText:SetTextColor(1, 1, 1)
            DW.resourceText:SetAlpha(1.00)
            if DW.resourceText.SetShadowColor then
                DW.resourceText:SetShadowColor(0, 0, 0, 0)
            end
            if DW.resourceText.SetShadowOffset then
                DW.resourceText:SetShadowOffset(0, 0)
            end
        end

        if kind == "ENERGY" then
            DW.resourceText:SetText(string.format("%s  %d / %d    Tick %.2fs", ResourceNameZh(kind), Round(displayCurrent), Round(displayMax), EffectiveEnergyTickMS() / 1000))
        else
            DW.resourceText:SetText(string.format("%s  %d / %d", ResourceNameZh(kind), Round(displayCurrent), Round(displayMax)))
        end
    end

    local mode = "WAIT"
    local remainMS = nil
    if kind == "MANA" and DW.uiFSREndMS and nowMS < DW.uiFSREndMS then
        mode = "FSR"
        remainMS = DW.uiFSREndMS - nowMS
        -- 5SR: Spark travels right -> left.
        SetManaTimerSpark(remainMS / UI_FSR_MS)
    elseif (kind == "MANA" or kind == "ENERGY") and nextTickMS and tickInMS and tickPeriodMS then
        mode = "TICK"
        remainMS = tickInMS
        -- Mana/Energy Tick: Spark travels left -> right.
        SetManaTimerSpark(1 - Clamp(tickInMS / math.max(1, tickPeriodMS), 0, 1))
    else
        DW.manaTimerSpark:Hide()
        if DW.manaTimerSparkLine then DW.manaTimerSparkLine:Hide() end
    end

    if DW.manaFlash then
        local flashDelta, flashMS, flashUntil = nil, nil, nil
        if kind == "MANA" then
            flashDelta, flashMS, flashUntil = DW.lastGainDelta, DW.lastGainMS, DW.manaFlashUntilMS
        elseif kind == "ENERGY" and DW.resourceFlashKind == "ENERGY" then
            flashDelta, flashMS, flashUntil = DW.resourceFlashDelta, DW.resourceFlashMS, DW.resourceFlashUntilMS
        end

        if flashDelta and flashMS and flashUntil and nowMS <= flashUntil then
            local elapsed = nowMS - flashMS
            local lifetime = math.max(1, flashUntil - flashMS)
            local alpha = 1
            if elapsed > lifetime * 0.45 then
                alpha = 1 - ((elapsed - lifetime * 0.45) / (lifetime * 0.55))
                if alpha < 0 then alpha = 0 end
            end
            DW.manaFlash:SetText("+" .. tostring(Round(tonumber(flashDelta) or 0)))
            DW.manaFlash:SetAlpha(alpha)
            DW.manaFlash:Show()
        else
            DW.manaFlash:Hide()
        end
    end
    return mode, remainMS
end

local function CreateTickTimerHUD()
    local holder = CreateFrame("Frame", "TaiYangDrinkWalkTickTimerFrame", UIParent)
    holder:SetWidth(UI_TICK_BAR_WIDTH)
    holder:SetHeight(UI_TICK_BAR_HEIGHT)
    holder:SetBackdrop({bgFile="Interface\\Tooltips\\UI-Tooltip-Background", edgeFile="Interface\\Tooltips\\UI-Tooltip-Border", tile=true, tileSize=16, edgeSize=10, insets={left=2,right=2,top=2,bottom=2}})
    holder:SetBackdropColor(0, 0, 0, 0.78)

    local bar = CreateFrame("StatusBar", "TaiYangDrinkWalkTickTimerBar", holder)
    bar:SetPoint("TOPLEFT", holder, "TOPLEFT", 3, -3)
    bar:SetPoint("BOTTOMRIGHT", holder, "BOTTOMRIGHT", -3, 3)
    bar:SetStatusBarTexture("Interface\\TARGETINGFRAME\\UI-StatusBar")
    bar:SetStatusBarColor(0.05, 0.10, 0.88)
    bar:SetMinMaxValues(0, 1)
    bar:SetValue(1)

    local bg = bar:CreateTexture(nil, "BACKGROUND")
    bg:SetAllPoints(bar)
    bg:SetTexture("Interface\\TARGETINGFRAME\\UI-StatusBar")
    bg:SetVertexColor(0.02, 0.02, 0.12, 0.86)

    -- Fixed Tick-zero marker. The status bar counts down toward the LEFT edge,
    -- so this red line represents the exact predicted Tick boundary.
    local endMarkerGlow = bar:CreateTexture(nil, "ARTWORK")
    endMarkerGlow:SetTexture(1.00, 0.05, 0.05, 0.65)
    endMarkerGlow:SetWidth(9)
    endMarkerGlow:SetHeight(UI_TICK_BAR_HEIGHT + 4)
    endMarkerGlow:SetPoint("CENTER", bar, "LEFT", 1, 0)
    endMarkerGlow:Hide()

    local endMarker = bar:CreateTexture(nil, "OVERLAY")
    endMarker:SetTexture(1.00, 0.05, 0.05, 1.00)
    endMarker:SetWidth(3)
    endMarker:SetHeight(UI_TICK_BAR_HEIGHT + 6)
    endMarker:SetPoint("CENTER", bar, "LEFT", 1, 0)

    local text = holder:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    text:SetPoint("LEFT", holder, "LEFT", 9, 0)
    text:SetText("2.0s")
    text:SetTextColor(1, 1, 1)

    EnableShiftLeftDrag(holder, "tickHUD")

    DW.tickTimerFrame = holder
    DW.tickTimerBar = bar
    DW.tickTimerText = text
    DW.tickEndMarker = endMarker
    DW.tickEndMarkerGlow = endMarkerGlow
    holder:Hide()
end

local function CreateManaSparkHUD()
    local holder = CreateFrame("Frame", "TaiYangDrinkWalkManaTimerFrame", UIParent)
    holder:SetWidth(UI_MANA_BAR_WIDTH + 6)
    holder:SetHeight(22)
    holder:SetBackdrop({bgFile="Interface\\Tooltips\\UI-Tooltip-Background", edgeFile="Interface\\Tooltips\\UI-Tooltip-Border", tile=true, tileSize=16, edgeSize=8, insets={left=2,right=2,top=2,bottom=2}})
    holder:SetBackdropColor(0, 0, 0, 0.72)

    local bar = CreateFrame("StatusBar", "TaiYangDrinkWalkManaTimerBar", holder)
    bar:SetWidth(UI_MANA_BAR_WIDTH)
    bar:SetHeight(UI_MANA_BAR_HEIGHT)
    bar:SetPoint("CENTER", holder, "CENTER", 0, 0)
    bar:SetStatusBarTexture("Interface\\TARGETINGFRAME\\UI-StatusBar")
    bar:SetStatusBarColor(0.05, 0.35, 0.95)
    bar:SetMinMaxValues(0, 1)
    bar:SetValue(0)

    local bg = bar:CreateTexture(nil, "BACKGROUND")
    bg:SetAllPoints(bar)
    bg:SetTexture("Interface\\TARGETINGFRAME\\UI-StatusBar")
    bg:SetVertexColor(0.03, 0.03, 0.09, 0.88)

    local resourceText = holder:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    resourceText:SetPoint("CENTER", bar, "CENTER", 0, 0)
    resourceText:SetTextColor(1, 1, 1)
    resourceText:SetText("资源 -- / --")

    local spark = bar:CreateTexture(nil, "OVERLAY")
    spark:SetTexture("Interface\\CastingBar\\UI-CastingBar-Spark")
    spark:SetBlendMode("ADD")
    spark:SetWidth(22)
    spark:SetHeight(34)
    spark:Hide()

    local sparkLine = bar:CreateTexture(nil, "OVERLAY")
    sparkLine:SetTexture(1, 1, 1, 0.95)
    sparkLine:SetWidth(2)
    sparkLine:SetHeight(UI_MANA_BAR_HEIGHT + 6)
    sparkLine:Hide()

    local gain = holder:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    gain:SetPoint("BOTTOM", bar, "TOP", 0, 4)
    gain:SetTextColor(0.35, 1.00, 0.35)
    gain:Hide()

    EnableShiftLeftDrag(holder, "manaHUD")

    DW.manaTimerFrame = holder
    DW.manaTimerBar = bar
    DW.manaTimerBG = bg
    DW.resourceText = resourceText
    DW.manaTimerSpark = spark
    DW.manaTimerSparkLine = sparkLine
    DW.manaFlash = gain
    holder:Hide()
end

local function RefreshSettingsUI()
    if not DW.frame then return end
    if DW.statusLabel then DW.statusLabel:SetText("状态：" .. UserStatusText() .. "    " .. ResourceSummaryText()) end
    if DW.itemCurrentLabel then DW.itemCurrentLabel:SetText(CurrentItemText()) end
    if DW.bindingCurrentLabel then DW.bindingCurrentLabel:SetText("当前按键：" .. SpamBindingText()) end
    if DW.tickCheck then DW.tickCheck:SetChecked(ShowTickTimerEnabled()) end
    if DW.manaCheck then DW.manaCheck:SetChecked(ShowManaSparkEnabled()) end
end

local function CreateCheck(parent, name, text, x, y, checked, fn)
    local cb = CreateFrame("CheckButton", name, parent, "UICheckButtonTemplate")
    cb:SetPoint("TOPLEFT", parent, "TOPLEFT", x, y)
    cb:SetChecked(checked and true or false)
    local label = getglobal(name .. "Text")
    if label then label:SetText(text) end
    cb:SetScript("OnClick", function()
        fn(this:GetChecked() and true or false)
        LayoutHUD()
    end)
    return cb
end

local function CreateUI()
    CreateTickTimerHUD()
    CreateManaSparkHUD()

    local f = CreateFrame("Frame", "TaiYangDrinkWalkDiagFrame", UIParent)
    f:SetWidth(445)
    f:SetHeight(320)
    f:SetPoint("CENTER", UIParent, "CENTER", 0, 0)
    f:SetBackdrop({bgFile="Interface\\DialogFrame\\UI-DialogBox-Background", edgeFile="Interface\\DialogFrame\\UI-DialogBox-Border", tile=true, tileSize=32, edgeSize=32, insets={left=8,right=8,top=8,bottom=8}})
    f:SetMovable(true)
    f:EnableMouse(true)
    f:RegisterForDrag("LeftButton")
    f:SetScript("OnDragStart", function() this:StartMoving() end)
    f:SetScript("OnDragStop", function() this:StopMovingOrSizing() end)

    local title = f:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    title:SetPoint("TOPLEFT", f, "TOPLEFT", 18, -17)
    title:SetText("太阳神殿 DrinkWalk v" .. VERSION)

    local close = CreateFrame("Button", nil, f, "UIPanelCloseButton")
    close:SetPoint("TOPRIGHT", f, "TOPRIGHT", -4, -4)

    local status = CreateLabel(f, 20, -47, 390, "GameFontNormal")
    status:SetText("状态：--")
    DW.statusLabel = status

    DW.tickCheck = CreateCheck(f, "TaiYangDrinkWalkShowTickCheck", "显示 Tick 计时条（Mana/Energy）", 18, -73, ShowTickTimerEnabled(), SetTickTimerEnabled)
    DW.manaCheck = CreateCheck(f, "TaiYangDrinkWalkShowManaCheck", "显示 自适应资源条 / Spark", 18, -101, ShowManaSparkEnabled(), SetManaSparkEnabled)

    local ratioLabel = CreateLabel(f, 20, -136, 92, "GameFontNormalSmall")
    ratioLabel:SetText("吃喝窗口比例：")
    local ratioOpen = CreateFrame("EditBox", "TaiYangDrinkWalkRatioOpenEdit", f, "InputBoxTemplate")
    ratioOpen:SetWidth(48); ratioOpen:SetHeight(20)
    ratioOpen:SetPoint("TOPLEFT", f, "TOPLEFT", 112, -131)
    ratioOpen:SetAutoFocus(false); ratioOpen:SetText(string.format("%.1f", DrinkOpenPct()))
    DW.ratioOpenEdit = ratioOpen
    local ratioSep = CreateLabel(f, 166, -136, 18, "GameFontNormalSmall")
    ratioSep:SetText("% ~")
    local ratioClose = CreateFrame("EditBox", "TaiYangDrinkWalkRatioCloseEdit", f, "InputBoxTemplate")
    ratioClose:SetWidth(48); ratioClose:SetHeight(20)
    ratioClose:SetPoint("TOPLEFT", f, "TOPLEFT", 194, -131)
    ratioClose:SetAutoFocus(false); ratioClose:SetText(string.format("%.1f", DrinkClosePct()))
    DW.ratioCloseEdit = ratioClose
    local ratioPct = CreateLabel(f, 248, -136, 18, "GameFontNormalSmall")
    ratioPct:SetText("%")
    CreateButton(f, "应用", 274, -131, 60, function()
        if SetDrinkWindowRatioSpec(DW.ratioOpenEdit:GetText(), DW.ratioCloseEdit:GetText()) then RefreshSettingsUI() end
    end)
    local ratioHint = CreateLabel(f, 340, -136, 86, "GameFontNormalSmall")
    ratioHint:SetText("Mana/Energy")
    ratioOpen:SetScript("OnEnterPressed", function()
        SetDrinkWindowRatioSpec(this:GetText(), DW.ratioCloseEdit:GetText()); this:ClearFocus(); RefreshSettingsUI()
    end)
    ratioClose:SetScript("OnEnterPressed", function()
        SetDrinkWindowRatioSpec(DW.ratioOpenEdit:GetText(), this:GetText()); this:ClearFocus(); RefreshSettingsUI()
    end)
    ratioOpen:SetScript("OnEscapePressed", function() this:ClearFocus(); this:SetText(string.format("%.1f", DrinkOpenPct())) end)
    ratioClose:SetScript("OnEscapePressed", function() this:ClearFocus(); this:SetText(string.format("%.1f", DrinkClosePct())) end)

    local itemLabel = CreateLabel(f, 20, -174, 90, "GameFontNormalSmall")
    itemLabel:SetText("物品名称 / ID：")
    local itemEdit = CreateFrame("EditBox", "TaiYangDrinkWalkItemEdit", f, "InputBoxTemplate")
    itemEdit:SetWidth(230); itemEdit:SetHeight(20)
    itemEdit:SetPoint("TOPLEFT", f, "TOPLEFT", 112, -169)
    itemEdit:SetAutoFocus(false); itemEdit:SetText(tostring(TaiYangDrinkWalkDiagDB.itemSpec or ""))
    itemEdit:SetScript("OnEnterPressed", function() SetItemSpec(this:GetText()); this:ClearFocus(); RefreshSettingsUI() end)
    itemEdit:SetScript("OnEscapePressed", function() this:ClearFocus(); this:SetText(tostring(TaiYangDrinkWalkDiagDB.itemSpec or "")) end)
    DW.itemEdit = itemEdit
    CreateButton(f, "应用", 350, -169, 60, function() SetItemSpec(DW.itemEdit:GetText()); RefreshSettingsUI() end)

    local current = CreateLabel(f, 112, -195, 300)
    current:SetText("当前：未设置")
    DW.itemCurrentLabel = current

    local bindLabel = CreateLabel(f, 20, -225, 90, "GameFontNormalSmall")
    bindLabel:SetText("按键：")
    local bindEdit = CreateFrame("EditBox", "TaiYangDrinkWalkBindEdit", f, "InputBoxTemplate")
    bindEdit:SetWidth(86); bindEdit:SetHeight(20)
    bindEdit:SetPoint("TOPLEFT", f, "TOPLEFT", 112, -220)
    local initialBindText = TaiYangDrinkWalkDiagDB.bindKey
    if initialBindText == nil then
        local k1 = nil
        if type(GetBindingKey) == "function" then k1 = GetBindingKey("TAIYANGDRINKWALK_ACTION") end
        initialBindText = k1 or "F"
    end
    bindEdit:SetAutoFocus(false); bindEdit:SetText(tostring(initialBindText or ""))
    bindEdit:SetScript("OnEnterPressed", function() BindActionKey(this:GetText()); this:ClearFocus(); RefreshSettingsUI() end)
    bindEdit:SetScript("OnEscapePressed", function() this:ClearFocus() end)
    DW.bindEdit = bindEdit
    CreateButton(f, "绑定", 208, -220, 55, function() BindActionKey(DW.bindEdit:GetText()); RefreshSettingsUI() end)
    CreateButton(f, "解绑", 266, -220, 55, function() UnbindActionKey(false); RefreshSettingsUI() end)
    local bindCurrent = CreateLabel(f, 326, -225, 105)
    bindCurrent:SetText("当前按键：--")
    DW.bindingCurrentLabel = bindCurrent

    local macroLabel = CreateLabel(f, 20, -255, 390, "GameFontNormalSmall")
    macroLabel:SetText("宏命令：/zchihe    （与按键调用同一吃喝动作）")

    local fubarHint = CreateLabel(f, 20, -278, 255)
    fubarHint:SetText("FuBar：左键打开 / 关闭；隐藏后可恢复")
    CreateButton(f, "恢复酒杯", 285, -273, 80, function()
        if type(TaiYangDrinkWalk_ShowFuBar) == "function" then TaiYangDrinkWalk_ShowFuBar() end
    end)
    local sign = f:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    sign:SetPoint("BOTTOMRIGHT", f, "BOTTOMRIGHT", -20, 15)
    sign:SetText("太阳神殿")

    DW.frame = f
    f:Hide()
    LayoutHUD()
    RefreshSettingsUI()

    local driver = CreateFrame("Frame", "TaiYangDrinkWalkUIDriver", UIParent)
    driver:SetScript("OnUpdate", function()
        -- HUD animation runs at the client's render cadence. V0.4.2 throttled
        -- this to 30 Hz, which made the Spark visibly step across the bar.
        DW:RefreshHUD()

        DW.settingsAccum = (DW.settingsAccum or 0) + (arg1 or 0)
        if DW.settingsAccum >= SETTINGS_REFRESH_SEC then
            DW.settingsAccum = 0
            RefreshResourceContext(false)
            RefreshSettingsUI()
        end
    end)
    DW.uiDriver = driver
end

function DW:RefreshHUD()
    if not ShowTickTimerEnabled() and not ShowManaSparkEnabled() then return end
    local now = NowMS()

    -- DrinkWalk's consumption window remains the proven mana classifier.
    local manaNextTick = NextTickMS(now)
    if DW.resourceType == "MANA" and manaNextTick then EnsureSpamTick(manaNextTick) end

    local hudNextTick, hudTickIn, hudPeriod, hudKind = nil, nil, nil, DW.resourceType
    if DW.resourceType == "MANA" then
        if DW.state == "LOCKED" and manaNextTick then
            hudNextTick = manaNextTick
            hudTickIn = manaNextTick - now
            hudPeriod = PERIOD_MS
        end
    elseif DW.resourceType == "ENERGY" then
        hudNextTick, hudPeriod = EnergyNextTickMS(now)
        if hudNextTick then hudTickIn = hudNextTick - now end
    end

    -- Rage has no periodic +20 style tick to predict, so it intentionally
    -- renders only the live resource bar and hides both Sparks/tick countdown.
    RenderTickTimerUI(now, hudNextTick, hudTickIn, hudPeriod, hudKind)
    RenderManaTimerUI(now, hudNextTick, hudTickIn, hudPeriod, hudKind)
end

function TaiYangDrinkWalk_ToggleSettings()
    if not DW.frame then return end
    if DW.frame:IsVisible() then
        DW.frame:Hide()
    else
        RefreshSettingsUI()
        DW.frame:Show()
    end
end

local function SetFuBarIconVisible(show)
    local fu = getglobal("TaiYangDrinkWalkFu")
    if not fu then
        DW.lastKeyResult = "FuBar入口未加载（需要 !Libs/FuBarPlugin-2.0）"
        AddLog(DW.lastKeyResult)
        return false
    end
    local fn = show and fu.Show or fu.Hide
    if type(fn) ~= "function" then
        DW.lastKeyResult = "当前 FuBarPlugin 不支持图标显隐接口"
        AddLog(DW.lastKeyResult)
        return false
    end
    local ok, err = pcall(fn, fu)
    if not ok then
        DW.lastKeyResult = "FuBar图标操作失败：" .. tostring(err or "unknown")
        AddLog(DW.lastKeyResult)
        return false
    end
    DW.lastKeyResult = show and "FuBar酒杯已恢复" or "FuBar酒杯已隐藏；/tsdw icon show 可恢复"
    AddLog(DW.lastKeyResult)
    return true
end

function TaiYangDrinkWalk_ShowFuBar() return SetFuBarIconVisible(true) end
function TaiYangDrinkWalk_HideFuBar() return SetFuBarIconVisible(false) end

function TaiYangDrinkWalk_GetFuBarSummary()
    return UserStatusText(), CurrentItemText(), SpamBindingText(), ShowTickTimerEnabled(), ShowManaSparkEnabled(), ResourceSummaryText()
end

local function InitState()
    DW.logs = {}
    DW.rawManaLogs = {}
    DW.rawManaEventCount = 0
    DW.manaGainCount = 0
    DW.lastManaGainMS = nil
    DW.lastManaGainGapMS = nil
    DW.lastGainDelta = nil
    DW.lastGainMS = nil
    DW.lastRawManaMS = nil
    DW.lastRawManaDelta = nil
    DW.lastRawManaOld = nil
    DW.lastRawManaNew = nil
    DW.manaFlashUntilMS = nil
    DW.uiFSRStartMS = nil
    DW.uiFSREndMS = nil
    DW.state = "UNSYNCED"
    DW.anchorMS = nil
    DW.lastSampleMS = nil
    DW.lastAcceptedMS = nil
    DW.lastSampleDelta = nil
    DW.lastPhaseErrorMS = nil
    DW.confidence = 0
    DW.goodSamples = 0
    DW.badSamples = 0
    DW.resumeReason = "NORMAL_START"
    ResetClassifierRuntime()

    DW.mode = "SPAM" -- formal release default; hardware input is still required
    DW.item = nil
    DW.itemStatus = "未扫描"
    DW.drinkState = "UNKNOWN"
    DW.foodState = "UNKNOWN"
    DW.consumeState = "NONE"
    DW.drinkDetection = "未扫描"
    DW.spamTickMS = nil
    DW.spamConfirmedTickMS = nil
    DW.spamAttemptsThisTick = 0
    DW.spamRequestCount = 0
    DW.spamConfirmCount = 0
    DW.spamTickHadRequest = false
    DW.spamTickContextRejected = false
    DW.spamTickLearnableSuccess = false
    DW.spamTickSuccessLeadMS = nil
    DW.keyPressCount = 0
    DW.lastKeyResult = "SPAM默认启动"
    DW.recentDispatch = nil
    DW.lastDispatchMS = nil

    DW.autoLeadSuccessStreak = 0
    DW.autoLeadMissStreak = 0
    DW.autoLeadConfirmedTicks = 0
    DW.autoLeadAdjustDown = 0
    DW.autoLeadAdjustUp = 0
    DW.autoLeadSuccessLeads = {}
    DW.autoLeadLastChange = "START"

    DW.playerClassToken = nil
    DW.resourceType = nil
    DW.resourceValue = 0
    DW.resourceMax = 0
    DW.resourceEventValue = nil
    DW.resourceFlashKind = nil
    DW.resourceFlashDelta = nil
    DW.resourceFlashMS = nil
    DW.resourceFlashUntilMS = nil
    DW.energyTheoryTickMS = nil
    DW.energyTickMS = nil
    DW.energyBladeRushRank = 0
    DW.energyAgility = 0
    DW.energySamples = {}
    DW.energyObservedTickMS = nil
    DW.energyAnchorMS = nil
    DW.energyLastAcceptedGainMS = nil
    DW.lastMana = 0
    DW.maxMana = 0
    RefreshResourceContext(true)
end

local function OnEvent()
    local e = event
    if e == "VARIABLES_LOADED" then
        HookReloadUI()
        RefreshResourceContext(true)
        SetWatchLead(WatchLeadMS())
        SetSpamWindow(SpamOpenMS(), SpamCloseMS(), SpamRetryMS())
        if TaiYangDrinkWalkDiagDB.drinkOpenPct ~= nil and TaiYangDrinkWalkDiagDB.drinkClosePct ~= nil then
            SetDrinkWindowRatio(TaiYangDrinkWalkDiagDB.drinkOpenPct, TaiYangDrinkWalkDiagDB.drinkClosePct)
        else
            SetAutoLeadMS(AutoLeadMS(), "LOAD")
        end
        RestoreReloadPhase()
        ScanDrinkAura()
        NormalizeStoredBinding()
        SetItemSpec(TaiYangDrinkWalkDiagDB.itemSpec or "")
        DW.mode = "SPAM"
        DW.lastKeyResult = "SPAM默认启动；按键或 /zchihe 触发"
        SetTickTimerEnabled(ShowTickTimerEnabled())
        SetManaSparkEnabled(ShowManaSparkEnabled())
        LayoutHUD()
        RefreshSettingsUI()
        return
    end

    if e == "UNIT_MANA" then
        if arg1 and arg1 ~= "player" then return end
        local now = NowMS()
        local kind, newValue, maxValue = RefreshResourceContext(false)
        local oldResource = DW.resourceEventValue
        DW.resourceValue = newValue
        DW.resourceMax = maxValue
        DW.resourceEventValue = newValue

        if kind == "MANA" then
            local oldMana = DW.lastMana or newValue
            DW.lastMana = newValue
            DW.maxMana = maxValue
            if newValue ~= oldMana then OnManaChanged(oldMana, newValue, maxValue, now, "LUA_UNIT_MANA") end
        elseif oldResource ~= nil then
            -- Some vanilla clients can emit UNIT_MANA around a power-type
            -- transition.  Never feed those values into the mana classifier.
            DW.resourceEventValue = newValue
        end
        return
    end

    if e == "UNIT_MAXMANA" then
        if arg1 and arg1 ~= "player" then return end
        local kind, current, maximum = RefreshResourceContext(false)
        DW.resourceValue = current
        DW.resourceMax = maximum
        if kind == "MANA" then DW.maxMana = maximum end
        return
    end

    if e == "UNIT_ENERGY" then
        if arg1 and arg1 ~= "player" then return end
        local now = NowMS()
        local kind, newValue, maxValue = RefreshResourceContext(false)
        local oldValue = DW.resourceEventValue
        DW.resourceValue = newValue
        DW.resourceMax = maxValue
        if kind == "ENERGY" and oldValue ~= nil and newValue ~= oldValue then
            OnEnergyChanged(oldValue, newValue, now)
        end
        DW.resourceEventValue = newValue
        return
    end

    if e == "UNIT_RAGE" then
        if arg1 and arg1 ~= "player" then return end
        local now = NowMS()
        local kind, newValue, maxValue = RefreshResourceContext(false)
        local oldValue = DW.resourceEventValue
        DW.resourceValue = newValue
        DW.resourceMax = maxValue
        if kind == "RAGE" and oldValue ~= nil and newValue ~= oldValue then
            OnRageChanged(oldValue, newValue, now)
        end
        DW.resourceEventValue = newValue
        return
    end

    if e == "UNIT_DISPLAYPOWER" then
        if arg1 and arg1 ~= "player" then return end
        RefreshResourceContext(true)
        return
    end

    if e == "UNIT_STATS" then
        if arg1 and arg1 ~= "player" then return end
        if DW.resourceType == "ENERGY" then RecomputeEnergyModel(true) end
        return
    end

    if e == "CHARACTER_POINTS_CHANGED" then
        if DW.resourceType == "ENERGY" then RecomputeEnergyModel(true) end
        return
    end

    if e == "PLAYER_AURAS_CHANGED" then
        ScanDrinkAura()
        ConfirmFromDrinkAura()
        RefreshResourceContext(false)
        return
    end

    if e == "BAG_UPDATE" then
        ConfirmFromBagUpdate()
        FindConfiguredItem()
        return
    end

    if e == "UI_ERROR_MESSAGE" then
        local r = DW.recentDispatch
        if r and NowMS() - (r.requestedMS or 0) <= 400 then
            DW.spamTickContextRejected = true
            r.contextRejected = true
            DW.lastKeyResult = "本次尝试被拒绝：" .. tostring(arg1 or "UI_ERROR") .. "；该Tick不参与Lead降档"
        end
        return
    end

    if e == "PLAYER_DEAD" then
        ClearPhase("DEATH")
        ResetEnergyRuntime("DEATH")
        DW.mode = "SPAM"
        AddLog("死亡：旧phase作废；SPAM保持默认")
        return
    end

    if e == "PLAYER_ALIVE" or e == "PLAYER_UNGHOST" then
        if DW.state ~= "UNSYNCED" then ClearPhase("RESURRECT") end
        RefreshResourceContext(true)
        return
    end

    if e == "ZONE_CHANGED_NEW_AREA" then
        if DW.anchorMS and DW.state ~= "UNSYNCED" then
            DW.state = "VERIFY"
            DW.resumeReason = "ZONE_VERIFY"
            AddLog("区域变化：保留phase但进入VERIFY")
        end
        return
    end

    if e == "PLAYER_ENTERING_WORLD" then
        RefreshResourceContext(true)
        if DW.resourceType == "MANA" then
            DW.lastMana = DW.resourceValue or 0
            DW.maxMana = DW.resourceMax or 0
        end
        ScanDrinkAura()
        FindConfiguredItem()
        return
    end
end


local function SlashHandler(msg)
    msg = Trim(msg or "")
    local low = string.lower(msg)
    if low == "reset" then ResetDiagnostics(); return end
    if low == "show" then DW.frame:Show(); return end
    if low == "hide" then DW.frame:Hide(); return end
    if low == "scan" then SetItemSpec(TaiYangDrinkWalkDiagDB.itemSpec or ""); return end
    if low == "mode watch" or low == "watch" then SetMode("WATCH"); return end
    if low == "mode spam" or low == "spam" then SetMode("SPAM"); return end
    if low == "autolead on" then SetAutoLeadEnabled(true); return end
    if low == "autolead off" then SetAutoLeadEnabled(false); return end
    if low == "autolead reset" then ResetAutoLead(); return end
    if string.find(low, "autolead ") == 1 then SetAutoLeadMS(string.sub(msg, 10), "SLASH"); return end
    if string.find(low, "lead ") == 1 then SetWatchLead(string.sub(msg, 6)); return end
    if string.find(low, "item ") == 1 then SetItemSpec(string.sub(msg, 6)); return end
    if low == "unbind" then UnbindActionKey(false); RefreshSettingsUI(); return end
    if string.find(low, "bind ") == 1 then BindActionKey(string.sub(msg, 6)); RefreshSettingsUI(); return end
    if low == "icon show" then TaiYangDrinkWalk_ShowFuBar(); return end
    if low == "icon hide" then TaiYangDrinkWalk_HideFuBar(); return end
    if string.find(low, "window ") == 1 then SetSpamWindowSpec(string.sub(msg, 8)); return end
    if string.find(low, "ratio ") == 1 then
        local spec = string.sub(msg, 7)
        spec = string.gsub(spec, "，", ",")
        local _, _, a, b = string.find(spec, "^%s*([%d%.]+)%s*,%s*([%d%.]+)%s*$")
        if a and b then SetDrinkWindowRatio(a, b) else AddLog("比例命令：/tsdw ratio 84,91.5") end
        return
    end
    if low == "status" then
        DEFAULT_CHAT_FRAME:AddMessage("|cff33ff66[DrinkWalk]|r mode=" .. tostring(DW.mode) .. " sync=" .. tostring(DW.state) .. " key=" .. SpamBindingText() .. " item=" .. tostring(DW.itemStatus) .. " | " .. ResourceSummaryText())
        return
    end
    if DW.frame:IsVisible() then DW.frame:Hide() else DW.frame:Show() end
end

InitState()

local eventFrame = CreateFrame("Frame", "TaiYangDrinkWalkDiagEventFrame")
eventFrame:RegisterEvent("VARIABLES_LOADED")
eventFrame:RegisterEvent("UNIT_MANA")
eventFrame:RegisterEvent("UNIT_MAXMANA")
eventFrame:RegisterEvent("UNIT_ENERGY")
eventFrame:RegisterEvent("UNIT_RAGE")
eventFrame:RegisterEvent("UNIT_DISPLAYPOWER")
eventFrame:RegisterEvent("UNIT_STATS")
eventFrame:RegisterEvent("CHARACTER_POINTS_CHANGED")
eventFrame:RegisterEvent("BAG_UPDATE")
eventFrame:RegisterEvent("UI_ERROR_MESSAGE")
eventFrame:RegisterEvent("PLAYER_AURAS_CHANGED")
eventFrame:RegisterEvent("PLAYER_ENTERING_WORLD")
eventFrame:RegisterEvent("PLAYER_DEAD")
eventFrame:RegisterEvent("PLAYER_ALIVE")
eventFrame:RegisterEvent("PLAYER_UNGHOST")
eventFrame:RegisterEvent("ZONE_CHANGED_NEW_AREA")
eventFrame:SetScript("OnEvent", OnEvent)
DW.eventFrame = eventFrame

DW.scanTooltip = CreateFrame("GameTooltip", "TaiYangDrinkWalkScanTooltip", UIParent, "GameTooltipTemplate")
DW.scanTooltip:SetOwner(UIParent, "ANCHOR_NONE")
CreateUI()

SLASH_TAIYANGDRINKWALK1 = "/tsdw"
SLASH_TAIYANGDRINKWALK2 = "/drinkwalk"
SlashCmdList["TAIYANGDRINKWALK"] = SlashHandler

SLASH_TAIYANGZCHIHE1 = "/zchihe"
SlashCmdList["TAIYANGZCHIHE"] = function(msg)
    if DW and DW.HandleActionKey then DW:HandleActionKey() end
end

DEFAULT_CHAT_FRAME:AddMessage("|cff33ff66[太阳神殿 DrinkWalk]|r V0.5.2 已加载。Food/Drink识别；Mana/Energy吃喝窗口；按键替换/解绑；FuBar酒杯可恢复；/zchihe 吃喝；/tsdw 设置。")
