-- 太阳神殿 尸体模型选择器 v0.3.0 / Turtle WoW 1.12 / TaiYangShenDian API32
-- 固定模型预设版。只开放尸体模型选择；采集链始终恢复原版 LootFX。

local ADDON = "TYSLootFXSelector"
local PREFIX = "|cff55dfff[尸体标记]|r "
local DEFAULT_CORPSE = "Particles\\TaiYangCorpse\\LootFX.mdl"
local STOCK = "Particles\\LootFX.mdl"

-- 固定模型预设。路径直接交给 DLL；DLL 内部统一归一化为 .mdl。
local PRESETS = {
    { name="厄运之槌水晶发生器", path="WORLD\\KALIMDOR\\DIREMAUL\\ACTIVEDOODADS\\LIGHTCRYSTAL\\DIREMAULCRYSTALGENERATOR.M2" },
    { name="部落 CTF 旗帜", path="SPELLS\\HordeCTFflag_spell.M2" },
    { name="联盟 CTF 旗帜", path="SPELLS\\AllianceCTFflag_spell.M2" },
    { name="中立 CTF 旗帜", path="SPELLS\\Neutralctfflag_spell.m2" },
    { name="绿色部落旗帜", path="SPELLS\\GreenHordeflag_spell.m2" },
    { name="金色联盟旗帜", path="SPELLS\\GoldAllianceFlag_spell.m2" },
    { name="金色部落旗帜", path="SPELLS\\GoldHordeflag_spell.m2" },
    { name="联盟世界 CTF 旗帜", path="World\\Generic\\PVP\\CTFlags\\AllianceCTFflag.m2" },
    { name="联盟狮徽 CTF 旗帜", path="world\\generic\\pvp\\ctfflags\\alliancectfflaglion.m2" },
    { name="灵魂之井", path="Spells\\WellOfSouls_Base.m2" },
    { name="削弱状态模型", path="Spells\\Nerf_State.m2" },
    { name="情人节爱心", path="Spells\\Holidays\\Valentines_LookingForLoveHeart.m2" },
    { name="生物法术传送门", path="Creature\\Spells\\Creature_SpellPortal.m2" },
    { name="蓝色法术传送门", path="Spells\\Creature_SpellPortal_Blue.m2" },
    { name="紫色法术传送门", path="SPELLS\\Creature_SpellPortal_Purple.m2" },
}

local db
local main
local picker
local corpsePathText
local corpseNameText
local statusText
local statusDot

local function chat(msg)
    if DEFAULT_CHAT_FRAME then
        DEFAULT_CHAT_FRAME:AddMessage(PREFIX .. tostring(msg))
    end
end

local function api(cmd, arg)
    if type(TaiYangShenDian) ~= "function" then
        return nil, "DLL_API_MISSING"
    end
    if arg ~= nil then return TaiYangShenDian(cmd, arg) end
    return TaiYangShenDian(cmd)
end

local function apiVersionOK()
    if type(TaiYangShenDian) ~= "function" then return false, "太阳神殿 DLL API 不存在" end
    local dll, apiVer, build = api("Core.Version")
    local n = tonumber(apiVer or "0") or 0
    if n < 32 then return false, "需要 API32，当前 API=" .. tostring(apiVer) end
    return true, tostring(dll) .. " / API" .. tostring(apiVer)
end

local function basename(path)
    if not path then return "-" end
    local p = string.gsub(path, "/", "\\")
    local last = p
    local i = 1
    while true do
        local a, b = string.find(p, "\\", i, true)
        if not a then break end
        last = string.sub(p, b + 1)
        i = b + 1
    end
    return last
end

local function presetName(path)
    if not path then return "-" end
    local low = string.lower(string.gsub(path, "/", "\\"))
    local i
    for i = 1, table.getn(PRESETS) do
        local p = string.lower(string.gsub(PRESETS[i].path, "/", "\\"))
        p = string.gsub(p, "%.m2$", ".mdl")
        local q = string.gsub(low, "%.m2$", ".mdl")
        if p == q then return PRESETS[i].name end
    end
    if low == string.lower(DEFAULT_CORPSE) then return "默认尸体旗帜" end
    if low == string.lower(STOCK) then return "原版尸体光效" end
    return basename(path)
end

local function setStatus(text, good)
    if statusText then
        statusText:SetText((good and "|cff62f5ff" or "|cffff6262") .. tostring(text) .. "|r")
    end
    if statusDot then
        if good then statusDot:SetVertexColor(0.15, 0.95, 1.0, 1.0)
        else statusDot:SetVertexColor(1.0, 0.20, 0.20, 1.0) end
    end
end

