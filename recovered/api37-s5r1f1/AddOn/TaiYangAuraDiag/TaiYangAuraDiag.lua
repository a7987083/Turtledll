-- TaiYangAuraDiag
-- AURA6-D4-R4 final lifecycle verification panel for WoW 1.12 / Lua 5.0.
-- Core remains event-driven; this test UI only re-reads Aura.List to render a countdown.

local REFRESH_SEC = 0.10
local MAX_ROWS = 6
local ui = {}

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

local function fmtSec(ms)
    if not ms or n(ms) <= 0 then return "--" end
    return string.format("%.1f 秒", n(ms) / 1000)
end

local function qualityText(row)
    if row.timeQuality == "PREDICTED" then
        if row.timeSource == "LOCAL_COMBO_SCALED" then return "组合点预测", "green" end
        if row.timeSource == "LOCAL_CAST_MODIFIED" then return "本机预测", "green" end
        if row.timeSource == "REMOTE_BASE" then return "远端基础预测", "yellow" end
        return "预测", "green"
    end
    if row.timeQuality == "EXACT" then return "精确", "green" end
    return "未知", "gray"
end

local function clearRows()
    for i = 1, MAX_ROWS do
        local r = ui.rows[i]
        r.name:SetText(""); r.id:SetText(""); r.duration:SetText("")
        r.remaining:SetText(""); r.quality:SetText("")
    end
end

local function refreshUI()
    if not ui.frame or not ui.frame:IsShown() then return end

    local ver, api = call("Core.Version")
    if ver then
        ui.version:SetText(tostring(ver) .. "   API " .. tostring(api or "?"))
        setColor(ui.version, "gray")
    else
        ui.version:SetText("DLL API 不可用"); setColor(ui.version, "red")
    end

    local list = call("Aura.List", "target")
    local src = call("Aura.Source.Status")
    local bus = call("Aura.NativeBus.Status")
    clearRows()

    local parseFail = type(src) == "table" and n(src.parseFailure) or 0
    local overrun = type(bus) == "table" and n(bus.cursorOverruns) or 0
    if parseFail > 0 or overrun > 0 then
        ui.summary:SetText("● 底层异常"); setColor(ui.summary, "red")
        ui.hint:SetText("解析失败或游标越界不为 0，请直接截这个窗口给我。")
        setColor(ui.hint, "red")
        return
    end

    if type(list) ~= "table" then
        ui.summary:SetText("● 请选择目标"); setColor(ui.summary, "yellow")
        ui.hint:SetText("选中一个怪物，然后先打 1 星割裂。")
        setColor(ui.hint, "yellow")
        return
    end

    local shown = 0
    local predicted = 0
    local combo = 0
    for i = 1, n(list.count) do
        local a = list[i]
        if type(a) == "table" and a.isDebuff and a.isMine then
            shown = shown + 1
            if shown <= MAX_ROWS then
                local r = ui.rows[shown]
                local name = tostring(a.name or "")
                if name == "" then name = "自己的减益" end
                r.name:SetText(name)
                r.id:SetText(tostring(math.floor(n(a.spellId))))
                r.duration:SetText(fmtSec(a.durationMs))
                r.remaining:SetText(fmtSec(a.remainingMs))
                local qt, qc = qualityText(a)
                r.quality:SetText(qt); setColor(r.quality, qc)
                if a.timeQuality == "PREDICTED" then predicted = predicted + 1 end
                if a.timeSource == "LOCAL_COMBO_SCALED" then combo = combo + 1 end
            end
        end
    end

    if combo > 0 then
        ui.summary:SetText("● Aura 计时正常"); setColor(ui.summary, "green")
        ui.hint:SetText("继续按上面的刷新 / 切目标 / World切换测试；三项都正常即可验收 R4。")
        setColor(ui.hint, "green")
    elseif predicted > 0 then
        ui.summary:SetText("● Aura 预测正常"); setColor(ui.summary, "green")
        ui.hint:SetText("继续测试刷新、切目标和 World 切换。")
        setColor(ui.hint, "yellow")
    elseif shown > 0 then
        ui.summary:SetText("● Aura 已识别"); setColor(ui.summary, "yellow")
        ui.hint:SetText("重新施放一次割裂，然后测试刷新和切目标。")
        setColor(ui.hint, "yellow")
    else
        ui.summary:SetText("● 等待目标 Aura"); setColor(ui.summary, "yellow")
        ui.hint:SetText("先给目标上一次割裂，然后按上面的 3 项测试。")
        setColor(ui.hint, "yellow")
    end
end

local function makeText(parent, x, y, w, justify)
    local fs = parent:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    fs:SetPoint("TOPLEFT", parent, "TOPLEFT", x, y)
    fs:SetWidth(w); fs:SetJustifyH(justify or "LEFT")
    fs:SetTextColor(0.92, 0.92, 0.92)
    return fs
end

