-- Phase 2: settings + item rules UI. Lua 5.0-compatible.
local DW = TaiYangDrinkWalk
local I
local function Label(f,text,x,y)
    local z=f:CreateFontString(nil,"OVERLAY","GameFontNormal")
    z:SetPoint("TOPLEFT",f,"TOPLEFT",x,y)
    z:SetText(text)
    return z
end
function DW.RefreshSettings()
    if DW.statusText then DW.statusText:SetText(DW.DLLStatus()) end
    if DW.modeText then
        local mode = "WATCH"
        if type(TaiYangDrinkWalkDB)=="table" then mode=TaiYangDrinkWalkDB.mode or "WATCH" end
        DW.modeText:SetText("模式: "..mode.." （未启用动作）")
    end
    if DW.rulesText then
        local n=0
        if type(TaiYangDrinkWalkDB)=="table" and type(TaiYangDrinkWalkDB.itemRules)=="table" then
            n=table.getn(TaiYangDrinkWalkDB.itemRules)
        end
        DW.rulesText:SetText("已配置物品规则: "..n.." 项")
    end
end
function DW.BuildSettings()
    if DW.settingsFrame or not UIParent then return end
    local f=CreateFrame("Frame","TaiYangDrinkWalkSettings",UIParent)
    f:SetWidth(430);f:SetHeight(310)
    f:SetPoint("CENTER",UIParent,"CENTER",0,0)
    f:SetFrameStrata("DIALOG")
    f:SetBackdrop({bgFile="Interface\\DialogFrame\\UI-DialogBox-Background",edgeFile="Interface\\DialogFrame\\UI-DialogBox-Border",tile=true,tileSize=32,edgeSize=32,insets={left=8,right=8,top=8,bottom=8}})
    f:SetMovable(true);f:EnableMouse(true)
    f:RegisterForDrag("LeftButton")
    f:SetScript("OnDragStart",function() f:StartMoving() end)
    f:SetScript("OnDragStop",function() f:StopMovingOrSizing() end)
    local title=Label(f,"TaiYangDrinkWalk - 物品管理",20,-20)
    local close=CreateFrame("Button",nil,f,"UIPanelCloseButton")
    close:SetPoint("TOPRIGHT",f,"TOPRIGHT",-4,-4)
    DW.statusText=Label(f,"DLL状态",20,-58)
    DW.statusText:SetWidth(380);DW.statusText:SetJustifyH("LEFT")
    DW.modeText=Label(f,"模式: WATCH",20,-87)
    DW.rulesText=Label(f,"物品规则: 0",20,-113)
    Label(f,"类别 (food / drink / both)",20,-150)
    local kind=CreateFrame("EditBox","TaiYangDrinkWalkKindInput",f,"InputBoxTemplate")
    kind:SetWidth(85);kind:SetHeight(22)
    kind:SetPoint("TOPLEFT",f,"TOPLEFT",22,-171)
    kind:SetAutoFocus(false);kind:SetText("drink")
    Label(f,"物品ID或完整名称",135,-150)
    local query=CreateFrame("EditBox","TaiYangDrinkWalkItemInput",f,"InputBoxTemplate")
    query:SetWidth(220);query:SetHeight(22)
    query:SetPoint("TOPLEFT",f,"TOPLEFT",136,-171)
    query:SetAutoFocus(false);query:SetMaxLetters(100)
    local function Button(text,x,y,w,fn)
        local b=CreateFrame("Button",nil,f,"UIPanelButtonTemplate")
        b:SetWidth(w);b:SetHeight(24)
        b:SetPoint("TOPLEFT",f,"TOPLEFT",x,y)
        b:SetText(text);b:SetScript("OnClick",fn)
        return b
    end
    Button("添加规则",20,-210,90,function()
        local ok,msg=DW.Items.Add(kind:GetText(),query:GetText())
        DW.Print(msg)
        DW.RefreshSettings()
    end)
    Button("列出规则",119,-210,90,function() DW.Items.List() end)
    Button("扫描背包",218,-210,90,function() DW.Items.Report() end)
    Button("状态检测",317,-210,90,function() DW.ShowStatus() end)
    Button("切换模式",20,-254,95,function()
        if type(TaiYangDrinkWalkDB)~="table" then TaiYangDrinkWalkDB={} end
        if TaiYangDrinkWalkDB.mode=="WATCH" then TaiYangDrinkWalkDB.mode="SPAM" else TaiYangDrinkWalkDB.mode="WATCH" end
        DW.RefreshSettings()
    end)
    local hint=Label(f,"删除规则: /tsdw remove 序号。当前只扫描，不使用物品。",124,-262)
    hint:SetWidth(288)
    f:Hide()
    DW.settingsFrame=f
    DW.RefreshSettings()
end
DW.RegisterModule("Settings")
