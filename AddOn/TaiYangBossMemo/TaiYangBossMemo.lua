-- TaiYangBossMemo 0.5.0 | WoW 1.12 / Lua 5.0 | No external DLL dependencies.
-- Observational only: DEAD means unit death, not authoritative encounter victory.
local PREFIX="|cffffd100[太阳Boss备忘录]|r "
local db,shown,firstSeq = nil,{},nil
local tracks,lastEntry={},nil
local cursor=0
local timer=0
local function say(s) if DEFAULT_CHAT_FRAME then DEFAULT_CHAT_FRAME:AddMessage(PREFIX..s) end end
local function trim(s) return string.gsub(s or "^","^%s*(.-)%s*$","%1") end
local function getDB()
    if type(TaiYangBossMemoDB)~="table" then TaiYangBossMemoDB={} end
    db=TaiYangBossMemoDB
    if type(db.notes)~="table" then db.notes={} end
    if db.enabled==nil then db.enabled=true end
    if db.before==nil then db.before=true end
    if db.after==nil then db.after=true end
    return db
end
local function api(cmd,a,b)
    if type(TaiYangShenDian)~="function" then return nil end
    local ok,val
    if b~=nil then ok,val=pcall(TaiYangShenDian,cmd,a,b)
    elseif a~=nil then ok,val=pcall(TaiYangShenDian,cmd,a)
    else ok,val=pcall(TaiYangShenDian,cmd) end
    if ok and type(val)=="table" then return val end
end
local function note(entry)
    local d=getDB()
    return d.notes[tostring(entry)]
end

local frame=CreateFrame("Frame","TaiYangBossMemoWindow",UIParent)
frame:SetWidth(510);frame:SetHeight(465)
frame:SetPoint("CENTER",UIParent,"CENTER",0,35)
frame:SetFrameStrata("DIALOG")
frame:SetBackdrop({bgFile="Interface\\DialogFrame\\UI-DialogBox-Background",edgeFile="Interface\\DialogFrame\\UI-DialogBox-Border",tile=true,tileSize=32,edgeSize=32,insets={left=11,right=11,top=12,bottom=11}})
frame:SetMovable(true);frame:EnableMouse(true);frame:RegisterForDrag("LeftButton")
frame:SetScript("OnDragStart",function() this:StartMoving() end)
frame:SetScript("OnDragStop",function() this:StopMovingOrSizing() end)
local title=frame:CreateFontString(nil,"OVERLAY","GameFontNormalLarge")
title:SetPoint("TOP",frame,"TOP",0,-21);title:SetText("太阳神殿 · Boss 战斗备忘录")
local subtitle=frame:CreateFontString(nil,"OVERLAY","GameFontHighlightSmall")
subtitle:SetPoint("TOP",title,"BOTTOM",0,-8)
subtitle:SetText("以 NPC Entry 保存｜战前与死亡后独立备注｜每 0.5 秒查询")
local function textAt(y,text)
    local x=frame:CreateFontString(nil,"OVERLAY","GameFontNormal")
    x:SetPoint("TOPLEFT",frame,"TOPLEFT",28,y);x:SetText(text)
    return x
end
textAt(-73,"NPC Entry（数字）")
textAt(-122,"Boss 名称（备注用）")
textAt(-170,"战前提醒内容")
textAt(-263,"死亡后提醒内容")
local function edit(y,h)
    local box=CreateFrame("EditBox",nil,frame)
    box:SetWidth(448);box:SetHeight(h)
    box:SetPoint("TOPLEFT",frame,"TOPLEFT",29,y)
    box:SetFontObject(GameFontHighlight)
    box:SetMultiLine(h>35);box:SetAutoFocus(false)
    local backdrop=CreateFrame("Frame",nil,frame)
    backdrop:SetFrameLevel(box:GetFrameLevel()-1)
    backdrop:SetPoint("TOPLEFT",box,"TOPLEFT",-5,6)
    backdrop:SetPoint("BOTTOMRIGHT",box,"BOTTOMRIGHT",5,-5)
    backdrop:SetBackdrop({bgFile="Interface\\Tooltips\\UI-Tooltip-Background",edgeFile="Interface\\Tooltips\\UI-Tooltip-Border",tile=true,tileSize=16,edgeSize=13,insets={left=3,right=3,top=3,bottom=3}})
    backdrop:SetBackdropColor(0.07,0.07,0.09,0.95)
    box:SetScript("OnEscapePressed",function() this:ClearFocus() end)
    return box