local function refreshTexts()
    if not db then return end
    if corpsePathText then corpsePathText:SetText(db.corpsePath or DEFAULT_CORPSE) end
    if corpseNameText then corpseNameText:SetText(presetName(db.corpsePath or DEFAULT_CORPSE)) end
end

local function applyCorpse(mode, path, quiet)
    local ok, code, normalized
    if mode == "stock" then
        ok, code, normalized = api("LootFX.Corpse.Stock")
    elseif mode == "custom" then
        ok, code, normalized = api("LootFX.Corpse.Set", path or "")
    else
        mode = "default"
        ok, code, normalized = api("LootFX.Corpse.Default")
    end
    if ok then
        db.corpseMode = mode
        db.corpsePath = normalized
        refreshTexts()
        if not quiet then chat("尸体模型已切换为：" .. presetName(normalized)) end
        setStatus("尸体设置已写入，新生成的可拾取尸体生效", true)
    else
        if not quiet then chat("尸体设置失败：" .. tostring(code)) end
        setStatus("尸体设置失败：" .. tostring(code), false)
    end
    return ok
end

-- V0.3 不再开放采集模型修改。每次加载都主动恢复原版，
-- 兼容 V0.2 曾保存/启用过自定义采集模型的用户。
local function forceGatherStock(quiet)
    local ok, code, normalized = api("LootFX.Gather.Stock")
    if ok then
        db.gatherMode = "stock"
        db.gatherPath = normalized or STOCK
    elseif not quiet then
        chat("恢复采集原版光失败：" .. tostring(code))
    end
    return ok
end

local function applySaved()
    local ok, info = apiVersionOK()
    if not ok then setStatus(info, false); return end
    forceGatherStock(true)
    applyCorpse(db.corpseMode or "default", db.corpsePath or DEFAULT_CORPSE, true)
    refreshTexts()
    setStatus("已连接  |  " .. info .. "  |  采集保持原版光", true)
end

local function makeSolid(parent, layer, r, g, b, a)
    local t = parent:CreateTexture(nil, layer or "BACKGROUND")
    t:SetTexture("Interface\\ChatFrame\\ChatFrameBackground")
    t:SetVertexColor(r, g, b, a or 1)
    return t
end

local function makeLabel(parent, text, x, y, font)
    local t = parent:CreateFontString(nil, "OVERLAY", font or "GameFontNormal")
    t:SetPoint("TOPLEFT", parent, "TOPLEFT", x, y)
    t:SetText(text)
    return t
end

local function makeTechButton(parent, text, w, h)
    local b = CreateFrame("Button", nil, parent)
    b:SetWidth(w); b:SetHeight(h)
    b:SetBackdrop({bgFile="Interface\\Tooltips\\UI-Tooltip-Background", edgeFile="Interface\\Tooltips\\UI-Tooltip-Border", tile=true, tileSize=8, edgeSize=10, insets={left=2,right=2,top=2,bottom=2}})
    b:SetBackdropColor(0.015, 0.055, 0.085, 0.96)
    b:SetBackdropBorderColor(0.05, 0.58, 0.76, 0.95)
    local fs = b:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    fs:SetPoint("CENTER", b, "CENTER", 0, 0)
    fs:SetText(text)
    fs:SetTextColor(0.55, 0.92, 1.0)
    b.label = fs
    local hl = b:CreateTexture(nil, "HIGHLIGHT")
    hl:SetTexture("Interface\\ChatFrame\\ChatFrameBackground")
    hl:SetAllPoints(b)
    hl:SetVertexColor(0.08, 0.65, 0.90, 0.22)
    return b
end

local function makeSection(parent, x, y, w, h, title)
    local f = CreateFrame("Frame", nil, parent)
    f:SetPoint("TOPLEFT", parent, "TOPLEFT", x, y)
    f:SetWidth(w); f:SetHeight(h)
    f:SetBackdrop({bgFile="Interface\\Tooltips\\UI-Tooltip-Background", edgeFile="Interface\\Tooltips\\UI-Tooltip-Border", tile=true, tileSize=8, edgeSize=10, insets={left=2,right=2,top=2,bottom=2}})
    f:SetBackdropColor(0.008, 0.025, 0.045, 0.88)
    f:SetBackdropBorderColor(0.04, 0.42, 0.58, 0.88)
    local top = makeSolid(f, "ARTWORK", 0.08, 0.70, 0.92, 0.9)
    top:SetPoint("TOPLEFT", f, "TOPLEFT", 1, -1); top:SetPoint("TOPRIGHT", f, "TOPRIGHT", -1, -1); top:SetHeight(2)
    local ttl = f:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    ttl:SetPoint("TOPLEFT", f, "TOPLEFT", 14, -13); ttl:SetText(title); ttl:SetTextColor(0.35, 0.90, 1.0)
    return f
