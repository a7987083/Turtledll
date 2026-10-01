-- TaiYangCastDiag
-- CAST1-R2 unified CastState acceptance panel for WoW 1.12 / Lua 5.0.
-- Core truth is event-driven. This panel performs read-side Cast.State queries
-- only when a TYS_CAST_* event arrives; it has no OnUpdate scan/poll loop.

local ui = {}
local test = {
    targetLiveState = false,
    interruptPreserved = false,
    selfSuccessPreserved = false,
    channelSeen = false,
    interruptedCaster = nil,
    regOk = 0,
    registrationDone = false,
    stateQueries = 0,
    lastSpell = 0,
    lastResult = "",
    lastPhase = ""
}

local function call(cmd, arg)
    if type(TaiYangShenDian) ~= "function" then return nil, "DLL_API_MISSING" end
    local ok, a, b, c = pcall(TaiYangShenDian, cmd, arg)
    if not ok then return nil, a end
    return a, b, c
end

local function n(v) return tonumber(v) or 0 end

local function setColor(fs, kind)
    if not fs then return end
    if kind == "green" then fs:SetTextColor(0.20, 1.00, 0.20)
    elseif kind == "yellow" then fs:SetTextColor(1.00, 0.82, 0.20)
    elseif kind == "red" then fs:SetTextColor(1.00, 0.25, 0.25)
    elseif kind == "gray" then fs:SetTextColor(0.65, 0.65, 0.65)
    else fs:SetTextColor(0.95, 0.95, 0.95) end
end

local function makeText(parent, x, y, w, justify, template)
    local fs = parent:CreateFontString(nil, "OVERLAY", template or "GameFontNormalSmall")
    fs:SetPoint("TOPLEFT", parent, "TOPLEFT", x, y)
    fs:SetWidth(w)
    fs:SetJustifyH(justify or "LEFT")
    fs:SetTextColor(0.92, 0.92, 0.92)
    return fs
end

local function mark(fs, ok)
    if ok then fs:SetText("✓ 已通过"); setColor(fs, "green")
    else fs:SetText("○ 等待"); setColor(fs, "yellow") end
end

local function stateByGuid(guid)
    if not guid then return nil end
    local r = call("Cast.State.GetByGuid", guid)
    if type(r) == "table" then
        test.stateQueries = test.stateQueries + 1
        test.lastSpell = n(r.spellId)
        test.lastResult = tostring(r.result or "")
        test.lastPhase = tostring(r.phase or "")
        return r
    end
    return nil
end

local function playerGuid()
    local st = call("Cast.Status")
    if type(st) == "table" then return st.playerGuid end
    return nil
end

local function targetGuid()
    local r = call("Cast.State.Get", "target")
    if type(r) == "table" then return r.casterGuid end
    return nil
end