local function createUI()
    if ui.frame then return end
    local f = CreateFrame("Frame", "TaiYangAuraDiagFrame", UIParent)
    f:SetWidth(600); f:SetHeight(390)
    f:SetPoint("CENTER", UIParent, "CENTER", 0, 20)
    f:SetMovable(true); f:EnableMouse(true); f:RegisterForDrag("LeftButton")
    f:SetScript("OnDragStart", function() this:StartMoving() end)
    f:SetScript("OnDragStop", function() this:StopMovingOrSizing() end)
    f:SetBackdrop({
        bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
        edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
        tile = true, tileSize = 32, edgeSize = 32,
        insets = { left = 11, right = 12, top = 12, bottom = 11 }
    })
    f:SetBackdropColor(0.05, 0.05, 0.05, 0.96); f:SetFrameStrata("DIALOG")
    ui.frame = f

    local title = f:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    title:SetPoint("TOPLEFT", f, "TOPLEFT", 20, -18)
    title:SetText("太阳神殿 · Aura 最终生命周期")
    title:SetTextColor(1.00, 0.82, 0.20)
    ui.summary = f:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    ui.summary:SetPoint("TOPRIGHT", f, "TOPRIGHT", -20, -20)
    ui.summary:SetText("● 检测中"); setColor(ui.summary, "yellow")
    ui.version = makeText(f, 22, -47, 360, "LEFT")

    local how = f:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    how:SetPoint("TOPLEFT", f, "TOPLEFT", 22, -76)
    how:SetText("R4 只测试这 3 件事")
    how:SetTextColor(1.00, 0.82, 0.20)
    local steps = makeText(f, 35, -103, 535, "LEFT")
    steps:SetHeight(58); steps:SetJustifyV("TOP")
    steps:SetText("1. 刷新：割裂还没消失时再上一次，剩余时间应重新变长，不应多出重复条。\n2. 切目标：两只怪分别上割裂，来回切换，各自时间不能串。\n3. World切换：进出副本/炉石后再上割裂，不应出现上个地图的旧 Aura。")

    local line = f:CreateTexture(nil, "ARTWORK")
    line:SetTexture(0.35,0.35,0.35,0.8); line:SetHeight(1)
    line:SetPoint("TOPLEFT", f, "TOPLEFT", 22, -166); line:SetPoint("TOPRIGHT", f, "TOPRIGHT", -22, -166)

    local h1=makeText(f,28,-185,205,"LEFT"); h1:SetText("技能")
    local h2=makeText(f,235,-185,55,"RIGHT"); h2:SetText("ID")
    local h3=makeText(f,305,-185,80,"RIGHT"); h3:SetText("总时长")
    local h4=makeText(f,400,-185,80,"RIGHT"); h4:SetText("剩余")
    local h5=makeText(f,495,-185,80,"RIGHT"); h5:SetText("质量")
    setColor(h1,"yellow");setColor(h2,"yellow");setColor(h3,"yellow");setColor(h4,"yellow");setColor(h5,"yellow")

    ui.rows = {}
    for i=1,MAX_ROWS do
        local y = -211 - (i-1)*24
        local r = {}
        r.name = makeText(f,28,y,205,"LEFT")
        r.id = makeText(f,235,y,55,"RIGHT")
        r.duration = makeText(f,305,y,80,"RIGHT")
        r.remaining = makeText(f,400,y,80,"RIGHT")
        r.quality = makeText(f,495,y,80,"RIGHT")
        ui.rows[i] = r
    end

    ui.hint = f:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    ui.hint:SetPoint("BOTTOMLEFT", f, "BOTTOMLEFT", 25, 47)
    ui.hint:SetWidth(450); ui.hint:SetJustifyH("LEFT")

    local close = CreateFrame("Button", nil, f, "UIPanelButtonTemplate")
    close:SetWidth(90); close:SetHeight(24); close:SetPoint("BOTTOMRIGHT", f, "BOTTOMRIGHT", -24, 16)
    close:SetText("关闭"); close:SetScript("OnClick", function() ui.frame:Hide() end)

    f.acc = 0
    f:SetScript("OnUpdate", function()
        this.acc = (this.acc or 0) + (arg1 or 0)
        if this.acc >= REFRESH_SEC then this.acc = 0; refreshUI() end
    end)
end

local function showUI()
    createUI(); ui.frame:Show(); refreshUI()
end

SLASH_TYSAURA6R41 = "/aura6r4"
SlashCmdList["TYSAURA6R4"] = function(msg)
    createUI()
    if ui.frame:IsShown() then ui.frame:Hide() else showUI() end
end

local ef = CreateFrame("Frame", "TaiYangAuraDiagEventFrame")
ef:RegisterEvent("VARIABLES_LOADED"); ef:RegisterEvent("PLAYER_ENTERING_WORLD")
ef:SetScript("OnEvent", function()
    if event == "VARIABLES_LOADED" then createUI()
    elseif event == "PLAYER_ENTERING_WORLD" then showUI() end
end)