end
local entryBox=edit(-92,22)
local nameBox=edit(-140,22)
local beforeBox=edit(-190,64)
local afterBox=edit(-283,64)
local status=frame:CreateFontString(nil,"OVERLAY","GameFontHighlightSmall")
status:SetPoint("BOTTOMLEFT",frame,"BOTTOMLEFT",27,60)
status:SetWidth(445);status:SetJustifyH("LEFT")
status:SetText("选中 Boss 后点击读取目标，或输入 Entry 并加载。")
local function setStatus(s) status:SetText(s) end
local function entryNumber()
    local str=trim(entryBox:GetText())
    if not string.find(str,"^%d+$") then return nil end
    local n=tonumber(str)
    if not n or n<1 or n>4294967295 then return nil end
    return n
end
local function loadEntry(n,label)
    if not n then setStatus("请输入有效的 NPC Entry 数字。");return end
    entryBox:SetText(tostring(n))
    local item=note(n)
    nameBox:SetText(item and item.name or label or "")
    beforeBox:SetText(item and item.before or "")
    afterBox:SetText(item and item.after or "")
    setStatus("已加载 Entry "..n.."。编辑完成后点击保存。")
    lastEntry=n
end
local function currentTarget()
    local id=api("Unit.Identity","target")
    if not id or not id.ok or not id.data or not id.data.entry then
        setStatus("当前目标不可用；也可以手动输入 Entry。");return
    end
    loadEntry(id.data.entry,id.data.name)
end
local function save()
    local n=entryNumber()
    if not n then setStatus("保存失败：Entry 必须为正整数。");return end
    local d=getDB()
    d.notes[tostring(n)]={name=trim(nameBox:GetText()),before=trim(beforeBox:GetText()),after=trim(afterBox:GetText())}
    lastEntry=n
    setStatus("已保存 Entry "..n.."。同一 Entry 的实例会共用备注。")
end
local function button(label,x,fn)
    local b=CreateFrame("Button",nil,frame,"UIPanelButtonTemplate")
    b:SetWidth(109);b:SetHeight(25);b:SetPoint("BOTTOMLEFT",frame,"BOTTOMLEFT",x,22)
    b:SetText(label);b:SetScript("OnClick",fn);return b
end
button("读取当前目标",25,currentTarget)
button("加载 Entry",140,function() loadEntry(entryNumber()) end)
button("保存备注",255,save)
button("关闭",370,function() frame:Hide() end)