local function refreshUI()
    if not ui.frame or not ui.frame:IsShown() then return end

    local ver, api = call("Core.Version")
    local cast = call("Cast.Status")
    local state = call("Cast.State.Status")
    local bus = call("Aura.NativeBus.Status")

    if ver then
        ui.version:SetText(tostring(ver) .. "   API " .. tostring(api or "?"))
        setColor(ui.version, "gray")
    else
        ui.version:SetText("DLL API 不可用")
        setColor(ui.version, "red")
    end

    local stateReady = type(state) == "table" and state.status == "READY_UNIFIED_CAST_STATE"
    local coreReady = type(cast) == "table" and cast.customEventsReady and cast.dynamicEventSlots and cast.clearCastingHook
    local parseFail = type(cast) == "table" and n(cast.parseFailure) or 0
    local overrun = type(bus) == "table" and n(bus.cursorOverruns) or 0
    local clockOk = type(state) == "table" and state.clock == "CLIENT_ENGINE_MS"

    mark(ui.rowLive, test.targetLiveState)
    mark(ui.rowInterrupt, test.interruptPreserved)
    mark(ui.rowSuccess, test.selfSuccessPreserved)

    if stateReady and coreReady and clockOk and parseFail == 0 and overrun == 0 and
       test.targetLiveState and test.interruptPreserved and test.selfSuccessPreserved then
        ui.summary:SetText("● CAST1-R2 完整通过")
        setColor(ui.summary, "green")
        ui.hint:SetText("CastState 已闭环：读条时是 CASTING，脚踢结束后仍保留 INTERRUPTED，自身成功后仍保留 SUCCESS。")
        setColor(ui.hint, "green")
    elseif not stateReady then
        ui.summary:SetText("● CastState 未就绪")
        setColor(ui.summary, "red")
        ui.hint:SetText("Cast.State.Status 未返回 READY_UNIFIED_CAST_STATE，请截图。")
        setColor(ui.hint, "red")
    elseif not coreReady or parseFail > 0 or overrun > 0 then
        ui.summary:SetText("● 底层异常")
        setColor(ui.summary, "red")
        ui.hint:SetText("事件槽 / 打断入口 / 解析状态异常，请截图。")
        setColor(ui.hint, "red")
    elseif not clockOk then
        ui.summary:SetText("● 客户端时钟未启用")
        setColor(ui.summary, "red")
        ui.hint:SetText("R2 应使用 CLIENT_ENGINE_MS，与 Lua GetTime() 保持同一时钟域。")
        setColor(ui.hint, "red")
    else
        ui.summary:SetText("● 等待三步测试")
        setColor(ui.summary, "yellow")
        ui.hint:SetText("还是原来的三步：目标读条 → 脚踢 → 自己成功放一次技能。")
        setColor(ui.hint, "yellow")
    end

    if type(state) == "table" and type(cast) == "table" and type(bus) == "table" then
        ui.core:SetText("统一状态核心 " .. (stateReady and "正常" or "异常") ..
            "   客户端时钟 " .. (clockOk and "正常" or tostring(state.clock or "异常")) ..
            "   动态事件注册 " .. tostring(test.regOk) .. "/10")
        if stateReady and clockOk and test.regOk == 10 then setColor(ui.core, "green") else setColor(ui.core, "red") end

        ui.safety:SetText("底层：解析失败 " .. tostring(math.floor(parseFail)) ..
            "   游标越界 " .. tostring(math.floor(overrun)) ..
            "   打断入口 " .. (cast.clearCastingHook and "正常" or "异常"))
        if parseFail == 0 and overrun == 0 and cast.clearCastingHook then setColor(ui.safety, "green") else setColor(ui.safety, "red") end

        ui.detail:SetText("最近状态：phase=" .. tostring(test.lastPhase) ..
            "  result=" .. tostring(test.lastResult) ..
            "  spell=" .. tostring(test.lastSpell) ..
            "  查询次数=" .. tostring(test.stateQueries))
        setColor(ui.detail, "gray")
    end

    if test.channelSeen then
        ui.optional:SetText("可选：已观察到 CHANNELING")
        setColor(ui.optional, "green")
    else
        ui.optional:SetText("可选：引导施法未测（不影响 R2 通过）")
        setColor(ui.optional, "gray")
    end
end

local function resetTest()
    test.targetLiveState = false
    test.interruptPreserved = false
    test.selfSuccessPreserved = false
    test.channelSeen = false
    test.interruptedCaster = nil
    test.stateQueries = 0
    test.lastSpell = 0
    test.lastResult = ""
    test.lastPhase = ""
    refreshUI()
end

local function createUI()
    if ui.frame then return end
    local f = CreateFrame("Frame", "TaiYangCastDiagFrame", UIParent)
    f:SetWidth(650); f:SetHeight(500)
    f:SetPoint("CENTER", UIParent, "CENTER", 0, 30)
    f:SetMovable(true); f:EnableMouse(true); f:RegisterForDrag("LeftButton")
    f:SetScript("OnDragStart", function() this:StartMoving() end)
    f:SetScript("OnDragStop", function() this:StopMovingOrSizing() end)
    f:SetBackdrop({
        bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
        edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
        tile = true, tileSize = 32, edgeSize = 32,
        insets = { left = 11, right = 12, top = 12, bottom = 11 }
    })
    f:SetBackdropColor(0.05, 0.05, 0.05, 0.96)
    f:SetFrameStrata("DIALOG")
    ui.frame = f

    local title = f:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    title:SetPoint("TOPLEFT", f, "TOPLEFT", 20, -18)
    title:SetText("太阳神殿 · CAST1-R2 统一施法状态")
    title:SetTextColor(1.00, 0.82, 0.20)

    ui.summary = f:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    ui.summary:SetPoint("TOPRIGHT", f, "TOPRIGHT", -20, -20)
    ui.summary:SetText("● 检测中")
    setColor(ui.summary, "yellow")

    ui.version = makeText(f, 22, -48, 430, "LEFT")

    local how = makeText(f, 25, -78, 580, "LEFT", "GameFontNormal")
    how:SetText("还是只测 3 步；这轮测试的是“状态是否能被插件稳定查询”")
    setColor(how, "yellow")

    local steps = makeText(f, 35, -108, 565, "LEFT")
    steps:SetHeight(58); steps:SetJustifyV("TOP")
    steps:SetText("1. 让当前目标开始一次有读条的施法。\n2. 下一次读条时用脚踢打断。\n3. 自己成功释放一次技能（瞬发也可以）。")

    local line = f:CreateTexture(nil, "ARTWORK")
    line:SetTexture(0.35, 0.35, 0.35, 0.8); line:SetHeight(1)
    line:SetPoint("TOPLEFT", f, "TOPLEFT", 22, -166)
    line:SetPoint("TOPRIGHT", f, "TOPRIGHT", -22, -166)

    local l1 = makeText(f, 32, -198, 390, "LEFT"); l1:SetText("① 读条中查询为 CASTING / active")
    local l2 = makeText(f, 32, -232, 390, "LEFT"); l2:SetText("② 脚踢 + STOP 后仍保留 INTERRUPTED")
    local l3 = makeText(f, 32, -266, 390, "LEFT"); l3:SetText("③ 自身技能结束后仍保留 SUCCESS")
    ui.rowLive = makeText(f, 455, -198, 145, "RIGHT")
    ui.rowInterrupt = makeText(f, 455, -232, 145, "RIGHT")
    ui.rowSuccess = makeText(f, 455, -266, 145, "RIGHT")

    ui.core = makeText(f, 25, -310, 600, "LEFT")
    ui.safety = makeText(f, 25, -338, 600, "LEFT")
    ui.detail = makeText(f, 25, -366, 600, "LEFT")
    ui.optional = makeText(f, 25, -394, 600, "LEFT")

    ui.hint = makeText(f, 25, -422, 600, "LEFT")

    local reset = CreateFrame("Button", nil, f, "UIPanelButtonTemplate")
    reset:SetWidth(92); reset:SetHeight(24)
    reset:SetPoint("BOTTOMRIGHT", f, "BOTTOMRIGHT", -122, 16)
    reset:SetText("重新测试")
    reset:SetScript("OnClick", resetTest)

    local close = CreateFrame("Button", nil, f, "UIPanelButtonTemplate")
    close:SetWidth(82); close:SetHeight(24)
    close:SetPoint("BOTTOMRIGHT", f, "BOTTOMRIGHT", -26, 16)
    close:SetText("关闭")
    close:SetScript("OnClick", function() ui.frame:Hide() end)
