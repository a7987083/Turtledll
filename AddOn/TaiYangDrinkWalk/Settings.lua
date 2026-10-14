-- TaiYangDrinkWalk item config UI - WoW 1.12.1 Lua 5.0.
local DW=TaiYangDrinkWalk
local function Label(f,text,x,y)
    local z=f:CreateFontString(nil,"OVERLAY","GameFontNormal")
    z:SetPoint("TOPLEFT",f,"TOPLEFT",x,y);z:SetText(text)
    return z
end
function DW.RefreshSettings()
    if DW.statusText then DW.statusText:SetText(DW.DLLStatus()) end
    if DW.modeText then
        local mode="WATCH"
        if type(TaiYangDrinkWalkDB)=="table" then mode=TaiYangDrinkWalkDB.mode or "WATCH" end
        DW.modeText:SetText("模式: "..mode.." （仅保存设置）")
    end
    if DW.specEdit then
        local focused = false
        if type(GetCurrentKeyBoardFocus) == "function" then
            focused = (GetCurrentKeyBoardFocus() == DW.specEdit)
        end
        if not focused then DW.specEdit:SetText(DW.Items.GetSpec()) end
    end
    if DW.itemText then DW.itemText:SetText(DW.Items.status or "尚未扫描") end
end
function DW.BuildSettings()
    if DW.settingsFrame or not UIParent then return end
    local f=CreateFrame("Frame","TaiYangDrinkWalkSettings",UIParent)
    f:SetWidth(470);f:SetHeight(295)
    f:SetPoint("CENTER",UIParent,"CENTER",0,0)
    f:SetFrameStrata("DIALOG")
    f:SetBackdrop({bgFile="Interface\\DialogFrame\\UI-DialogBox-Background",edgeFile="Interface\\DialogFrame\\UI-DialogBox-Border",tile=true,tileSize=32,edgeSize=32,insets={left=8,right=8,top=8,bottom=8}})
    f:SetMovable(true);f:EnableMouse(true);f:RegisterForDrag("LeftButton")
    f:SetScript("OnDragStart",function() f:StartMoving() end)
    f:SetScript("OnDragStop",function() f:StopMovingOrSizing() end)
    Label(f,"TaiYangDrinkWalk - 物品管理 0.2.1",20,-20)
    local close=CreateFrame("Button",nil,f,"UIPanelCloseButton")
    close:SetPoint("TOPRIGHT",f,"TOPRIGHT",-4,-4)
    DW.statusText=Label(f,"DLL",20,-58)
    DW.statusText:SetWidth(420);DW.statusText:SetJustifyH("LEFT")
    DW.modeText=Label(f,"模式: WATCH",20,-88)
    Label(f,"允许使用的物品 ID 或完整名称（逗号分隔）",20,-117)
    local spec=CreateFrame("EditBox","TaiYangDrinkWalkItemSpec",f,"InputBoxTemplate")
    spec:SetAutoFocus(false);spec:SetWidth(402);spec:SetHeight(22)
    spec:SetMaxLetters(1000);spec:SetPoint("TOPLEFT",f,"TOPLEFT",22,-139)
    spec:SetScript("OnEscapePressed",function() spec:ClearFocus() end)
    spec:SetScript("OnEnterPressed",function()
        local ok,msg=DW.Items.SetSpec(spec:GetText())
        DW.Print(msg);spec:ClearFocus()
    end)
    DW.specEdit=spec
    local function Button(text,x,y,w,fn)
        local b=CreateFrame("Button",nil,f,"UIPanelButtonTemplate")
        b:SetWidth(w);b:SetHeight(24);b:SetPoint("TOPLEFT",f,"TOPLEFT",x,y)
        b:SetText(text);b:SetScript("OnClick",fn);return b
    end
    Button("保存物品",20,-180,110,function()
        local ok,msg=DW.Items.SetSpec(spec:GetText())
        DW.Print(msg);DW.RefreshSettings()
    end)
    Button("扫描并选择",142,-180,112,function()
        local ok,msg=DW.Items.SetSpec(spec:GetText())
        if ok then DW.Items.Report() end
        spec:ClearFocus();DW.RefreshSettings()
    end)
    Button("状态检测",270,-180,100,function() DW.ShowStatus();DW.RefreshSettings() end)
    Button("切换模式",20,-227,110,function()
        if type(TaiYangDrinkWalkDB)~="table" then TaiYangDrinkWalkDB={} end
        if TaiYangDrinkWalkDB.mode=="WATCH" then TaiYangDrinkWalkDB.mode="SPAM"
        else TaiYangDrinkWalkDB.mode="WATCH" end
        DW.RefreshSettings()
    end)
    DW.itemText=Label(f,"尚未扫描",20,-262)
    DW.itemText:SetWidth(430);DW.itemText:SetJustifyH("LEFT")
    f:Hide();DW.settingsFrame=f;DW.RefreshSettings()
end
DW.RegisterModule("Settings")