end

local function closePicker()
    if picker then picker:Hide() end
end

local function selectPreset(index)
    local p = PRESETS[index]
    if not p then return end
    applyCorpse("custom", p.path, false)
    closePicker()
end

local function openPicker()
    if picker then picker:Show() end
end

local function buildPicker()
    picker = CreateFrame("Frame", "TYSLootFXPresetPicker", UIParent)
    picker:SetWidth(520); picker:SetHeight(470)
    picker:SetPoint("CENTER", UIParent, "CENTER", 170, 10)
    picker:SetFrameStrata("FULLSCREEN_DIALOG")
    picker:SetBackdrop({bgFile="Interface\\Tooltips\\UI-Tooltip-Background", edgeFile="Interface\\Tooltips\\UI-Tooltip-Border", tile=true, tileSize=16, edgeSize=12, insets={left=3,right=3,top=3,bottom=3}})
    picker:SetBackdropColor(0.005, 0.018, 0.030, 0.98)
    picker:SetBackdropBorderColor(0.08, 0.72, 0.92, 1.0)
    picker:EnableMouse(true); picker:SetMovable(true); picker:RegisterForDrag("LeftButton")
    picker:SetScript("OnDragStart", function() this:StartMoving() end)
    picker:SetScript("OnDragStop", function() this:StopMovingOrSizing() end)
    picker:Hide()

    local top = makeSolid(picker, "ARTWORK", 0.05, 0.78, 1.0, 0.95)
    top:SetPoint("TOPLEFT", picker, "TOPLEFT", 4, -4); top:SetPoint("TOPRIGHT", picker, "TOPRIGHT", -4, -4); top:SetHeight(3)

    local title = makeLabel(picker, "尸体模型预设", 18, -18, "GameFontNormalLarge")
    title:SetTextColor(0.40, 0.94, 1.0)
    local sub = makeLabel(picker, "固定模型列表 · 点击立即应用", 292, -22, "GameFontHighlightSmall")
    sub:SetTextColor(0.25, 0.78, 0.96)

    local i
    for i = 1, table.getn(PRESETS) do
        local b = makeTechButton(picker, PRESETS[i].name, 478, 23)
        b:SetPoint("TOPLEFT", picker, "TOPLEFT", 20, -52 - (i - 1) * 25)
        b.label:SetJustifyH("LEFT")
        b.label:ClearAllPoints(); b.label:SetPoint("LEFT", b, "LEFT", 10, 0)
        b.index = i
        b:SetScript("OnClick", function() selectPreset(this.index) end)
    end

    local close = makeTechButton(picker, "关闭", 82, 22)
    close:SetPoint("BOTTOMRIGHT", picker, "BOTTOMRIGHT", -18, 15)
    close:SetScript("OnClick", closePicker)
end