end

local function registerCastEvents(frame)
    if test.registrationDone then return end
    local names = {
        "TYS_CAST_SENT", "TYS_CAST_START", "TYS_CAST_SUCCESS", "TYS_CAST_FAILED",
        "TYS_CAST_INTERRUPTED", "TYS_CAST_STOP", "TYS_CHANNEL_START",
        "TYS_CHANNEL_UPDATE", "TYS_CHANNEL_STOP", "TYS_CAST_DELAYED"
    }
    test.regOk = 0
    for i = 1, table.getn(names) do
        local ok = pcall(function() frame:RegisterEvent(names[i]) end)
        if ok then test.regOk = test.regOk + 1 end
    end
    test.registrationDone = test.regOk == table.getn(names)
end

local function onCastEvent()
    local caster = arg1
    local spellId = n(arg3)

    if event == "TYS_CAST_START" then
        local tg = targetGuid()
        local r = stateByGuid(caster)
        if r and tg and caster == tg and r.active and r.phase == "CASTING" and n(r.spellId) == spellId then
            test.targetLiveState = true
        end
    elseif event == "TYS_CAST_INTERRUPTED" then
        local tg = targetGuid()
        if tg and caster == tg then test.interruptedCaster = caster end
        stateByGuid(caster)
    elseif event == "TYS_CAST_STOP" then
        local r = stateByGuid(caster)
        if test.interruptedCaster and caster == test.interruptedCaster and r and
           r.phase == "IDLE" and r.result == "INTERRUPTED" then
            test.interruptPreserved = true
        end
    elseif event == "TYS_CAST_SUCCESS" then
        local r = stateByGuid(caster)
        local pg = playerGuid()
        if pg and caster == pg and r and r.result == "SUCCESS" then
            test.selfSuccessPreserved = true
        end
    elseif event == "TYS_CHANNEL_START" then
        local r = stateByGuid(caster)
        if r and r.phase == "CHANNELING" and r.active then test.channelSeen = true end
    elseif event == "TYS_CHANNEL_UPDATE" or event == "TYS_CHANNEL_STOP" or
           event == "TYS_CAST_FAILED" or event == "TYS_CAST_DELAYED" then
        stateByGuid(caster)
    end
    refreshUI()
end

local function showUI()
    createUI()
    ui.frame:Show()
    refreshUI()
end

SLASH_TYSCAST11 = "/cast1"
SlashCmdList["TYSCAST1"] = function(msg)
    createUI()
    if ui.frame:IsShown() then ui.frame:Hide() else showUI() end
end

local ef = CreateFrame("Frame", "TaiYangCastDiagEventFrame")
ef:RegisterEvent("VARIABLES_LOADED")
ef:RegisterEvent("PLAYER_ENTERING_WORLD")
ef:SetScript("OnEvent", function()
    if event == "VARIABLES_LOADED" then
        createUI()
        call("Cast.Status")
        registerCastEvents(this)
    elseif event == "PLAYER_ENTERING_WORLD" then
        registerCastEvents(this)
        showUI()
    else
        onCastEvent()
    end
end)