local popup=CreateFrame("Frame","TaiYangBossMemoAlert",UIParent)
popup:SetWidth(440);popup:SetHeight(245)
popup:SetPoint("CENTER",UIParent,"CENTER",0,125)
popup:SetFrameStrata("FULLSCREEN_DIALOG")
popup:SetBackdrop({bgFile="Interface\\DialogFrame\\UI-DialogBox-Background",edgeFile="Interface\\DialogFrame\\UI-DialogBox-Border",tile=true,tileSize=32,edgeSize=32,insets={left=11,right=11,top=12,bottom=11}})
popup:SetMovable(true);popup:EnableMouse(true);popup:RegisterForDrag("LeftButton")
popup:SetScript("OnDragStart",function() this:StartMoving() end)
popup:SetScript("OnDragStop",function() this:StopMovingOrSizing() end)
local pTitle=popup:CreateFontString(nil,"OVERLAY","GameFontNormalLarge")
pTitle:SetPoint("TOP",popup,"TOP",0,-25)
local pBody=popup:CreateFontString(nil,"OVERLAY","GameFontHighlight")
pBody:SetPoint("TOPLEFT",popup,"TOPLEFT",30,-72);pBody:SetWidth(380);pBody:SetHeight(120)
pBody:SetJustifyH("LEFT");pBody:SetJustifyV("TOP")
local pClose=CreateFrame("Button",nil,popup,"UIPanelButtonTemplate")
pClose:SetWidth(110);pClose:SetHeight(25)
pClose:SetPoint("BOTTOM",popup,"BOTTOM",0,23);pClose:SetText("知道了")
pClose:SetScript("OnClick",function() popup:Hide() end)
popup:Hide()
local function alert(entry,phase)
    local d=getDB()
    if not d.enabled then return end
    if phase=="before" and not d.before then return end
    if phase=="after" and not d.after then return end
    local item=note(entry)
    if not item then return end
    local content=trim(item[phase])
    if content=="" then return end
    pTitle:SetText((phase=="before" and "战前提醒" or "检测到单位死亡").." · "..(item.name~="" and item.name or "Entry "..entry))
    pBody:SetText(content)
    popup:Show()
    say((phase=="before" and "战前：" or "死亡后：")..(item.name or tostring(entry)))
end

-- Poll while monitoring is enabled regardless of editor visibility.
-- M4 journal is driven by Boss.State calls, and only observed transitions are emitted.
local watched={}
local function observe()
    local d=getDB()
    if not d.enabled then return end
    local id=api("Unit.Identity","target")
    if id and id.ok and id.data and id.data.guid and id.data.entry then
        local entry=id.data.entry
        if note(entry) then
            local boss=api("Boss.State","target")
            -- Defensive: never infer kill from loss of target or missing GUID.
            if boss and boss.ok then watched[id.data.guid]=entry end
        end
    end
    local ev=api("Boss.Event.GetAfter",cursor)
    if not ev then return end
    if ev.lost then
        -- Missing observations should never be replayed as reliable event triggers.
        cursor=ev.latestSeq or cursor
        return
    end
    local items=ev.events or {}
    for i=1,table.getn(items) do
        local v=items[i]
        if v and type(v.seq)=="number" and v.seq>cursor then
            cursor=v.seq
            local entry=v.entry
            local item=entry and note(entry) or nil
            if item and v.guid then
                local key=v.guid
                if not shown[key] then shown[key]={} end
                if (v.state=="DETECTED" or v.state=="ENGAGED") and not shown[key].before then
                    shown[key].before=true
                    alert(entry,"before")
                elseif v.state=="DEAD" and not shown[key].after then
                    shown[key].after=true
                    alert(entry,"after")
                end
            end
        end
    end
    if ev.latestSeq and ev.latestSeq>cursor then cursor=ev.latestSeq end
end
local driver=CreateFrame("Frame")
driver:RegisterEvent("VARIABLES_LOADED")
driver:SetScript("OnEvent",function()
    if event=="VARIABLES_LOADED" then
        getDB()
        local st=api("Boss.Status")
        -- On login, never replay older historical events as new reminders.
        if st and type(st.latestSeq)=="number" then cursor=st.latestSeq end
    end
end)
driver:SetScript("OnUpdate",function()
    timer=timer+arg1
    if timer>=0.5 then timer=0;observe() end
end)
frame:Hide()
SLASH_TAIYANGBOSSMEMO1="/bossmemo"
SlashCmdList["TAIYANGBOSSMEMO"]=function(msg)
    local cmd=trim(msg)
    if cmd=="off" then getDB().enabled=false;say("自动提醒已关闭。");return end
    if cmd=="on" then getDB().enabled=true;say("自动提醒已开启。");return end
    if cmd=="target" then frame:Show();currentTarget();return end
    frame:Show()
    if lastEntry then loadEntry(lastEntry) else currentTarget() end
end