local function buildMain()
    main = CreateFrame("Frame", "TYSLootFXSelectorFrame", UIParent)
    main:SetWidth(680); main:SetHeight(260)
    main:SetPoint("CENTER", UIParent, "CENTER", 0, 35)
    main:SetFrameStrata("DIALOG")
    main:SetBackdrop({bgFile="Interface\\Tooltips\\UI-Tooltip-Background", edgeFile="Interface\\Tooltips\\UI-Tooltip-Border", tile=true, tileSize=16, edgeSize=12, insets={left=3,right=3,top=3,bottom=3}})
    main:SetBackdropColor(0.004, 0.016, 0.028, 0.97)
    main:SetBackdropBorderColor(0.06, 0.62, 0.82, 1.0)
    main:EnableMouse(true); main:SetMovable(true); main:RegisterForDrag("LeftButton")
    main:SetScript("OnDragStart", function() this:StartMoving() end)
    main:SetScript("OnDragStop", function() this:StopMovingOrSizing() end)
    main:Hide()

    local top = makeSolid(main, "ARTWORK", 0.04, 0.80, 1.0, 0.95)
    top:SetPoint("TOPLEFT", main, "TOPLEFT", 4, -4); top:SetPoint("TOPRIGHT", main, "TOPRIGHT", -4, -4); top:SetHeight(3)
    local top2 = makeSolid(main, "ARTWORK", 0.02, 0.22, 0.34, 0.9)
    top2:SetPoint("TOPLEFT", main, "TOPLEFT", 4, -7); top2:SetPoint("TOPRIGHT", main, "TOPRIGHT", -4, -7); top2:SetHeight(18)

    local title = makeLabel(main, "太阳神殿 · 尸体模型选择器", 22, -16, "GameFontNormalLarge")
    title:SetTextColor(0.45, 0.95, 1.0)
    local apiTag = makeLabel(main, "原生模型分流  /  API32", 22, -42, "GameFontHighlightSmall")
    apiTag:SetTextColor(0.30, 0.64, 0.76)

    local corpse = makeSection(main, 22, -70, 636, 112, "尸体效果")
    corpseNameText = corpse:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    corpseNameText:SetPoint("TOPLEFT", corpse, "TOPLEFT", 14, -38)
    corpseNameText:SetTextColor(0.92, 0.96, 1.0)
    corpsePathText = corpse:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    corpsePathText:SetPoint("TOPLEFT", corpse, "TOPLEFT", 14, -60)
    corpsePathText:SetWidth(420); corpsePathText:SetJustifyH("LEFT"); corpsePathText:SetTextColor(0.40, 0.65, 0.72)

    local cChoose = makeTechButton(corpse, "选择模型", 96, 24)
    cChoose:SetPoint("TOPRIGHT", corpse, "TOPRIGHT", -14, -32)
    cChoose:SetScript("OnClick", openPicker)

    local cDefault = makeTechButton(corpse, "默认旗帜", 96, 22)
    cDefault:SetPoint("TOPRIGHT", corpse, "TOPRIGHT", -14, -62)
    cDefault:SetScript("OnClick", function() applyCorpse("default", DEFAULT_CORPSE, false) end)

    local cStock = makeTechButton(corpse, "原版光", 86, 22)
    cStock:SetPoint("RIGHT", cDefault, "LEFT", -6, 0)
    cStock:SetScript("OnClick", function() applyCorpse("stock", STOCK, false) end)

    local note = makeLabel(main, "说明：仅修改可拾取尸体模型；草药、矿石和其他采集交互始终保持客户端原版光效。", 24, -198, "GameFontHighlightSmall")
    note:SetTextColor(0.34, 0.62, 0.70)

    statusDot = makeSolid(main, "ARTWORK", 0.15, 0.95, 1.0, 1.0)
    statusDot:SetPoint("BOTTOMLEFT", main, "BOTTOMLEFT", 24, 24); statusDot:SetWidth(6); statusDot:SetHeight(6)
    statusText = main:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    statusText:SetPoint("LEFT", statusDot, "RIGHT", 8, 0); statusText:SetWidth(470); statusText:SetJustifyH("LEFT"); statusText:SetText("等待连接 DLL")

    local sig = main:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    sig:SetPoint("BOTTOMRIGHT", main, "BOTTOMRIGHT", -18, 18)
    sig:SetText("|cff55dfff太阳神殿|r")

    local close = makeTechButton(main, "关闭", 48, 20)
    close:SetPoint("TOPRIGHT", main, "TOPRIGHT", -11, -11)
    close:SetScript("OnClick", function() main:Hide(); closePicker() end)
end

-- 供斜杠命令和 FuBar 共用。
function TYSLootFXSelector_Toggle()
    if not main then return end
    if main:IsShown() then main:Hide(); closePicker()
    else main:Show(); applySaved() end
end

function TYSLootFXSelector_Show()
    if not main then return end
    main:Show(); applySaved()
end

local eventFrame = CreateFrame("Frame")
eventFrame:RegisterEvent("ADDON_LOADED")
eventFrame:RegisterEvent("PLAYER_ENTERING_WORLD")
eventFrame:SetScript("OnEvent", function()
    if event == "ADDON_LOADED" and arg1 == ADDON then
        TYSLootFXSelectorDB = TYSLootFXSelectorDB or {}
        db = TYSLootFXSelectorDB
        if not db.corpseMode then db.corpseMode = "default" end
        if not db.corpsePath then db.corpsePath = DEFAULT_CORPSE end
        -- 清除 V0.2 可能遗留的采集自定义状态。
        db.gatherMode = "stock"
        db.gatherPath = STOCK
        buildMain(); buildPicker(); refreshTexts()
    elseif event == "PLAYER_ENTERING_WORLD" and db then
        applySaved()
    end
end)

SLASH_TYSLOOTFX1 = "/lootfx"
SlashCmdList["TYSLOOTFX"] = function(msg)
    msg = string.lower(msg or "")
    if msg == "status" or msg == "状态" then
        local s, err = api("LootFX.Status")
        chat(s or err or "无状态")
    elseif msg == "reset" or msg == "重置" then
        if db then
            forceGatherStock(true)
            applyCorpse("default", DEFAULT_CORPSE, false)
        end
    else
        TYSLootFXSelector_Toggle()
    end
end
