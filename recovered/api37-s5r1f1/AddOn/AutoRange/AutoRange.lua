-- AutoRange B3.8R7.5.1c Warning Stage2.31 - ARX1/API33 independent runtime
-- Turtle WoW 1.12 / TaiYangShenDian API33 only (no UnitXP/SuperWoW runtime dependency)
-- Totems: DBC classifies TYS cast events -> short native discovery binds the new
-- totem GUID -> TotemSnapshot stops -> exact GUID checks own lifetime with 2-miss debounce.
-- Automatically resolves radius from the client's active DBCs. One hazard owns
-- exactly one M2 instance: VisualSet once, VisualMove thereafter, VisualClear on end.
-- Enemy/friendly-player filtering and totem owner filtering are DLL-backed.



AutoRangeDB = AutoRangeDB or {}

-- PLR1: Native features are process-lifetime available; no addon bootstrap is required.

local F = CreateFrame("Frame", "AutoRangeEventFrame")
F:RegisterEvent("VARIABLES_LOADED")
F:RegisterEvent("PLAYER_ENTERING_WORLD")
F:RegisterEvent("PLAYER_LEAVING_WORLD")
F:RegisterEvent("PLAYER_LOGOUT")

local running = true
local visualEnabled = true
local enemyCircle = true
local friendCircle = false
local enemyLimitEnabled = false
local enemyLimitMax = 20
local enemyLimitCount = 0
local enemyLimitHead = nil
local enemyLimitTail = nil
-- R7.3: keep all new state/functions/UI under one table so the Lua 5.0 main chunk
-- stays below its local-variable ceiling.
local SF={enemyMode="ALL",enemyRules={},enemyCount=0,enemyActiveCount=0,
  selfEnabled=false,selfRules={},selfCount=0,selfActiveCount=0,
  enemyPage=1,selfPage=1,pageSize=8,enemyPageSize=4,selfPageSize=8,enemyIds=nil,selfIds=nil,selectedEnemyRule=nil,selectedSelfRule=nil,
  enemyEditStyle="STANDARD",selfEditStyle="WHITE",enemyEditDecision="AUTO",enemyEditMode="NORMAL",
  playerGuid=nil,playerGuidTried=false,refreshInterval=0.20,semanticFilterEnabled=true,
  ui={},durationCache={},auraCandidateCache={},auraNameCache={},auraNames={},auraTextures={},spellBookTextureCache={},semanticCache={}}
local totemMode = "SELF"
local totemLastMode = "SELF"
local scanRange = 140
local elapsed = 0
local totemLifeElapsed = 0
local totemDiscoverElapsed = 0
local pendingTotems = {}
local totemDbcCache = {}
local totemBootstrapAt = nil
local totemBootstrapRetries = 0
local OnUpdateHandler = nil
local ApplyRuntimeHooks = nil
local runtimeEventAttached = false
local runtimeAuraAttached = false
local runtimeUpdateAttached = false
local visualMaxAge = 27.0 -- restart the SAME S30 model sequence; never Clear+Set for rollover
local castSerial = 0
local pendingCastByToken = {}
local hazards = {}
local order = {}
local orderIndex = {}
local dynamicMeta = {}
local totemRangeCache = {}
-- R7.0: ordinary hostile/teammate spell geometry is immutable for the client session.
-- Cache successful native resolver results. A native NO_RADIUS result is deterministic for
-- this client session too, so cache that miss while still allowing runtime DynamicObject discovery.
local normalResolveCache = {}
-- CASTER position/lifecycle state is tracked once per caster GUID, never once per circle.
-- Enemy GUID state uses API33 UnitStateByGuid; teammate GUIDs share the same read path.
local casterRuntime = {}
local casterHazardCounts = {}
local casterReuseByToken = {}
-- R7.0 runtime scheduler: only hazards that currently need periodic work live in
-- these compact buckets. Hidden/disabled/pending-only hazards remain in hazards/order
-- for lifecycle compatibility but cost zero OnUpdate traversal.
local activeCasterKeys, activeCasterIndex = {}, {}
local activeDiscoveryKeys, activeDiscoveryIndex = {}, {}
local activeDynamicKeys, activeDynamicIndex = {}, {}
local activeTotemKeys, activeTotemIndex = {}, {}
-- Reused hot-path scratch tables. Wiped in place instead of allocating every configured refresh tick.
local pollDynamics, pollDynamicSeen = {}, {}
local pollClaimed, pollRemove, pollReclass, pollCasterCache = {}, {}, {}, {}
-- R6.3: zero-idle, one-shot DBC caches for the Reliquary-derived spell chain.
-- They are touched only when a new totem needs its range resolved.
local chainSpellCache = {}
local chainRadiusCache = {}
local TotemGuidAlive = nil
local PurgeStaleHiddenHazards = nil
local RestoreActiveSelfAuraHazards = nil
local EnforceEnemyLimit = nil
local RemoveHazard = nil

local Render = nil

local visualPool = {
  -- Keep the original five-model pool unchanged for non-totem hazards/backward compatibility.
  -- Totem AUTO sizing uses the measured-W candidate list below; manual catalog selection still overrides it.
  {id="STANDARD",label="标准",path="Spells\\DangerZone_W10_S30.m2",diameter=10},
  {id="WHITE",label="白色",path="Spells\\DangerZone_W10_S30_White.m2",diameter=10},
  {id="FLAME",label="火焰",path="Spells\\DangerZone_W10_S30_Flame.m2",diameter=10},
  {id="JAGGED",label="锯齿",path="Spells\\DangerZone_W12_S30_Jagged.m2",diameter=12},
  {id="JAGGEDG",label="锯齿绿",path="Spells\\DangerZone_W12_S30_JaggedG.m2",diameter=12},
}

-- Production totem AUTO sizing pool. In-game measurements confirmed that W is
-- the Scale=1 model diameter in yards. Keep only neutral S30 models required
-- by the automatic sizing algorithm; the 52-model browser lives in the future
-- developer diagnostic addon, not in AutoRange production.
local totemAutoSizePool={
  {id="DZ_W8_S30", label="W8_S30", path="Spells\\DangerZone_W8_S30.m2", diameter=8},
  {id="DZ_W10_S30",label="W10_S30",path="Spells\\DangerZone_W10_S30.m2",diameter=10},
  {id="DZ_W12_S30",label="W12_S30",path="Spells\\DangerZone_W12_S30.m2",diameter=12},
  {id="DZ_W15_S30",label="W15_S30",path="Spells\\DangerZone_W15_S30.m2",diameter=15},
  {id="DZ_W17_S30",label="W17_S30",path="Spells\\DangerZone_W17_S30.m2",diameter=17},
  {id="DZ_W18_S30",label="W18_S30",path="Spells\\DangerZone_W18_S30.m2",diameter=18},
  {id="DZ_W20_S30",label="W20_S30",path="Spells\\DangerZone_W20_S30.m2",diameter=20},
  {id="DZ_W22_S30",label="W22_S30",path="Spells\\DangerZone_W22_S30.m2",diameter=22},
  {id="DZ_W23_S30",label="W23_S30",path="Spells\\DangerZone_W23_S30.m2",diameter=23},
  {id="DZ_W25_S30",label="W25_S30",path="Spells\\DangerZone_W25_S30.m2",diameter=25},
  {id="DZ_W30_S30",label="W30_S30",path="Spells\\DangerZone_W30_S30.m2",diameter=30},
  {id="DZ_W35_S30",label="W35_S30",path="Spells\\DangerZone_W35_S30.m2",diameter=35},
  {id="DZ_W40_S30",label="W40_S30",path="Spells\\DangerZone_W40_S30.m2",diameter=40},
  {id="DZ_W50_S30",label="W50_S30",path="Spells\\DangerZone_W50_S30.m2",diameter=50},
}
local totemVisualStyle = "AUTO"
-- Totem appearance state lives under SF to keep the Lua 5.0 main chunk below
-- its hard local-variable ceiling. Shared mode defaults ON for old-save behavior.
SF.totemUnifiedStyle=true
SF.totemCategoryStyles={FIRE="FLAME",EARTH="JAGGED",WATER="WHITE",AIR="STANDARD",OTHER="STANDARD"}
SF.totemElementText={FIRE="火焰",EARTH="大地",WATER="水",AIR="空气",OTHER="其他"}
-- Stage2.29: user-defined totem rules are the highest-priority totem override.
-- Keyed by normalized localized totem name. Each rule can override radius + M2 style.
SF.totemCustomRules={}
SF.totemCustomIds={}
SF.totemCustomEditStyle="AUTO"
SF.totemCustomPage=1
SF.totemCustomPageSize=5

function SF.TotemElementFromEffect(effectType)
  effectType=tonumber(effectType) or 0
  if effectType==87 then return "FIRE" end
  if effectType==88 then return "EARTH" end
  if effectType==89 then return "WATER" end
  if effectType==90 then return "AIR" end
  return "OTHER"
end

-- R7.4 custom M2 library. MPQ is metadata only: the client resolves the M2
-- through its already-mounted virtual filesystem. Runtime rendering always passes
-- the virtual M2 path to AutoRange.VisualSet/VisualMove.
local M2={customById={},customIds={},styleOrder={},nextId=1,testKey="__AutoRangeM2Test",ui={},scanRecords={},bySource={},sources={},sourcePage=1,sourcePageSize=8,modelPage=1,modelPageSize=8,selectedSource=nil,selectedRecord=nil,searchMode=false,searchResults={},recent={},recentMax=20,scanPhase="IDLE",scanResultCount=0,scanGetIndex=1,scanArchiveCount=0,scanArchiveGetIndex=1,scanElapsed=0,visibleSourceRows={},visibleModelRows={}}

-- Warning overlay: one lightweight screen texture shared by all four enemy rule modes.
-- NORMAL never calls LOS. VISION_RANGE calls LOS only after geometry hit.
-- VISION_GLOBAL calls LOS after release without geometry. GLOBAL_HIT never calls LOS.
local Warn={enabled=true,mode="SINGLE",path="",directionPath="",directionSlot=nil,directionMedia={LEFT="",RIGHT=""},directionPos={LEFT={x=-220,y=120},RIGHT={x=220,y=120}},directionFrames={},directionTextures={},directionLabels={},mediaDir="Interface\\AddOns\\AutoRange\\Media\\",mediaFiles={},mediaIndex=1,mediaSource="INDEX",
  x=0,y=120,width=384,height=192,scale=1.0,interval=0.01,elapsed=0,danger=false,test=false,dirty=true,frame=nil,texture=nil,ui={},
  -- Fast geometry hit-test is capped at ~100Hz and therefore follows every frame at 60 FPS.
  losCache={},losCacheTTL=0.05,losReadyAt=0}

local function TrimText(v)
  local s=tostring(v or "")
  s=string.gsub(s,"^%s+","")
  s=string.gsub(s,"%s+$","")
  return s
end

-- Stage2.29b: forward declarations for helpers used by the early custom-totem
-- functions. These helpers are implemented later in the file; declaring them here
-- makes the early functions capture the intended locals instead of looking up nil globals.
local TotemVisualStyleLabel
local ResolveSearingTotemRange
local ResolveTotemRange
local ClearVisual
local SetVisual
local Chat

function SF.NormalizeTotemCustomName(v)
  local s=TrimText(v)
  s=string.lower(s)
  s=string.gsub(s,"%s+"," ")
  return s
end

function SF.RebuildTotemCustomIds()
  local ids={}
  local key,rule
  for key,rule in pairs(SF.totemCustomRules or {}) do
    if type(rule)=="table" and rule.name and tonumber(rule.radius) and tonumber(rule.radius)>0 then
      table.insert(ids,key)
    end
  end
  table.sort(ids,function(a,b)
    local ra=SF.totemCustomRules[a]; local rb=SF.totemCustomRules[b]
    return string.lower(tostring(ra and ra.name or a))<string.lower(tostring(rb and rb.name or b))
  end)
  SF.totemCustomIds=ids
  return ids
end

function SF.TotemCustomRuleForSpell(spell)
  local name=SF.GetSpellName(spell)
  if name=="" then return nil,nil end
  local key=SF.NormalizeTotemCustomName(name)
  local rule=SF.totemCustomRules and SF.totemCustomRules[key]
  if type(rule)=="table" then return rule,name end
  return nil,name
end

function SF.SaveTotemCustomRules()
  AutoRangeDB.totemCustomRules=SF.totemCustomRules
end

function SF.LoadTotemCustomRules(saved)
  SF.totemCustomRules={}
  local customKey,customRule,customName,customRadius
  for customKey,customRule in pairs(saved or {}) do
    if type(customRule)=="table" then
      customName=TrimText(customRule.name or customKey)
      customRadius=tonumber(customRule.radius)
      if customName~="" and customRadius and customRadius>0 then
        if customRadius>999 then customRadius=999 end
        SF.totemCustomRules[SF.NormalizeTotemCustomName(customName)]={name=customName,radius=customRadius,style=SF.ValidTotemVisualStyle(customRule.style,"AUTO")}
      end
    end
  end
  SF.RebuildTotemCustomIds()
  AutoRangeDB.totemCustomRules=SF.totemCustomRules
end

function SF.RefreshCustomTotemVisuals()
  local i,h,rule,baseRadius,baseSpell,spell
  for i=1,table.getn(order) do
    h=hazards[order[i]]
    if h and h.totemOnly then
      spell=h.createdSpell or h.originalTotemSpell or h.spell
      rule=SF.TotemCustomRuleForSpell(spell)
      if rule then
        h.radius=tonumber(rule.radius) or h.radius
        h.totemCustomStyle=SF.ValidTotemVisualStyle(rule.style,"AUTO")
        h.totemCustomName=rule.name
      else
        -- Restore the normal Stage2.28 result when a custom rule is removed.
        baseRadius,baseSpell=ResolveSearingTotemRange(spell,h.ownerScope)
        if not baseRadius then baseRadius,baseSpell=ResolveTotemRange(spell,h.totemAuraCsv or "") end
        if baseRadius and baseRadius>0 then h.radius=baseRadius; h.spell=baseSpell or spell; h.geomSpell=baseSpell or spell end
        h.totemCustomStyle=nil; h.totemCustomName=nil
      end
      ClearVisual(h); SetVisual(h)
    end
  end
end

function SF.UpsertTotemCustomRule(name,radius,style,announce)
  name=TrimText(name)
  radius=tonumber(radius)
  if name=="" then if announce then Chat("自定义图腾：请输入图腾名字") end return false end
  if not radius or radius<=0 then if announce then Chat("自定义图腾：码数必须大于0") end return false end
  if radius>999 then radius=999 end
  style=SF.ValidTotemVisualStyle(style or SF.totemCustomEditStyle,"AUTO")
  local key=SF.NormalizeTotemCustomName(name)
  SF.totemCustomRules[key]={name=name,radius=radius,style=style}
  SF.SaveTotemCustomRules(); SF.RebuildTotemCustomIds(); SF.RefreshCustomTotemVisuals()
  if announce then Chat("自定义图腾已保存："..name.."  "..tostring(radius).."码  "..TotemVisualStyleLabel(style)) end
  return true
end

function SF.DeleteTotemCustomRule(key,announce)
  key=SF.NormalizeTotemCustomName(key)
  local rule=SF.totemCustomRules[key]
  if not rule then return false end
  SF.totemCustomRules[key]=nil
  SF.SaveTotemCustomRules(); SF.RebuildTotemCustomIds(); SF.RefreshCustomTotemVisuals()
  if announce then Chat("已删除自定义图腾："..tostring(rule.name or key)) end
  return true
end


function M2.NormalizePath(v)
  local s=TrimText(v)
  s=string.gsub(s,"/","\\")
  s=string.gsub(s,"^\\+","")
  return s
end

function M2.NormalizeMpq(v)
  return TrimText(v)
end

function M2.RebuildStyleOrder()
  local t={"STANDARD","WHITE","FLAME","JAGGED","JAGGEDG"}
  local i
  for i=1,table.getn(M2.customIds) do t[table.getn(t)+1]=M2.customIds[i] end
  M2.styleOrder=t
  return t
end

function M2.LoadSaved()
  M2.customById={}; M2.customIds={}
  local src=AutoRangeDB.customM2Library
  if type(src)~="table" then src={} end
  local maxId=0
  local id,v,n
  for id,v in pairs(src) do
    local sid=string.upper(tostring(id or ""))
    local _,_,num=string.find(sid,"^CUSTOM_([0-9]+)$")
    n=tonumber(num)
    if n and n>0 and type(v)=="table" then
      local path=M2.NormalizePath(v.path)
      local diameter=tonumber(v.diameter) or tonumber(v.width) or 0
      local label=TrimText(v.label or v.name)
      if path~="" and diameter>0 then
        if label=="" then label="自定义M2 "..tostring(n) end
        local rec={id=sid,label=label,path=path,diameter=diameter,mpq=M2.NormalizeMpq(v.mpq),custom=true}
        M2.customById[sid]=rec
        table.insert(M2.customIds,sid)
        if n>maxId then maxId=n end
      end
    end
  end
  table.sort(M2.customIds,function(a,b)
    local _,_,aa=string.find(a,"([0-9]+)$"); local _,_,bb=string.find(b,"([0-9]+)$")
    return (tonumber(aa) or 0)<(tonumber(bb) or 0)
  end)
  M2.nextId=math.max(maxId+1,math.floor(tonumber(AutoRangeDB.customM2NextId) or 1))
  AutoRangeDB.customM2NextId=M2.nextId
  AutoRangeDB.customM2Library=src
  M2.RebuildStyleOrder()
end

Chat=function(s)
  if DEFAULT_CHAT_FRAME then DEFAULT_CHAT_FRAME:AddMessage("|cff66ccff[AutoRange]|r " .. tostring(s)) end
end

local function Split(line)
  local t, p = {}, 1
  line = tostring(line or "")
  while true do
    local q = string.find(line, "|", p, true)
    if not q then table.insert(t, string.sub(line, p)); break end
    table.insert(t, string.sub(line, p, q - 1)); p = q + 1
  end
  return t
end

local function Lines(text, fn)
  local p = 1
  text = tostring(text or "")
  while p <= string.len(text) do
    local q = string.find(text, "\n", p, true)
    if q then fn(string.sub(text, p, q - 1)); p = q + 1
    else fn(string.sub(text, p)); break end
  end
end

local function NormGuid(v)
  local s = string.upper(tostring(v or ""))
  s = string.gsub(s, "^0X", "")
  s = string.gsub(s, "^0+", "")
  if s == "" then s = "0" end
  return s
end

function SF.RefreshPlayerGuid()
  SF.playerGuidTried=true
  SF.playerGuid=nil
  if type(TaiYangShenDian)~="function" then return nil end
  local ok,guid=pcall(TaiYangShenDian,"Unit.Guid","player")
  if ok and guid then
    local n=NormGuid(guid)
    if n and n~="0" then SF.playerGuid=n end
  end
  return SF.playerGuid
end

function SF.IsPlayerCaster(guid)
  if not SF.playerGuidTried then SF.RefreshPlayerGuid() end
  if not SF.playerGuid then return false end
  return NormGuid(guid)==SF.playerGuid
end

function SF.ValidStyle(style,defaultStyle)
  style=string.upper(tostring(style or defaultStyle or "STANDARD"))
  if style=="STANDARD" or style=="WHITE" or style=="FLAME" or style=="JAGGED" or style=="JAGGEDG" then return style end
  if M2 and M2.customById and M2.customById[style] then return style end
  return string.upper(tostring(defaultStyle or "STANDARD"))
end

function SF.ValidDecision(v)
  v=string.upper(tostring(v or "AUTO"))
  if v=="SHOW" or v=="HIDE" then return v end
  return "AUTO"
end

function SF.DecisionLabel(v)
  v=SF.ValidDecision(v)
  if v=="SHOW" then return "强显" end
  if v=="HIDE" then return "强隐" end
  return "自动"
end

function SF.ValidRuleMode(v)
  v=string.upper(tostring(v or "NORMAL"))
  -- One-time compatibility with the removed LOS V1 rule values. New storage uses VISION_* only.
  if v=="LOS_RANGE" then v="VISION_RANGE" elseif v=="LOS_GLOBAL" then v="VISION_GLOBAL" end
  if v=="VISION_RANGE" or v=="VISION_GLOBAL" or v=="GLOBAL_HIT" then return v end
  return "NORMAL"
end

function SF.RuleModeLabel(v)
  v=SF.ValidRuleMode(v)
  if v=="VISION_RANGE" then return "视野范围" end
  if v=="VISION_GLOBAL" then return "视野全局" end
  if v=="GLOBAL_HIT" then return "全局必中" end
  return "普通"
end

function SF.SemanticSpellClass(spell)
  spell=tonumber(spell) or 0
  if spell<=0 then return "UNKNOWN" end
  local cached=SF.semanticCache[spell]
  if cached then return cached end
  if type(TaiYangShenDian)~="function" then SF.semanticCache[spell]="UNKNOWN"; return "UNKNOWN" end

  local ok,row=pcall(TaiYangShenDian,"spell",spell)
  if not ok or type(row)~="table" then SF.semanticCache[spell]="UNKNOWN"; return "UNKNOWN" end

  local dangerEffect={ [2]=true,[7]=true,[9]=true,[17]=true,[31]=true,[58]=true,[121]=true }
  local dangerAura={ [3]=true,[5]=true,[7]=true,[12]=true,[14]=true,[15]=true,[26]=true,[27]=true,[33]=true,[43]=true }
  local pureHelpful=true
  local hasEffect=false
  local hasAura=false
  local friendlyArea=false
  local i
  for i=1,3 do
    local eff=tonumber(row["effect"..tostring(i)]) or 0
    local aura=tonumber(row["applyAura"..tostring(i)]) or 0
    if eff>0 then
      hasEffect=true
      if dangerEffect[eff] or dangerAura[aura] then
        SF.semanticCache[spell]="DANGER"
        return "DANGER"
      end
      if aura>0 or eff==6 or eff==35 or eff==65 then hasAura=true end
      if eff==35 or eff==65 then friendlyArea=true end
      if eff~=10 and eff~=30 then pureHelpful=false end
    end
  end

  local raw=string.lower(table.concat({
    tostring(row.name or ""),tostring(row.nameZhCN or ""),
    tostring(row.descriptionEnUS or ""),tostring(row.descriptionZhCN or ""),
    tostring(row.auraDescriptionEnUS or ""),tostring(row.auraDescriptionZhCN or "")
  }," "))

  local harmfulText=false
  local harmfulHints={
    "damage taken","takes damage","deals damage","inflicts damage",
    "受到的伤害","受到伤害","每秒受到","昏迷","恐惧","沉默","定身","减速","降低移动速度"
  }
  for i=1,table.getn(harmfulHints) do
    if string.find(raw,harmfulHints[i],1,true) then harmfulText=true; break end
  end
  if harmfulText then SF.semanticCache[spell]="DANGER"; return "DANGER" end

  if hasEffect and pureHelpful then SF.semanticCache[spell]="SAFE"; return "SAFE" end
  if friendlyArea and hasAura then SF.semanticCache[spell]="SAFE"; return "SAFE" end

  if hasAura then
    local safeHints={
      "increases damage","increase damage","damage done by","attack power","attack speed",
      "increases armor","armor by","increases resistance","resistance by",
      "restores health","restores mana","heals nearby","heals allies","heals party","heals raid",
      "critical strike chance","increases critical","increases chance to hit","absorbs damage",
      "提高伤害","伤害提高","增加伤害","伤害增加","攻击强度","攻击速度","施法速度",
      "提高护甲","增加护甲","护甲值","提高抗性","增加抗性","恢复生命","恢复法力",
      "治疗附近","治疗友方","治疗盟友","提高暴击","增加暴击","提高命中","增加命中","吸收伤害"
    }
    for i=1,table.getn(safeHints) do
      if string.find(raw,safeHints[i],1,true) then SF.semanticCache[spell]="SAFE"; return "SAFE" end
    end
  end

  SF.semanticCache[spell]="UNKNOWN"
  return "UNKNOWN"
end

function SF.AutoSemanticAllowed(spell)
  if not SF.semanticFilterEnabled then return true end
  return SF.SemanticSpellClass(spell)~="SAFE"
end

function SF.EnemyAllowed(spell)
  spell=tonumber(spell) or 0
  local rule=SF.enemyRules[spell]
  local decision=rule and SF.ValidDecision(rule.decision) or "AUTO"
  if decision=="SHOW" then return true end
  if decision=="HIDE" then return false end
  if SF.enemyMode=="ONLY" then return rule and true or false end
  if SF.enemyMode=="EXCLUDE" and rule then return false end
  return SF.AutoSemanticAllowed(spell)
end

function SF.RuleForSource(sourceType,spell)
  spell=tonumber(spell) or 0
  if sourceType=="SELF" then return SF.selfRules[spell] end
  if sourceType=="ENEMY" or sourceType=="UNKNOWN" then return SF.enemyRules[spell] end
  return nil
end

function SF.NormalizeRules(src,defaultStyle)
  local out,count={},0
  if type(src)~="table" then return out,0,0 end
  local k,v,id,rule
  for k,v in pairs(src) do
    id=tonumber(k)
    if not id and (type(v)=="number" or type(v)=="string") then id=tonumber(v) end
    if id and id>0 then
      id=math.floor(id)
      if type(v)=="table" then
        rule={radius=math.max(0,tonumber(v.radius) or 0),
          style=SF.ValidStyle(v.style,defaultStyle),name=tostring(v.name or ""),
          decision=SF.ValidDecision(v.decision),mode=SF.ValidRuleMode(v.mode or v.mechanic)}
      else
        rule={radius=0,style=SF.ValidStyle(nil,defaultStyle),name="",decision="AUTO",mode="NORMAL"}
      end
      if not out[id] then out[id]=rule; count=count+1 end
    end
  end
  return out,count,count
end

function SF.ForEachInput(text,fn)
  text=tostring(text or "")
  if type(fn)~="function" then return 0 end
  local n=0
  local token
  for token in string.gfind(text,"%d+") do
    local id=tonumber(token)
    if id and id>0 then fn(math.floor(id)); n=n+1 end
  end
  return n
end

function SF.GetSpellName(id)
  id=tonumber(id) or 0
  if id<=0 or type(TaiYangShenDian)~="function" then return "" end
  local ok,row=pcall(TaiYangShenDian,"spell",id)
  if ok and type(row)=="table" then
    local name=tostring(row.nameZhCN or row.name or "")
    if name~="" then return name end
  end
  return ""
end

function SF.RuleIds(rules)
  local cached=nil
  if rules==SF.selfRules then cached=SF.selfIds elseif rules==SF.enemyRules then cached=SF.enemyIds end
  if cached then return cached end
  local ids,id,rule={}
  for id,rule in pairs(rules or {}) do if tonumber(id) then table.insert(ids,tonumber(id)) end end
  table.sort(ids)
  if rules==SF.selfRules then SF.selfIds=ids elseif rules==SF.enemyRules then SF.enemyIds=ids end
  return ids
end

function SF.SelectRule(kind,id)
  id=math.floor(tonumber(id) or 0)
  if id<=0 then return false end
  local rules=(kind=="SELF") and SF.selfRules or SF.enemyRules
  if not rules[id] then return false end
  if kind=="SELF" then
    SF.selectedSelfRule=id
    if SF.ui.selfEdit then SF.ui.selfEdit:SetText(tostring(id)) end
  else
    SF.selectedEnemyRule=id
    if SF.ui.enemyEdit then SF.ui.enemyEdit:SetText(tostring(id)) end
  end
  SF.LoadRule(kind,tostring(id),false)
  if type(Render)=="function" then Render() end
  return true
end

function SF.ClearSelectedRule(kind)
  if kind=="SELF" then
    SF.selectedSelfRule=nil
    if SF.ui.selfEdit then SF.ui.selfEdit:SetText("") end
  else
    SF.selectedEnemyRule=nil
    if SF.ui.enemyEdit then SF.ui.enemyEdit:SetText("") end
  end
end

function SF.DeleteRuleRow(kind,id)
  id=math.floor(tonumber(id) or 0)
  if id<=0 then return end
  SF.RemoveRules(kind,tostring(id),false)
  if kind=="SELF" and SF.selectedSelfRule==id then SF.ClearSelectedRule("SELF") end
  if kind~="SELF" and SF.selectedEnemyRule==id then SF.ClearSelectedRule("ENEMY") end
  if type(Render)=="function" then Render() end
end

function SF.RenderRuleRows(kind)
  local rules=(kind=="SELF") and SF.selfRules or SF.enemyRules
  local count=(kind=="SELF") and SF.selfCount or SF.enemyCount
  local page=(kind=="SELF") and SF.selfPage or SF.enemyPage
  local rows=(kind=="SELF") and SF.ui.selfRows or SF.ui.enemyRows
  local foot=(kind=="SELF") and SF.ui.selfPageText or SF.ui.enemyPageText
  if not rows then return end
  local ids=SF.RuleIds(rules)
  local pageSize=SF.RulePageSize(kind)
  local pages=math.max(1,math.ceil(table.getn(ids)/pageSize))
  page=math.floor(tonumber(page) or 1); if page<1 then page=1 elseif page>pages then page=pages end
  if kind=="SELF" then SF.selfPage=page else SF.enemyPage=page end
  local first=(page-1)*pageSize+1
  local i,id,rule,label,radiusText,decisionText,modeText,styleText,selected,text,row
  for i=1,pageSize do
    row=rows[i]
    id=ids[first+i-1]
    if id and rules[id] then
      rule=rules[id]
      label=(rule.name and rule.name~="" and rule.name) or ("#"..tostring(id))
      radiusText=((tonumber(rule.radius) or 0)>0) and (tostring(rule.radius).."码") or "AUTO"
      decisionText=(kind=="ENEMY") and (" · "..SF.DecisionLabel(rule.decision)) or ""
      modeText=(kind=="ENEMY") and (" · "..SF.RuleModeLabel(rule.mode)) or ""
      styleText=SF.SelfStyleLabel(rule.style)
      selected=(kind=="SELF" and SF.selectedSelfRule==id) or (kind~="SELF" and SF.selectedEnemyRule==id)
      text=(selected and "▶ " or "   ")..tostring(id).."  "..label.."  |  "..radiusText..decisionText..modeText.." · "..styleText
      row.select.ruleID=id; row.delete.ruleID=id
      row.select:SetText(text)
      row.select:Show(); row.delete:Show()
    else
      row.select.ruleID=nil; row.delete.ruleID=nil
      row.select:Hide(); row.delete:Hide()
    end
  end
  if foot then foot:SetText("页 "..tostring(page).."/"..tostring(pages).." · 共 "..tostring(count or 0).." 条") end
end

function SF.SpellDurationSec(spell)
  spell=tonumber(spell) or 0
  if spell<=0 then return nil end
  local cached=SF.durationCache[spell]
  if cached~=nil then return cached or nil end
  if type(TaiYangShenDian)~="function" then return nil end
  local ok,row=pcall(TaiYangShenDian,"spell",spell)
  if not ok or type(row)~="table" then return nil end
  local durationId=tonumber(row.durationIndex) or 0
  if durationId<=0 then SF.durationCache[spell]=false; return nil end
  local ok2,d=pcall(TaiYangShenDian,"spellduration",durationId)
  if not ok2 or type(d)~="table" then return nil end
  local ms=tonumber(d.duration) or 0
  if ms<=0 then SF.durationCache[spell]=false; return nil end
  local sec=ms/1000
  SF.durationCache[spell]=sec
  return sec
end

local function RelationInfo(guid)
  if type(TaiYangShenDian) ~= "function" then return "UNKNOWN",-1,nil,nil end
  local ok,text=pcall(TaiYangShenDian,"AutoRange.RelationByGuid",tostring(guid or ""))
  if not ok or type(text)~="string" then return "UNKNOWN",-1,nil,nil end
  local a=Split(text)
  if a[1]~="R" then return "UNKNOWN",-1,nil,nil end
  local canonicalRaw=tostring(a[6] or "")
  local canonical=(canonicalRaw~="" and NormGuid(canonicalRaw)) or nil
  return tostring(a[2] or "UNKNOWN"),tonumber(a[5]) or -1,canonical,canonicalRaw
end

local function GroupScope(guid)
  if type(TaiYangShenDian)~="function" then return "UNKNOWN" end
  local ok,text=pcall(TaiYangShenDian,"GroundProbe.ScopeByGuid",tostring(guid or ""))
  if not ok or type(text)~="string" then return "UNKNOWN" end
  local a=Split(text)
  if a[1]~="G" then return "UNKNOWN" end
  return tostring(a[2] or "UNKNOWN")
end

local function CasterSourceAllowed(guid)
  local relation,objType,canonical,canonicalRaw=RelationInfo(guid)
  if relation=="HOSTILE" then
    return enemyCircle,"ENEMY",canonical,canonicalRaw
  end
  if relation=="UNKNOWN" then
    -- Fail-safe rule: relation failures follow the enemy switch so real danger is not hidden.
    return enemyCircle,"UNKNOWN",canonical,canonicalRaw
  end
  if relation=="FRIENDLY" then
    -- Friendly NPCs are intentionally not treated as "teammates".
    if objType~=4 then return false,"FRIEND_NPC",canonical,canonicalRaw end
    -- Teammate circles are independent from totem circles. When teammate circles
    -- are disabled, do not spend a native call classifying PARTY/RAID scope.
    if not friendCircle then return false,"FRIEND",canonical,canonicalRaw end
    local scope=GroupScope(guid)
    if scope=="PARTY" or scope=="RAID" then
      return true,"FRIEND",canonical,canonicalRaw
    end
    -- SELF and friendly strangers stay filtered. Totems have their own independent path.
    return false,(scope=="SELF" and "SELF" or "FRIEND_OTHER"),canonical,canonicalRaw
  end
  return enemyCircle,"UNKNOWN",canonical,canonicalRaw
end

-- Geometry semantic correction. Vanilla 1.12 target 18 means
-- TARGET_LOCATION_CASTER_DEST. When a radius-bearing effect pairs it with a
-- destination-area enumerator (8/16/31/34/52), the destination is explicitly
-- the caster, so this is a moving CASTER-centered AoE rather than a free ground
-- DynamicObject. This fixes triggered self-AoEs such as Hellfire without a
-- SpellID whitelist and leaves ordinary ground-target spells unchanged.
local casterDestModeCache={}
local function HasCasterDestRadiusPair(spell)
  spell=tonumber(spell) or 0
  if spell<=0 then return false end
  local cached=casterDestModeCache[spell]
  if cached~=nil then return cached and true or false end
  if type(TaiYangShenDian)~="function" then casterDestModeCache[spell]=false; return false end
  local ok,row=pcall(TaiYangShenDian,"spell",spell)
  if not ok or type(row)~="table" then casterDestModeCache[spell]=false; return false end
  local destEnum={[8]=true,[16]=true,[31]=true,[34]=true,[52]=true}
  local i,ta,tb,rid
  for i=1,3 do
    ta=tonumber(row["targetA"..tostring(i)]) or 0
    tb=tonumber(row["targetB"..tostring(i)]) or 0
    rid=tonumber(row["radiusIndex"..tostring(i)]) or 0
    if rid>0 and ((ta==18 and destEnum[tb]) or (tb==18 and destEnum[ta])) then
      casterDestModeCache[spell]=true
      return true
    end
  end
  casterDestModeCache[spell]=false
  return false
end

local function Resolve(spell)
  spell=tonumber(spell) or 0
  if spell<=0 then return nil end
  local cached=normalResolveCache[spell]
  if cached~=nil then return cached or nil end
  if type(TaiYangShenDian) ~= "function" then return nil end
  local ok,text=pcall(TaiYangShenDian,"AutoRange.Resolve",spell)
  if not ok or type(text)~="string" then return nil end
  local a=Split(text)
  if a[1]~="A" then
    -- NO_RADIUS means DBC lookup succeeded and proved there is no static radius.
    -- Cache only this deterministic miss; other errors remain retryable.
    if a[1]=="E" and a[2]=="NO_RADIUS" then normalResolveCache[spell]=false end
    return nil
  end
  local result={
    geomSpell=tonumber(a[3]) or spell,
    radius=tonumber(a[4]) or 0,
    durationMs=tonumber(a[5]) or 0,
    mode=a[6] or "UNKNOWN",
  }
  -- Native B3.2 historically treats target 16 (AOE-at-destination) as ground
  -- by itself. The paired target 18 supplies that destination as the caster.
  -- Correct only that explicit pair; do not convert generic target-16 ground AoEs.
  if result.mode=="GROUND" and HasCasterDestRadiusPair(result.geomSpell or spell) then
    result.mode="CASTER"
    result.centerFix="T18_CASTER_DEST"
  end
  normalResolveCache[spell]=result
  return result
end

local function ParseUnitPosition(text)
  -- GroundProbe.UnitByGuid hot path: return x/y/z directly, no temporary table.
  text=tostring(text or "")
  if string.sub(text,1,2)~="U|" then return nil end
  local p=3
  local i,q
  for i=2,4 do
    q=string.find(text,"|",p,true)
    if not q then return nil end
    p=q+1
  end
  q=string.find(text,"|",p,true); if not q then return nil end
  local x=tonumber(string.sub(text,p,q-1)); p=q+1
  q=string.find(text,"|",p,true); if not q then return nil end
  local y=tonumber(string.sub(text,p,q-1)); p=q+1
  q=string.find(text,"|",p,true)
  local z=tonumber(q and string.sub(text,p,q-1) or string.sub(text,p))
  if not x or not y or not z then return nil end
  return x,y,z
end

local function CasterPosition(guid)
  if type(TaiYangShenDian) ~= "function" then return nil end
  local ok, text = pcall(TaiYangShenDian, "GroundProbe.UnitByGuid", tostring(guid or ""))
  if not ok or type(text) ~= "string" then return nil end
  return ParseUnitPosition(text)
end

function SF.UnitStateByGuid(guid)
  if type(TaiYangShenDian)~="function" then return nil end
  local ok,state=pcall(TaiYangShenDian,"GroundProbe.UnitStateByGuid",tostring(guid or ""))
  if not ok or type(state)~="table" then return nil end
  return state
end

local function IsEnemySource(h)
  return h and (h.sourceType=="ENEMY" or h.sourceType=="UNKNOWN")
end

local function QueryCasterState(h,cache,checkDead)
  if not h then return nil end
  local guid=h.casterGuid or NormGuid(h.caster)
  if not guid or guid=="0" then return nil end
  local cached=cache and cache[guid] or nil
  if cached and (not checkDead or cached.deadChecked) then return cached end

  if h.sourceType=="SELF" and not checkDead then
    local selfState={guid=guid,visible=false,misses=0,dead=false,deadChecked=false}
    if type(TaiYangShenDian)=="function" then
      local ok,text=pcall(TaiYangShenDian,"GroundProbe.PlayerPosition")
      if ok and type(text)=="string" then
        local a=Split(text)
        local x=tonumber(a[2]); local y=tonumber(a[3]); local z=tonumber(a[4]) or 0
        if string.sub(text,1,2)=="P|" and x and y then selfState.visible=true; selfState.x=x; selfState.y=y; selfState.z=z end
      end
    end
    if cache then cache[guid]=selfState end
    return selfState
  end

  local now=GetTime()
  local rt=casterRuntime[guid]
  if not rt then rt={guid=guid,misses=0}; casterRuntime[guid]=rt end
  if rt.lastQueryAt and now-rt.lastQueryAt<0.05 and (not checkDead or rt.deadCheckedAt==rt.lastQueryAt) then
    if cache then cache[guid]=rt end
    return rt
  end

  rt.dead=false; rt.deadChecked=false; rt.guidMiss=false
  rt.visible=false; rt.x=nil; rt.y=nil; rt.z=nil
  local state=SF.UnitStateByGuid(tostring(h.casterGuidRaw or h.caster or guid))
  if state and state.visible then
    rt.visible=true; rt.misses=0
    rt.x=tonumber(state.x); rt.y=tonumber(state.y); rt.z=tonumber(state.z) or 0
    if checkDead and state.deadKnown then rt.deadChecked=true; rt.dead=state.dead and true or false end
  else
    rt.guidMiss=true
    rt.misses=(rt.misses or 0)+1
  end
  rt.lastQueryAt=now
  rt.deadCheckedAt=rt.deadChecked and now or nil
  if cache then cache[guid]=rt end
  return rt
end

local function PipeNext(text,p)
  local q=string.find(text,"|",p,true)
  if q then return string.sub(text,p,q-1),q+1 end
  return string.sub(text,p),string.len(text)+1
end


local function ParseDynamic(text,out)
  -- Parse D|guid|caster|spell|radius|x|y|z. Exact-GUID tracking passes a
  -- persistent per-hazard table, so steady-state DynamicByGuid allocates no row table.
  text=tostring(text or "")
  if string.sub(text,1,2)~="D|" then return nil end
  local p=3
  local guidRaw,casterRaw,field
  guidRaw,p=PipeNext(text,p)
  casterRaw,p=PipeNext(text,p)
  out=out or {}
  field,p=PipeNext(text,p); out.spell=tonumber(field) or 0
  field,p=PipeNext(text,p); out.radius=tonumber(field) or 0
  field,p=PipeNext(text,p); out.x=tonumber(field)
  field,p=PipeNext(text,p); out.y=tonumber(field)
  field,p=PipeNext(text,p); out.z=tonumber(field)
  out.guidRaw=guidRaw; out.guid=NormGuid(guidRaw)
  out.caster=casterRaw; out.casterGuid=NormGuid(casterRaw)
  return out
end

local function DynamicByGuid(guid,out)
  if type(TaiYangShenDian)~="function" then return nil end
  local ok,text=pcall(TaiYangShenDian,"GroundProbe.DynamicByGuid",tostring(guid or ""))
  if not ok or type(text)~="string" then return nil end
  return ParseDynamic(text,out)
end

local function WipeTable(t)
  local k
  for k in pairs(t) do t[k]=nil end
end
local auraScanTooltip=nil
local function AuraScanTooltip()
  if auraScanTooltip then return auraScanTooltip end
  auraScanTooltip=CreateFrame("GameTooltip","AutoRangeAuraScanTooltip",UIParent,"GameTooltipTemplate")
  auraScanTooltip:SetOwner(UIParent,"ANCHOR_NONE")
  return auraScanTooltip
end

function SF.SpellHasAuraEffect(spell)
  spell=tonumber(spell) or 0
  if spell<=0 then return false end
  local cached=SF.auraCandidateCache[spell]
  if cached~=nil then return cached and true or false end
  if type(TaiYangShenDian)~="function" then SF.auraCandidateCache[spell]=false; return false end
  local ok,row=pcall(TaiYangShenDian,"spell",spell)
  if not ok or type(row)~="table" then SF.auraCandidateCache[spell]=false; return false end
  local yes=false
  local i
  for i=1,3 do
    if (tonumber(row["applyAura"..tostring(i)]) or 0)>0 or (tonumber(row["effect"..tostring(i)]) or 0)==6 then yes=true; break end
  end
  SF.auraCandidateCache[spell]=yes and true or false
  SF.auraNameCache[spell]={tostring(row.nameZhCN or ""),tostring(row.name or "")}
  return yes
end

function SF.GetSpellBookTexture(spell,fallbackName)
  spell=tonumber(spell) or 0
  if spell<=0 then return nil end
  local cached=SF.spellBookTextureCache[spell]
  if cached~=nil then return cached or nil end
  if type(GetSpellName)~="function" or type(GetSpellTexture)~="function" then
    SF.spellBookTextureCache[spell]=false; return nil
  end
  SF.SpellHasAuraEffect(spell)
  local names={}
  local list=SF.auraNameCache[spell]
  local i,n
  if list then
    for i=1,table.getn(list) do n=list[i]; if n and n~="" then names[n]=true end end
  end
  fallbackName=tostring(fallbackName or "")
  if fallbackName~="" then names[fallbackName]=true end
  local book=BOOKTYPE_SPELL or "spell"
  for i=1,512 do
    local ok,name=pcall(GetSpellName,i,book)
    if not ok or not name then break end
    if names[name] then
      local ok2,tex=pcall(GetSpellTexture,i,book)
      if ok2 and tex and tex~="" then SF.spellBookTextureCache[spell]=tex; return tex end
    end
  end
  SF.spellBookTextureCache[spell]=false
  return nil
end

function SF.PlayerHasSpellAura(spell,names,fallbackName,textures)
  -- Match by localized DBC names first, then by the player's own spellbook icon.
  -- The icon fallback is important on 1.12 clients where buff-name APIs/tooltips
  -- can be unavailable or localized differently from the DBC text table.
  SF.SpellHasAuraEffect(spell)
  local list=SF.auraNameCache[tonumber(spell) or 0]
  if list then
    local i,n
    for i=1,table.getn(list) do
      n=list[i]
      if n and n~="" and names[n] then return true end
    end
  end
  fallbackName=tostring(fallbackName or "")
  if fallbackName~="" and names[fallbackName] then return true end
  local tex=SF.GetSpellBookTexture(spell,fallbackName)
  return tex and textures and textures[tex] and true or false
end

function SF.ScanPlayerHelpfulAuras()
  WipeTable(SF.auraNames); WipeTable(SF.auraTextures)
  if type(GetPlayerBuff)~="function" then return SF.auraNames,SF.auraTextures end
  local tip=nil
  local slot,buffIndex,nameLine,name,ok,texture
  for slot=0,63 do
    buffIndex=GetPlayerBuff(slot,"HELPFUL")
    if not buffIndex or buffIndex<0 then break end

    name=nil
    if type(GetPlayerBuffName)=="function" then
      ok,name=pcall(GetPlayerBuffName,buffIndex)
      if not ok then name=nil end
    end
    if not name or name=="" then
      if not tip then tip=AuraScanTooltip() end
      tip:ClearLines(); tip:SetPlayerBuff(buffIndex)
      nameLine=getglobal("AutoRangeAuraScanTooltipTextLeft1")
      name=nameLine and nameLine:GetText() or nil
    end
    if name and name~="" then SF.auraNames[name]=true end

    texture=nil
    if type(GetPlayerBuffTexture)=="function" then
      ok,texture=pcall(GetPlayerBuffTexture,buffIndex)
      if not ok then texture=nil end
    end
    if texture and texture~="" then SF.auraTextures[texture]=true end
  end
  if tip then tip:Hide() end
  return SF.auraNames,SF.auraTextures
end

function SF.MessageMentionsSpell(spell,msg,fallbackName)
  msg=tostring(msg or "")
  if msg=="" then return false end
  SF.SpellHasAuraEffect(spell)
  local list=SF.auraNameCache[tonumber(spell) or 0]
  local i,n
  if list then
    for i=1,table.getn(list) do
      n=list[i]
      if n and n~="" and string.find(msg,n,1,true) then return true end
    end
  end
  fallbackName=tostring(fallbackName or "")
  return fallbackName~="" and string.find(msg,fallbackName,1,true) and true or false
end

function SF.HandleSelfAuraGone(msg)
  if not SF.selfEnabled or SF.selfActiveCount<=0 then return 0 end
  local remove={}
  local i,h,rule,name
  for i=1,table.getn(order) do
    h=hazards[order[i]]
    if h and h.sourceType=="SELF" and (h.selfAuraManaged or h.selfAuraHeld or h.selfAuraSeen) then
      rule=SF.selfRules[h.spell]
      name=(rule and rule.name) or ""
      if SF.MessageMentionsSpell(h.spell,msg,name) then remove[table.getn(remove)+1]=h.key end
    end
  end
  for i=1,table.getn(remove) do RemoveHazard(remove[i]) end
  if table.getn(remove)>0 and ApplyRuntimeHooks then ApplyRuntimeHooks() end
  return table.getn(remove)
end

function SF.RefreshSelfAuraHazards()
  if not SF.selfEnabled or SF.selfActiveCount<=0 then return 0 end
  local names,textures=SF.ScanPlayerHelpfulAuras()
  local remove={}
  local now=GetTime()
  local i,h,rule,name
  for i=1,table.getn(order) do
    h=hazards[order[i]]
    if h and h.sourceType=="SELF" then
      rule=SF.selfRules[h.spell]
      name=(rule and rule.name) or ""
      if name=="" then
        name=SF.GetSpellName(h.spell)
        if rule and name~="" then rule.name=name; SF.SaveRules() end
      end
      if SF.PlayerHasSpellAura(h.spell,names,name,textures) then
        h.selfAuraSeen=true; h.selfAuraHeld=true; h.selfAuraManaged=true
        -- Aura presence owns lifetime. expires remains only a fallback for the
        -- brief CAST->PLAYER_AURAS_CHANGED window before the aura is observed.
        if (h.expires or 0)<now+1.0 then h.expires=now+1.0 end
      elseif h.selfAuraSeen then
        h.selfAuraHeld=nil
        remove[table.getn(remove)+1]=h.key
      end
    end
  end
  for i=1,table.getn(remove) do RemoveHazard(remove[i]) end
  if table.getn(remove)>0 and ApplyRuntimeHooks then ApplyRuntimeHooks() end
  return table.getn(remove)
end


local function DynamicSnapshot(list,seen)
  list=list or {}
  seen=seen or {}
  WipeTable(list); WipeTable(seen)
  if type(TaiYangShenDian) ~= "function" then return list end
  local now=GetTime()
  local ok, raw = pcall(TaiYangShenDian, "GroundProbe.Snapshot", scanRange, false)
  if not ok or type(raw) ~= "string" then return list end
  Lines(raw, function(line)
    if string.sub(line,1,2)=="D|" then
      local d=ParseDynamic(line)
      if d and d.guid~="0" and not seen[d.guid] then
        seen[d.guid]=true
        local m=dynamicMeta[d.guid]
        if not m then m={firstSeen=now,lastSeen=now}; dynamicMeta[d.guid]=m else m.lastSeen=now end
        d.firstSeen=m.firstSeen
        list[table.getn(list)+1]=d
      end
    end
  end)
  local k,m
  for k,m in pairs(dynamicMeta) do
    if now-(m.lastSeen or now)>8.0 then dynamicMeta[k]=nil end
  end
  return list
end

local function FindDynamic(h, dynamics, claimed)
  local i
  local wantedCaster=h.casterGuid or NormGuid(h.caster)

  -- Exact spell + caster is authoritative. DynamicSnapshot already normalizes
  -- both GUIDs, so do not repeat NormGuid() inside every candidate comparison.
  for i=1,table.getn(dynamics) do
    local d=dynamics[i]
    local spellMatch=(d.spell==h.spell or d.spell==h.geomSpell)
    local casterMatch=(d.casterGuid==wantedCaster)
    local available=(not claimed or not claimed[d.guid] or claimed[d.guid]==h.key)
    if spellMatch and casterMatch and available then return d end
  end

  -- Some DynamicObjects expose a trigger-child spell or no caster. During the
  -- short creation window accept a UNIQUE exact-spell object.
  local now=GetTime()
  local found=nil
  if now-(h.started or 0)<=1.50 then
    for i=1,table.getn(dynamics) do
      local d=dynamics[i]
      if (d.spell==h.spell or d.spell==h.geomSpell)
          and (d.firstSeen or now)>=(h.started or now)-0.20
          and (not claimed or not claimed[d.guid] or claimed[d.guid]==h.key) then
        if found then found=nil; break end
        found=d
      end
    end
    if found then return found end

    -- Last fallback: a UNIQUE newly-created DynamicObject from the same caster.
    -- This handles parent cast -> trigger child -> DynamicObject chains without
    -- guessing when several ground objects are spawned at once.
    found=nil
    for i=1,table.getn(dynamics) do
      local d=dynamics[i]
      if d.casterGuid==wantedCaster
          and (d.firstSeen or now)>=(h.started or now)-0.20
          and (not claimed or not claimed[d.guid] or claimed[d.guid]==h.key) then
        if found then found=nil; break end
        found=d
      end
    end
    if found then return found end
  end
  return nil
end

local function ScopeAllowedForMode(scope,mode)
  scope=string.upper(tostring(scope or "UNKNOWN"))
  mode=string.upper(tostring(mode or "OFF"))
  if mode=="OFF" then return false end
  if mode=="ALL" then return true end
  if mode=="SELF" then return scope=="SELF" end
  if mode=="PARTY" then return scope=="SELF" or scope=="PARTY" end
  if mode=="PARTY_ONLY" then return scope=="PARTY" end
  if mode=="RAID" then return scope=="SELF" or scope=="PARTY" or scope=="RAID" end
  return false
end

local function HazardFilterVisible(h)
  if not h or not running or not visualEnabled then return false end
  if h.totemOnly then return ScopeAllowedForMode(h.ownerScope,totemMode) end
  -- Spell membership is enforced only at event/user-action boundaries, never in the
  -- 0.20s hot path. Live disallowed hazards are removed immediately when a list changes.
  if h.sourceType=="SELF" then return SF.selfEnabled end
  if h.sourceType=="FRIEND" then return friendCircle end
  if h.sourceType=="ENEMY" or h.sourceType=="UNKNOWN" then return enemyCircle end
  return false
end

-- R7.2 enemy performance guard. The linked list exists only while the guard is
-- enabled and contains only enemy/UNKNOWN hazards that currently have runtime work.
-- Oldest active enemy circle is retired first, so newly-created danger remains visible.
local function EnemyLimitUntrack(h)
  if not h or not h.enemyLimitTracked then return end
  local prev=h.enemyLimitPrev
  local nextKey=h.enemyLimitNext
  if prev then
    local p=hazards[prev]
    if p then p.enemyLimitNext=nextKey end
  else
    enemyLimitHead=nextKey
  end
  if nextKey then
    local n=hazards[nextKey]
    if n then n.enemyLimitPrev=prev end
  else
    enemyLimitTail=prev
  end
  h.enemyLimitTracked=nil
  h.enemyLimitPrev=nil
  h.enemyLimitNext=nil
  enemyLimitCount=math.max(0,(enemyLimitCount or 1)-1)
end

local function EnemyLimitTrack(h)
  if not enemyLimitEnabled or not h or h.enemyLimitTracked then return end
  if h.totemOnly or (h.sourceType~="ENEMY" and h.sourceType~="UNKNOWN") then return end
  if not h.runtimeClass then return end
  h.enemyLimitTracked=true
  h.enemyLimitPrev=enemyLimitTail
  h.enemyLimitNext=nil
  if enemyLimitTail then
    local tail=hazards[enemyLimitTail]
    if tail then tail.enemyLimitNext=h.key end
  else
    enemyLimitHead=h.key
  end
  enemyLimitTail=h.key
  enemyLimitCount=enemyLimitCount+1
end

local function SyncEnemyLimitTracking(h)
  if enemyLimitEnabled and h and h.runtimeClass and not h.totemOnly
      and (h.sourceType=="ENEMY" or h.sourceType=="UNKNOWN") then
    EnemyLimitTrack(h)
  else
    EnemyLimitUntrack(h)
  end
end

local function ResetEnemyLimitTracking()
  enemyLimitHead=nil; enemyLimitTail=nil; enemyLimitCount=0
  local i
  for i=1,table.getn(order) do
    local h=hazards[order[i]]
    if h then
      h.enemyLimitTracked=nil
      h.enemyLimitPrev=nil
      h.enemyLimitNext=nil
    end
  end
end

local function RuntimeBucketAdd(list,index,key)
  if not key or index[key] then return end
  table.insert(list,key)
  index[key]=table.getn(list)
end

local function RuntimeBucketRemove(list,index,key)
  local pos=key and index[key] or nil
  if not pos then return end
  local last=table.getn(list)
  local lastKey=list[last]
  if pos~=last then
    list[pos]=lastKey
    index[lastKey]=pos
  end
  table.remove(list,last)
  index[key]=nil
end

local function RuntimeBucketClear(list,index)
  local i
  for i=table.getn(list),1,-1 do list[i]=nil end
  local k
  for k in pairs(index) do index[k]=nil end
end

local function RegisterHazard(h)
  if not h or not h.key then return nil end
  local key=h.key
  if hazards[key] then return hazards[key] end
  hazards[key]=h
  order[table.getn(order)+1]=key
  orderIndex[key]=table.getn(order)
  if h.modeHint=="CASTER" and not h.totemOnly and h.casterGuid then
    casterHazardCounts[h.casterGuid]=(casterHazardCounts[h.casterGuid] or 0)+1
    casterReuseByToken[tostring(h.sourceType or "UNKNOWN")..":"..h.casterGuid..":"..tostring(h.spell or 0)]=key
  end
  return h
end

local function UnregisterHazardKey(key,h)
  local pos=orderIndex[key]
  if pos then
    local last=table.getn(order)
    local lastKey=order[last]
    if pos~=last then order[pos]=lastKey; orderIndex[lastKey]=pos end
    order[last]=nil
    orderIndex[key]=nil
  end
  if h and h.modeHint=="CASTER" and not h.totemOnly and h.casterGuid then
    local guid=h.casterGuid
    local n=(casterHazardCounts[guid] or 1)-1
    if n<=0 then casterHazardCounts[guid]=nil; casterRuntime[guid]=nil else casterHazardCounts[guid]=n end
    local token=tostring(h.sourceType or "UNKNOWN")..":"..guid..":"..tostring(h.spell or 0)
    if casterReuseByToken[token]==key then casterReuseByToken[token]=nil end
  end
end

local function SetHazardRuntimeClass(h,className)
  if not h or not h.key then return end
  local old=h.runtimeClass
  if old==className then return end
  if old=="CASTER" then RuntimeBucketRemove(activeCasterKeys,activeCasterIndex,h.key)
  elseif old=="DISCOVERY" then RuntimeBucketRemove(activeDiscoveryKeys,activeDiscoveryIndex,h.key)
  elseif old=="DYNAMIC" then RuntimeBucketRemove(activeDynamicKeys,activeDynamicIndex,h.key)
  elseif old=="TOTEM" then RuntimeBucketRemove(activeTotemKeys,activeTotemIndex,h.key) end
  h.runtimeClass=nil
  if className=="CASTER" then RuntimeBucketAdd(activeCasterKeys,activeCasterIndex,h.key)
  elseif className=="DISCOVERY" then RuntimeBucketAdd(activeDiscoveryKeys,activeDiscoveryIndex,h.key)
  elseif className=="DYNAMIC" then RuntimeBucketAdd(activeDynamicKeys,activeDynamicIndex,h.key)
  elseif className=="TOTEM" then RuntimeBucketAdd(activeTotemKeys,activeTotemIndex,h.key) end
  h.runtimeClass=className
  SyncEnemyLimitTracking(h)
end

local function DesiredHazardRuntimeClass(h)
  if not h or not HazardFilterVisible(h) then return nil end
  if h.totemOnly then return "TOTEM" end
  if h.dynamicGuid then return "DYNAMIC" end
  if h.modeHint=="CASTER" then return "CASTER" end
  if (h.modeHint=="GROUND" or h.modeHint=="UNKNOWN") and not h.castPending then return "DISCOVERY" end
  return nil
end

local function RefreshHazardRuntimeClass(h)
  SetHazardRuntimeClass(h,DesiredHazardRuntimeClass(h))
end

local function RebuildRuntimeBuckets()
  RuntimeBucketClear(activeCasterKeys,activeCasterIndex)
  RuntimeBucketClear(activeDiscoveryKeys,activeDiscoveryIndex)
  RuntimeBucketClear(activeDynamicKeys,activeDynamicIndex)
  RuntimeBucketClear(activeTotemKeys,activeTotemIndex)
  -- Reuse the existing rebuild traversal; when the guard is OFF and no guard
  -- state exists, this adds no second pass over hazards.
  local clearEnemyLimitState=enemyLimitEnabled or enemyLimitCount>0 or enemyLimitHead or enemyLimitTail
  enemyLimitHead=nil; enemyLimitTail=nil; enemyLimitCount=0
  local i
  for i=1,table.getn(order) do
    local h=hazards[order[i]]
    if h then
      if clearEnemyLimitState then
        h.enemyLimitTracked=nil; h.enemyLimitPrev=nil; h.enemyLimitNext=nil
      end
      h.runtimeClass=nil
      RefreshHazardRuntimeClass(h)
    end
  end
  if EnforceEnemyLimit then EnforceEnemyLimit() end
end

local function ModelDiameterYards(path)
  local _,_,w=string.find(tostring(path or ""), "_W([0-9]+)_")
  local n=tonumber(w)
  if n and n>0 then return n end
  return nil
end

local function VisualDiameterYards(visual)
  if type(visual)=="table" then
    local d=tonumber(visual.diameter)
    if d and d>0 then return d end
    return ModelDiameterYards(visual.path)
  end
  return ModelDiameterYards(visual)
end

local function WorldRadiusScale(radiusYards, visual)
  local r=tonumber(radiusYards) or 0
  if r<=0 then return nil end
  local modelDiameter=VisualDiameterYards(visual)
  if not modelDiameter then return nil end
  return (r*2)/modelDiameter
end

local function FindVisualById(style)
  style=string.upper(tostring(style or "AUTO"))
  local i
  for i=1,table.getn(visualPool) do
    if visualPool[i].id==style then return visualPool[i],i end
  end
  if M2 and M2.customById and M2.customById[style] then return M2.customById[style],nil end
  return nil,nil
end

local function PickTotemAutoSizeVisual(radiusYards)
  local r=tonumber(radiusYards) or 0
  if r<=0 then return nil end
  local targetDiameter=r*2
  local best=nil
  local bestDelta=nil
  local bestW=nil
  local i
  for i=1,table.getn(totemAutoSizePool) do
    local v=totemAutoSizePool[i]
    local w=VisualDiameterYards(v)
    if w and w>0 then
      local scale=targetDiameter/w
      if scale>=0.05 and scale<=20.0 then
        local delta=math.abs(scale-1.0)
        -- Stable tie-break: prefer the larger W, which keeps scale lower.
        if (not best) or delta<(bestDelta-0.000001)
            or (math.abs(delta-bestDelta)<=0.000001 and w>(bestW or 0)) then
          best=v; bestDelta=delta; bestW=w
        end
      end
    end
  end
  return best
end

TotemVisualStyleLabel=function(style)
  style=string.upper(tostring(style or "AUTO"))
  if style=="AUTO" then return "自动尺寸" end
  local v=FindVisualById(style)
  return v and v.label or "自动尺寸"
end

function SF.ValidTotemVisualStyle(style,fallback)
  style=string.upper(tostring(style or fallback or "AUTO"))
  if style=="AUTO" then return "AUTO" end
  if FindVisualById(style) then return style end
  return string.upper(tostring(fallback or "AUTO"))
end

function SF.TotemStyleForHazard(h)
  -- Custom rule always wins over unified/category style.
  if h and h.totemCustomStyle then return SF.ValidTotemVisualStyle(h.totemCustomStyle,"AUTO") end
  if SF.totemUnifiedStyle then return SF.ValidTotemVisualStyle(totemVisualStyle,"AUTO") end
  local element=(h and h.totemElement) or "OTHER"
  if not SF.totemCategoryStyles[element] then element="OTHER" end
  return SF.ValidTotemVisualStyle(SF.totemCategoryStyles[element],"AUTO")
end

function SF.SelfStyleLabel(style)
  local v=FindVisualById(style)
  return v and v.label or "白色"
end

local function HashStartIndex(h)
  local n=(tonumber(h.spell) or 0)*131 + math.floor((h.started or GetTime())*1000)
  local s=tostring(h.caster or "")
  local i
  for i=1,string.len(s) do n=n+string.byte(s,i)*i end
  local count=table.getn(visualPool)
  if count<=0 then return 1 end
  return math.mod(n,count)+1
end

local function PickVisual(h)
  local count=table.getn(visualPool)
  if count<=0 then return nil,nil end
  if h and h.totemOnly then
    local totemStyle=SF.TotemStyleForHazard(h)
    if totemStyle=="AUTO" then
      local autoV=PickTotemAutoSizeVisual(h.radius)
      if autoV then
        h.visualIndex=nil
        h.visual=autoV
        return h.visual,nil
      end
    else
      local forced,forcedIndex=FindVisualById(totemStyle)
      if forced then
        h.visualIndex=forcedIndex
        h.visual=forced
        return h.visual,forcedIndex
      end
    end
  end
  if h and h.ruleStyle then
    local forced,forcedIndex=FindVisualById(h.ruleStyle)
    if forced then
      h.visualIndex=forcedIndex
      h.visual=forced
      return h.visual,forcedIndex
    end
  end
  if not h.visualIndex then h.visualIndex=HashStartIndex(h) end
  if not h.visual then h.visual=visualPool[h.visualIndex] end
  return h.visual,h.visualIndex
end

ClearVisual=function(h)
  if not h then return end
  if h.visualCreated and type(TaiYangShenDian)=="function" then
    pcall(TaiYangShenDian,"AutoRange.VisualClear",h.key)
  end
  h.visualCreated=false
  h.visualIndex=nil
  h.visual=nil
  h.visualBornAt=nil
  h.visualHidden=nil
  h.nextVisualRetry=nil
  h.nextVisualRestartRetry=nil
  h.visualX=nil; h.visualY=nil; h.visualZ=nil; h.visualScale=nil
  h.visualSetFails=0
  h.visualMoveMisses=0
end

local function HideVisual(h)
  if not h or not h.visualCreated or h.visualHidden then return true end
  if type(TaiYangShenDian)~="function" then return false end
  local ok,hidden=pcall(TaiYangShenDian,"AutoRange.VisualHide",h.key)
  if ok and hidden then
    h.visualHidden=true
    return true
  end
  return false
end

local function ShowVisual(h)
  if not h or not h.visualCreated or not h.visualHidden then return true end
  if type(TaiYangShenDian)~="function" then return false end
  local ok,shown=pcall(TaiYangShenDian,"AutoRange.VisualShow",h.key)
  if ok and shown then
    h.visualHidden=nil
    return true
  end
  return false
end

RemoveHazard=function(key)
  local h=hazards[key]
  if not h then return end
  Warn.dirty=true
  if h.castToken and pendingCastByToken[h.castToken]==key then pendingCastByToken[h.castToken]=nil end
  SetHazardRuntimeClass(h,nil)
  EnemyLimitUntrack(h)
  ClearVisual(h)
  hazards[key]=nil
  UnregisterHazardKey(key,h)
end

EnforceEnemyLimit=function()
  if not enemyLimitEnabled then return end
  local maxCount=math.floor(tonumber(enemyLimitMax) or 20)
  if maxCount<1 then maxCount=1 end
  while enemyLimitCount>maxCount and enemyLimitHead do
    local oldest=enemyLimitHead
    RemoveHazard(oldest)
  end
end

SetVisual=function(h)
  if not h or h.radius<=0 or not h.x then return end
  if h.modeNow~="CASTER" and h.modeNow~="DYNAMIC" and h.modeNow~="TOTEM" then return end

  if not HazardFilterVisible(h) then
    HideVisual(h)
    return
  end

  if h.visualCreated and h.visualHidden then
    -- Native preview slots can be reclaimed while a Lua hazard still remembers a
    -- hidden handle.  Do not leave that hazard permanently invisible: if Show
    -- misses, release the stale bookkeeping and recreate the same logical circle.
    if not ShowVisual(h) then ClearVisual(h) end
  end

  local v,visualIndex=PickVisual(h)
  if not v then return end
  local scale=WorldRadiusScale(h.radius,v)
  if not scale or scale<0.05 or scale>20.00 then return end
  local now=GetTime()
  local z=(h.z or 0)+0.03

  -- Finite S30 assets restart the SAME native model sequence in place.
  if h.visualCreated and h.visualBornAt and now-h.visualBornAt>=visualMaxAge
      and now>=(h.nextVisualRestartRetry or 0) then
    local ok,restarted=pcall(TaiYangShenDian,"AutoRange.VisualRestart",h.key)
    if ok and restarted then
      h.visualBornAt=now; h.nextVisualRestartRetry=nil
    else
      h.nextVisualRestartRetry=now+2.0
    end
  end

  if not h.visualCreated then
    if now<(h.nextVisualRetry or 0) then return end
    local callOk,created=pcall(TaiYangShenDian,"AutoRange.VisualSet",h.key,v.path,h.x,h.y,z,scale,0)
    if callOk and created then
      h.visualCreated=true; h.visualIndex=visualIndex; h.visualBornAt=now
      h.visualHidden=nil; h.nextVisualRetry=nil; h.visualSetFails=0; h.visualMoveMisses=0
      h.visualX=h.x; h.visualY=h.y; h.visualZ=z; h.visualScale=scale
    else
      h.visualSetFails=(h.visualSetFails or 0)+1
      local n=h.visualSetFails
      if n==1 then h.nextVisualRetry=now+0.20
      elseif n==2 then h.nextVisualRetry=now+0.40
      elseif n==3 then h.nextVisualRetry=now+0.80
      elseif n==4 then h.nextVisualRetry=now+1.50
      elseif n==5 then h.nextVisualRetry=now+2.50
      else h.nextVisualRetry=now+4.00 end
      if n==1 and PurgeStaleHiddenHazards then PurgeStaleHiddenHazards(h.key) end
    end
    return
  end

  -- R7.0: absolutely no VisualMove for an unchanged transform. Native M2 work is
  -- driven by real movement/scale change only; static caster/ground/totem circles
  -- therefore generate zero VisualMove calls after creation.
  local movedEnough=(not h.visualX)
    or math.abs((h.x or 0)-(h.visualX or 0))>0.03
    or math.abs((h.y or 0)-(h.visualY or 0))>0.03
    or math.abs(z-(h.visualZ or z))>0.02
    or math.abs(scale-(h.visualScale or scale))>0.001
  if not movedEnough then return end

  local moveCallOk,moved=pcall(TaiYangShenDian,"AutoRange.VisualMove",h.key,h.x,h.y,z,scale,0)
  if moveCallOk and moved then
    h.visualMoveMisses=0
    h.visualX=h.x; h.visualY=h.y; h.visualZ=z; h.visualScale=scale
  else
    h.visualMoveMisses=(h.visualMoveMisses or 0)+1
    if h.visualMoveMisses>=3 then
      h.visualCreated=false; h.visualHidden=nil; h.nextVisualRetry=now+0.20
    end
  end
end

local function UpdateHazard(h,dynamics,claimed,casterStateCache)
  local now=GetTime()
  local d=nil

  if h.dynamicGuid then
    h.dynamicState=h.dynamicState or {}
    d=DynamicByGuid(h.dynamicGuidRaw or h.dynamicGuid,h.dynamicState)
    if d then
      h.dynamicMissCount=0
    else
      h.dynamicMissCount=(h.dynamicMissCount or 0)+1
      if h.dynamicMissCount>=2 then
        HideVisual(h)
        return false
      end
    end
  elseif dynamics and table.getn(dynamics)>0
      and now<=(h.dynamicDiscoverUntil or 0) then
    d=FindDynamic(h,dynamics,claimed)
  end

  if d then
    h.modeNow="DYNAMIC"; h.x=d.x; h.y=d.y; h.z=d.z
    h.dynamicGuid=d.guid
    h.dynamicGuidRaw=d.guidRaw or d.guid
    h.dynamicMissCount=0
    if claimed then claimed[h.dynamicGuid]=h.key end
    if (not h.manualRadius or h.manualRadius<=0) and d.radius and d.radius>0 then h.radius=d.radius end
    h.lastDynamic=now
    if h.expires<now+0.50 then h.expires=now+0.50 end
  elseif h.dynamicGuid and h.dynamicMissCount and h.dynamicMissCount<2 then
    h.modeNow="DYNAMIC"
  elseif h.modeHint=="CASTER" then
    if h.sourceType=="SELF" then
      local state=QueryCasterState(h,casterStateCache,false)
      if state and state.visible then
        h.modeNow="CASTER"; h.x=state.x; h.y=state.y; h.z=state.z
      else
        -- A local-player position miss is not a lifecycle signal. If the Buff is
        -- still present, keep the last known visual and try again next 0.20s Poll.
        h.modeNow="CASTER?"
        return true
      end
    elseif IsEnemySource(h) then
      local state=QueryCasterState(h,casterStateCache,true)
      if state and state.dead then
        HideVisual(h)
        return false
      end
      if state and state.visible then
        h.modeNow="CASTER"; h.x=state.x; h.y=state.y; h.z=state.z
      else
        -- Never leave a stale CASTER circle parked on the last corpse/position.
        -- One visibility miss is tolerated; two consecutive misses retire it.
        h.modeNow="CASTER?"
        HideVisual(h)
        if state and state.misses>=2 then return false end
        return true
      end
    else
      -- Teammates share the same per-GUID position cache; no death query is needed.
      local state=QueryCasterState(h,casterStateCache,false)
      if state and state.visible then
        h.modeNow="CASTER"; h.x=state.x; h.y=state.y; h.z=state.z
      else
        h.modeNow="CASTER?"; HideVisual(h)
        if state and state.misses>=2 then return false end
        return true
      end
    end
  elseif h.modeHint=="GROUND" then
    h.modeNow="GROUND?"
  else
    h.modeNow="UNKNOWN"
  end
  SetVisual(h)
  return true
end


local function CsvSpellIds(csv)
  local out={}
  local p=1
  csv=tostring(csv or "")
  while p<=string.len(csv) do
    local q=string.find(csv,",",p,true)
    local part
    if q then part=string.sub(csv,p,q-1); p=q+1
    else part=string.sub(csv,p); p=string.len(csv)+1 end
    local id=tonumber(part)
    if id and id>0 then table.insert(out,id) end
  end
  return out
end

-- R6.3 dynamic totem Spell-chain resolver.  This uses the Reliquary-derived
-- TaiYangShenDian("spell") / TaiYangShenDian("spellradius") APIs and never keeps a polling loop.
-- No SpellID->radius table exists here: roots come from the live totem row and
-- every TriggerSpell edge is followed on demand, with a loop guard and depth cap.
local function ChainSpellRow(spell)
  spell=tonumber(spell) or 0
  if spell<=0 then return nil end
  local cached=chainSpellCache[spell]
  if cached~=nil then return cached or nil end
  if type(TaiYangShenDian)~="function" then chainSpellCache[spell]=false; return nil end
  local ok,row=pcall(TaiYangShenDian,"spell",spell)
  if ok and type(row)=="table" then
    chainSpellCache[spell]=row
    return row
  end
  chainSpellCache[spell]=false
  return nil
end

local function ChainRadiusRow(radiusId)
  radiusId=tonumber(radiusId) or 0
  if radiusId<=0 then return nil end
  local cached=chainRadiusCache[radiusId]
  if cached~=nil then return cached or nil end
  if type(TaiYangShenDian)~="function" then chainRadiusCache[radiusId]=false; return nil end
  local ok,row=pcall(TaiYangShenDian,"spellradius",radiusId)
  if ok and type(row)=="table" then
    chainRadiusCache[radiusId]=row
    return row
  end
  chainRadiusCache[radiusId]=false
  return nil
end

-- Stage2.28: Searing Totem is a special case.  Its summon spell text contains
-- multiple yard values and the generic TEXT fallback can incorrectly pick 1yd.
-- Keep the generic resolver untouched for every other totem.
local function IsSearingTotemSpell(spell)
  local row=ChainSpellRow(spell)
  if not row then return false end
  local name=tostring(row.nameZhCN or "")
  if name=="" then name=tostring(row.name or row.nameEnUS or "") end
  local lower=string.lower(name)
  if string.find(lower,"searing totem",1,true) then return true end
  if string.find(name,"灼热图腾",1,true) then return true end
  return false
end

local function SearingTotemSelfRangeBonus()
  -- No polling/cached OnUpdate: this is queried only when a SELF Searing Totem is
  -- actually bound, i.e. at most once per summon.  Flame Guidance is one rank and
  -- adds 10yd to Searing Totem range on the Turtle talent tree that has it.
  if type(GetTalentInfo)~="function" then return 0 end
  local tabs=3
  if type(GetNumTalentTabs)=="function" then
    local ok,n=pcall(GetNumTalentTabs)
    if ok and tonumber(n) and tonumber(n)>0 then tabs=tonumber(n) end
  end
  local tab,index
  for tab=1,tabs do
    local count=20
    if type(GetNumTalents)=="function" then
      local ok,n=pcall(GetNumTalents,tab)
      if ok and tonumber(n) and tonumber(n)>0 then count=tonumber(n) end
    end
    for index=1,count do
      local ok,name,_,_,_,rank=pcall(GetTalentInfo,tab,index)
      if ok and name and (tonumber(rank) or 0)>0 then
        local n=tostring(name)
        local lower=string.lower(n)
        if string.find(lower,"flame guidance",1,true)
          or string.find(n,"火焰指引",1,true)
          or string.find(n,"烈焰指引",1,true)
          or string.find(n,"火焰引导",1,true) then
          return 10
        end
      end
    end
  end
  return 0
end

ResolveSearingTotemRange=function(createdSpell,scope)
  if not IsSearingTotemSpell(createdSpell) then return nil,nil end
  local radius=20
  if string.upper(tostring(scope or ""))=="SELF" then
    radius=radius+SearingTotemSelfRangeBonus()
  end
  return radius,tonumber(createdSpell) or 0
end

local function ChainAddTextCandidate(out,seen,spell,depth,rootKind,text,strongScore,plainScore)
  if type(text)~="string" or text=="" then return end
  local function addRadius(value,strong)
    local r=tonumber(value) or 0
    if r<=0 or r>500 then return end
    local key=tostring(spell)..":"..string.format("%.3f",r)
    local old=seen[key]
    local score=(strong and strongScore or plainScore) + ((rootKind=="AURA") and 60 or 20) - depth*2
    if old and old.score>=score then return end
    local c={kind="TEXT",radius=r,spell=spell,depth=depth,effect=0,score=score}
    if old then
      local i
      for i=1,table.getn(out) do if out[i]==old then out[i]=c; break end end
    else
      table.insert(out,c)
    end
    seen[key]=c
  end

  -- Strong forms: explicit "radius 30 yards" / "30 yard radius" / "半径30码".
  local n
  for n in string.gfind(text,"半径%s*(%d+%.?%d*)%s*码") do addRadius(n,true) end
  for n in string.gfind(text,"[Rr][Aa][Dd][Ii][Uu][Ss]%s*(%d+%.?%d*)%s*[Yy][Aa][Rr][Dd][Ss]?") do addRadius(n,true) end
  for n in string.gfind(text,"(%d+%.?%d*)%s*[Yy][Aa][Rr][Dd][Ss]?%s*[Rr][Aa][Dd][Ii][Uu][Ss]") do addRadius(n,true) end

  -- Weaker fallback: any yard/码 number in the same spell description.  This
  -- never receives the same confidence as an explicit radius phrase.
  for n in string.gfind(text,"(%d+%.?%d*)%s*码") do addRadius(n,false) end
  for n in string.gfind(text,"(%d+%.?%d*)%s*[Yy][Aa][Rr][Dd][Ss]?") do addRadius(n,false) end
end

local function ChainCandidateBetter(a,b)
  if not b then return true end
  if (a.score or 0)~=(b.score or 0) then return (a.score or 0)>(b.score or 0) end
  -- Equal confidence: prefer structured DBC geometry over text, then the
  -- shallower chain node.  Radius magnitude is deliberately NOT a tie-breaker.
  if a.kind~=b.kind then return a.kind=="STRUCT" end
  if (a.depth or 99)~=(b.depth or 99) then return (a.depth or 99)<(b.depth or 99) end
  if (a.effect or 99)~=(b.effect or 99) then return (a.effect or 99)<(b.effect or 99) end
  return false
end

local function ResolveTotemSpellChain(createdSpell,auraCsv)
  if type(TaiYangShenDian)~="function" then return nil,"NO_TYS" end

  local roots={}
  local rootSeen={}
  local auraIds=CsvSpellIds(auraCsv)
  local i
  local function addRoot(id,kind)
    id=tonumber(id) or 0
    if id<=0 or rootSeen[id] then return end
    rootSeen[id]=true
    table.insert(roots,{id=id,kind=kind})
  end
  for i=1,table.getn(auraIds) do addRoot(auraIds[i],"AURA") end
  addRoot(createdSpell,"CREATED")
  if table.getn(roots)==0 then return nil,"NO_ROOT_SPELL" end

  local queue={}
  for i=1,table.getn(roots) do
    table.insert(queue,{id=roots[i].id,depth=0,rootKind=roots[i].kind})
  end
  local head=1
  local visited={}
  local visitedCount=0
  local candidates={}
  local textSeen={}
  while head<=table.getn(queue) and visitedCount<64 do
    local node=queue[head]; head=head+1
    local id=tonumber(node.id) or 0
    if id>0 and not visited[id] then
      visited[id]=true
      visitedCount=visitedCount+1
      local sp=ChainSpellRow(id)
      if sp then
        -- Client tooltip text is only a dynamic cross-check/fallback candidate.
        -- It is never hard-coded by SpellID and strong explicit "radius" wording
        -- is scored above a stale radius field when the two disagree.
        ChainAddTextCandidate(candidates,textSeen,id,node.depth,node.rootKind,sp.descriptionZhCN,690,500)
        ChainAddTextCandidate(candidates,textSeen,id,node.depth,node.rootKind,sp.descriptionEnUS,680,490)
        ChainAddTextCandidate(candidates,textSeen,id,node.depth,node.rootKind,sp.auraDescriptionZhCN,650,470)
        ChainAddTextCandidate(candidates,textSeen,id,node.depth,node.rootKind,sp.auraDescriptionEnUS,640,460)

        for i=1,3 do
          local effect=tonumber(sp["effect"..tostring(i)]) or 0
          local targetA=tonumber(sp["targetA"..tostring(i)]) or 0
          local targetB=tonumber(sp["targetB"..tostring(i)]) or 0
          local radiusIndex=tonumber(sp["radiusIndex"..tostring(i)]) or 0
          local applyAura=tonumber(sp["applyAura"..tostring(i)]) or 0
          local trigger=tonumber(sp["triggerSpell"..tostring(i)]) or 0
          if radiusIndex>0 then
            local rr=ChainRadiusRow(radiusIndex)
            local radius=rr and (tonumber(rr.radius) or 0) or 0
            if radius>0 then
              local score=420
              if effect>0 then score=score+60 end
              if applyAura>0 then score=score+90 end
              if targetA>0 then score=score+45 end
              if targetB>0 then score=score+20 end
              if node.rootKind=="AURA" then score=score+60 else score=score+20 end
              if node.depth>0 then score=score+25 end
              if trigger>0 then score=score+10 end
              score=score-node.depth*2
              local c={kind="STRUCT",radius=radius,spell=id,depth=node.depth,effect=i,score=score}
              table.insert(candidates,c)
            end
          end
          if trigger>0 and node.depth<8 and not visited[trigger] then
            table.insert(queue,{id=trigger,depth=node.depth+1,rootKind=node.rootKind})
          end
        end
      end
    end
  end

  -- Independent agreement is a strong signal: e.g. a triggered structured
  -- Radius=30 plus a tooltip that explicitly says radius 30.  Duplicate locale
  -- text from the same spell does not count as independent agreement.
  local j
  for i=1,table.getn(candidates) do
    local a=candidates[i]
    for j=1,table.getn(candidates) do
      if i~=j then
        local b=candidates[j]
        if math.abs((a.radius or 0)-(b.radius or 0))<0.05
          and (a.kind~=b.kind or a.spell~=b.spell or a.effect~=b.effect) then
          if a.kind=="STRUCT" then a.score=(a.score or 0)+240
          else a.score=(a.score or 0)+100 end
          break
        end
      end
    end
  end

  local best=nil
  for i=1,table.getn(candidates) do
    if ChainCandidateBetter(candidates[i],best) then best=candidates[i] end
  end
  if not best then
    return nil,"CHAIN_NO_RADIUS"
  end

  return {radius=best.radius,spell=best.spell},nil
end

ResolveTotemRange=function(createdSpell,auraCsv)
  local cacheKey=tostring(createdSpell or 0)..":"..tostring(auraCsv or "")
  local cached=totemRangeCache[cacheKey]
  if cached~=nil then
    if cached==false then return nil,nil end
    return cached.radius,cached.spell
  end

  local chain=ResolveTotemSpellChain(createdSpell,auraCsv)
  if chain and chain.radius and chain.radius>0 then
    local result={radius=chain.radius,spell=chain.spell}
    totemRangeCache[cacheKey]=result
    return result.radius,result.spell
  end

  -- Production policy: never fall back to the retired pre-R6.3 totem resolver.
  -- If the live Spell chain cannot prove a radius, do not draw a guessed circle.
  totemRangeCache[cacheKey]=false
  return nil,nil
end

local function TotemInfo(spell)
  spell=tonumber(spell) or 0
  if spell<=0 then return nil end
  local cached=totemDbcCache[spell]
  if cached~=nil then return cached or nil end
  if type(TaiYangShenDian)~="function" then return nil end

  local ok,raw=pcall(TaiYangShenDian,"AutoRange.TotemInfo",spell)
  if not ok or type(raw)~="string" then
    return nil
  end
  local a=Split(raw)
  if a[1]=="T" and tonumber(a[2])==1 then
    local effectType=tonumber(a[6]) or 0
    local info={
      castSpell=tonumber(a[3]) or spell,
      summonSpell=tonumber(a[4]) or spell,
      effectType=effectType,
      element=SF.TotemElementFromEffect(effectType),
      entry=tonumber(a[7]) or 0,
    }
    totemDbcCache[spell]=info
    return info
  end
  if a[1]=="T" and tonumber(a[2])==0 then totemDbcCache[spell]=false end
  return nil
end

local function TotemScopeAllowed(scope)
  return ScopeAllowedForMode(scope,totemMode)
end

local function ReadTotemSnapshot(modeOverride)
  if type(TaiYangShenDian)~="function" or totemMode=="OFF" then return nil,false end
  local scanMode=string.upper(tostring(modeOverride or totemMode))
  local ok,raw=pcall(TaiYangShenDian,"GroundProbe.TotemSnapshot",scanMode,scanRange)
  if not ok or type(raw)~="string" then return nil,false end
  local rows={}
  local valid=false
  Lines(raw,function(line)
    local a=Split(line)
    if a[1]=="T" then
      local guidRaw=tostring(a[2] or "")
      local guid=NormGuid(guidRaw)
      if guid~="0" then
        table.insert(rows,{
          guid=guid,guidRaw=guidRaw,
          entry=tonumber(a[3]) or 0,
          owner=a[4] or "0",
          scope=a[5] or "UNKNOWN",
          createdSpell=tonumber(a[6]) or 0,
          x=tonumber(a[7]), y=tonumber(a[8]), z=tonumber(a[9]),
          xy=tonumber(a[10]) or 9999,
          auraCsv=a[12] or "",
        })
      end
    elseif a[1]=="S" then
      valid=true
    end
  end)
  return rows,valid
end

local function BindTotemRow(row,discover)
  if not row or not row.guid then return false end
  if hazards["T:"..row.guid] then return true end
  if not row.x or not row.y or not row.z then return false end

  -- Highest priority: explicit user custom rule by localized totem name.
  -- If no custom rule matches, preserve Stage2.28 Searing special-case and the
  -- existing dynamic Spell-chain resolver for all other totems.
  local customRule,customActualName=SF.TotemCustomRuleForSpell(row.createdSpell)
  local radius,rangeSpell
  if customRule then
    radius=tonumber(customRule.radius)
    rangeSpell=row.createdSpell
  else
    radius,rangeSpell=ResolveSearingTotemRange(row.createdSpell,row.scope)
    if not radius then radius,rangeSpell=ResolveTotemRange(row.createdSpell,row.auraCsv) end
  end
  if not radius or radius<=0 then return false end

  local now=GetTime()
  local owner=discover and discover.ownerRaw or row.owner
  local totemInfo=(discover and discover.info) or TotemInfo(row.createdSpell)
  local totemElement=(totemInfo and totemInfo.element) or "OTHER"
  local key="T:"..row.guid
  local h={
    key=key,caster=owner,spell=rangeSpell or row.createdSpell,
    geomSpell=rangeSpell or row.createdSpell,started=now,radius=radius,
    modeHint="TOTEM",modeNow="TOTEM",totemOnly=true,sourceType="TOTEM",
    totemGuid=row.guid,ownerScope=row.scope,x=row.x,y=row.y,z=row.z,
    totemGuidRaw=row.guidRaw or row.guid,totemMissCount=0,
    totemElement=totemElement,totemEffectType=(totemInfo and totemInfo.effectType) or 0,
    createdSpell=row.createdSpell,originalTotemSpell=row.createdSpell,totemAuraCsv=row.auraCsv or "",
    totemCustomStyle=customRule and SF.ValidTotemVisualStyle(customRule.style,"AUTO") or nil,
    totemCustomName=customRule and customRule.name or nil,
  }
  RegisterHazard(h)
  RefreshHazardRuntimeClass(h)
  SetVisual(h)
  if type(Render)=="function" then Render() end
  return true
end

local function MatchTotemRow(discover,rows)
  local best=nil
  local bestScore=-1
  local i
  for i=1,table.getn(rows) do
    local row=rows[i]
    local key="T:"..row.guid
    if not hazards[key] then
      local ownerMatch=(NormGuid(row.owner)==discover.owner)
      local entryMatch=(discover.info.entry>0 and row.entry==discover.info.entry)
      local createdMatch=(row.createdSpell==discover.info.summonSpell or row.createdSpell==discover.info.castSpell)
      local identityMatch=(entryMatch or createdMatch)
      local selfPending=(discover.scope=="SELF")
      local ownerNotReady=(NormGuid(row.owner)=="0" or string.upper(tostring(row.scope or "UNKNOWN"))=="UNKNOWN")
      local provisional=(selfPending and ownerNotReady and identityMatch and (row.xy or 9999)<=12)
      if identityMatch and (ownerMatch or provisional) then
        local score=10
        if ownerMatch then score=score+100 else score=score+20 end
        if entryMatch then score=score+8 end
        if row.createdSpell==discover.info.summonSpell then score=score+10
        elseif row.createdSpell==discover.info.castSpell then score=score+8 end
        if provisional then score=score-math.min(row.xy or 0,12) end
        if score>bestScore then best=row; bestScore=score end
      end
    end
  end
  return best
end

local function StartTotemDiscovery(caster,spell)
  if not running or totemMode=="OFF" then return false end
  local info=TotemInfo(spell)
  if not info then return false end

  -- B3.7R1 behavior: owner matching uses the caster GUID delivered by TYS cast events
  -- directly. ScopeByGuid is only for scope classification, never for rewriting the
  -- owner identity used to match UNIT_FIELD_CREATEDBY/SUMMONEDBY.
  local scope=GroupScope(caster)
  if not TotemScopeAllowed(scope) then
    return true
  end

  local now=GetTime()
  local owner=NormGuid(caster)
  local i
  for i=1,table.getn(pendingTotems) do
    local p=pendingTotems[i]
    if p.owner==owner and p.info and p.info.castSpell==info.castSpell then
      p.deadline=now+2.50
      p.hardDeadline=now+3.00
      if (p.nextScan or now)>now then p.nextScan=now end
      return true
    end
  end
  local d={
    owner=owner,ownerRaw=tostring(caster or ""),scope=scope,info=info,
    started=now,deadline=now+2.50,hardDeadline=now+3.00,nextScan=now,
  }
  table.insert(pendingTotems,d)
  if ApplyRuntimeHooks then ApplyRuntimeHooks() end
  return true
end

local function DiscoverPendingTotems()
  if not running or totemMode=="OFF" or table.getn(pendingTotems)==0 then return end
  local beforeCount=table.getn(pendingTotems)
  local now=GetTime()
  local ready=false
  local i

  -- Absolute 3s safety cap. The normal 2.5s window now also covers valid snapshots
  -- where the new totem row exists but owner/identity fields are not ready yet.
  for i=table.getn(pendingTotems),1,-1 do
    local d=pendingTotems[i]
    if now>(d.hardDeadline or (d.started or now)+3.00) then
      table.remove(pendingTotems,i)
    end
  end
  if table.getn(pendingTotems)==0 then
    if ApplyRuntimeHooks then ApplyRuntimeHooks() end
    return
  end

  for i=1,table.getn(pendingTotems) do
    if now>=(pendingTotems[i].nextScan or now) then ready=true; break end
  end
  if not ready then return end

  -- Restore B3.7R1: an invalid snapshot does not add a 0.20s penalty. The next
  -- normal discovery tick may try again. This is still bounded by hardDeadline.
  -- During pending discovery use ALL so a just-created totem whose owner fields
  -- are not ready yet is not discarded by the native SELF/PARTY pre-filter.
  -- This is a short post-cast window only; bootstrap and normal source filtering
  -- still use the configured totemMode.
  local rows,valid=ReadTotemSnapshot("ALL")
  if not valid then return end

  for i=table.getn(pendingTotems),1,-1 do
    local d=pendingTotems[i]
    if now>d.deadline then
      table.remove(pendingTotems,i)
    elseif now>=(d.nextScan or now) then
      local row=MatchTotemRow(d,rows)
      if row then
        local bound=BindTotemRow(row,d)
        if bound then table.remove(pendingTotems,i)
        else d.nextScan=now+0.12 end
      else
        d.nextScan=now+0.12
      end
    end
  end
  if table.getn(pendingTotems)~=beforeCount and ApplyRuntimeHooks then ApplyRuntimeHooks() end
end

local function BootstrapTotems()
  if not running or totemMode=="OFF" then return true end
  local rows,valid=ReadTotemSnapshot()
  if not valid then return false end
  local i
  for i=1,table.getn(rows) do
    local row=rows[i]
    if TotemScopeAllowed(row.scope) and not hazards["T:"..row.guid] then
      BindTotemRow(row,nil)
    end
  end
  return true
end

TotemGuidAlive=function(guid,guidRaw)
  local state=SF.UnitStateByGuid(tostring(guidRaw or guid or ""))
  if not state or not state.visible then return false end
  if state.deadKnown and state.dead then return false end
  return true
end

PurgeStaleHiddenHazards=function(excludeKey,sourceA,sourceB)
  -- Hidden hazards do not run OnUpdate. Reclaim them only on low-frequency user
  -- actions or native slot pressure so OFF sources remain truly idle.
  local now=GetTime()
  local remove={}
  local casterStateCache={}
  local i
  for i=1,table.getn(order) do
    local h=hazards[order[i]]
    if h and h.key~=excludeKey and h.visualHidden
        and (not sourceA or h.sourceType==sourceA or (sourceB and h.sourceType==sourceB)) then
      local stale=false
      -- A configured SELF aura is owned by aura lifetime, not by enemy/dynamic
      -- cleanup.  Source toggles must never reclaim it while the aura is active.
      if h.sourceType=="SELF" and h.selfAuraHeld then
        stale=false
      elseif h.totemOnly then
        if TotemGuidAlive(h.totemGuid,h.totemGuidRaw) then
          h.hiddenMissCount=0; h.hiddenMissSince=nil; h.totemMissCount=0
        else
          h.hiddenMissCount=(h.hiddenMissCount or 0)+1
          if not h.hiddenMissSince then h.hiddenMissSince=now end
          if h.hiddenMissCount>=3 and now-h.hiddenMissSince>=0.75 then stale=true end
        end
      elseif h.dynamicGuid then
        h.dynamicState=h.dynamicState or {}
        local d=DynamicByGuid(h.dynamicGuidRaw or h.dynamicGuid,h.dynamicState)
        if d then
          h.hiddenMissCount=0; h.hiddenMissSince=nil; h.dynamicMissCount=0
          h.x=d.x; h.y=d.y; h.z=d.z
          if (not h.manualRadius or h.manualRadius<=0) and d.radius and d.radius>0 then h.radius=d.radius end
          h.lastDynamic=now
        else
          h.hiddenMissCount=(h.hiddenMissCount or 0)+1
          if not h.hiddenMissSince then h.hiddenMissSince=now end
          if h.hiddenMissCount>=2 and now-h.hiddenMissSince>=0.35 then stale=true end
        end
      elseif h.modeHint=="CASTER" and IsEnemySource(h) then
        if now>(h.expires or now) then
          stale=true
        else
          local state=QueryCasterState(h,casterStateCache,true)
          if state and state.dead then
            stale=true
          elseif state and state.visible then
            h.x=state.x; h.y=state.y; h.z=state.z
          elseif state and state.misses>=2 then
            stale=true
          end
        end
      elseif h.modeHint=="CASTER" then
        stale=(not (h.sourceType=="SELF" and h.selfAuraHeld)) and now>(h.expires or now)
      else
        stale=now>(h.expires or now) and (not h.lastDynamic or now-h.lastDynamic>0.35)
      end
      if stale then table.insert(remove,h.key) end
    end
  end
  for i=1,table.getn(remove) do RemoveHazard(remove[i]) end
  return table.getn(remove)
end

local function CheckTotemLifetimes()
  if not running or not visualEnabled or totemMode=="OFF" then return end
  local now=GetTime()
  local remove={}
  local i
  for i=1,table.getn(activeTotemKeys) do
    local h=hazards[activeTotemKeys[i]]
    if h then
      local guid=h.totemGuid or string.gsub(h.key,"^T:","")
      if TotemGuidAlive(guid,h.totemGuidRaw) then
        h.totemMissCount=0
        if not h.visualCreated
            or (h.visualBornAt and now-h.visualBornAt>=visualMaxAge
                and now>=(h.nextVisualRestartRetry or 0)) then SetVisual(h) end
      else
        h.totemMissCount=(h.totemMissCount or 0)+1
        if h.totemMissCount>=2 then table.insert(remove,h.key) end
      end
    end
  end
  for i=1,table.getn(remove) do RemoveHazard(remove[i]) end
  if table.getn(remove)>0 and ApplyRuntimeHooks then ApplyRuntimeHooks() end
end

local function RefreshSourceVisibility(sourceA,sourceB)
  if PurgeStaleHiddenHazards then PurgeStaleHiddenHazards(nil,sourceA,sourceB) end
  local now=GetTime()
  local remove={}
  local casterStateCache={}
  local i
  for i=1,table.getn(order) do
    local h=hazards[order[i]]
    if h and (not sourceA or h.sourceType==sourceA or (sourceB and h.sourceType==sourceB)) then
      if HazardFilterVisible(h) then
        if h.totemOnly then
          if TotemGuidAlive(h.totemGuid,h.totemGuidRaw) then
            h.totemMissCount=0
            SetVisual(h)
          else
            h.totemMissCount=(h.totemMissCount or 0)+1
            HideVisual(h)
            if h.totemMissCount>=2 then table.insert(remove,h.key) end
          end
        elseif h.modeHint=="CASTER" and IsEnemySource(h) then
          if now>(h.expires or now) then
            table.insert(remove,h.key)
          else
            local state=QueryCasterState(h,casterStateCache,true)
            if state and state.dead then
              HideVisual(h)
              table.insert(remove,h.key)
            elseif state and state.visible then
              h.modeNow="CASTER"; h.x=state.x; h.y=state.y; h.z=state.z
                    SetVisual(h)
            else
                    HideVisual(h)
              if state and state.misses>=2 then table.insert(remove,h.key) end
            end
          end
        elseif h.modeHint=="CASTER" and h.sourceType=="SELF" then
          if (not h.selfAuraHeld) and now>(h.expires or now) then
            table.insert(remove,h.key)
          else
            local state=QueryCasterState(h,casterStateCache,false)
            if state and state.visible then
              h.modeNow="CASTER"; h.x=state.x; h.y=state.y; h.z=state.z; SetVisual(h)
            else
              -- Buff lifetime owns SELF. Keep the existing visual on a transient
              -- position miss instead of hiding/removing it like an enemy caster.
              if h.visualCreated and h.visualHidden then ShowVisual(h) end
            end
          end
        elseif h.modeHint=="CASTER" then
          if now>(h.expires or now) then
            table.insert(remove,h.key)
          else
            local state=QueryCasterState(h,casterStateCache,false)
            if state and state.visible then
              h.modeNow="CASTER"; h.x=state.x; h.y=state.y; h.z=state.z; SetVisual(h)
            else
              HideVisual(h)
              if state and state.misses>=2 then table.insert(remove,h.key) end
            end
          end
        elseif h.dynamicGuid then
          h.dynamicState=h.dynamicState or {}
          local d=DynamicByGuid(h.dynamicGuidRaw or h.dynamicGuid,h.dynamicState)
          if d then
            h.dynamicMissCount=0
            h.modeNow="DYNAMIC"
            h.x=d.x; h.y=d.y; h.z=d.z
            if (not h.manualRadius or h.manualRadius<=0) and d.radius and d.radius>0 then h.radius=d.radius end
            h.lastDynamic=now
            if (h.expires or 0)<now+0.50 then h.expires=now+0.50 end
            SetVisual(h)
          elseif now>(h.expires or now) and (not h.lastDynamic or now-h.lastDynamic>0.35) then
            table.insert(remove,h.key)
          else
            SetVisual(h)
          end
        elseif now>(h.expires or now) and (not h.lastDynamic or now-h.lastDynamic>0.35) then
          table.insert(remove,h.key)
        else
          SetVisual(h)
        end
      else
        HideVisual(h)
      end
    end
  end
  for i=1,table.getn(remove) do RemoveHazard(remove[i]) end
  RebuildRuntimeBuckets()
end

local totemModes={"SELF","PARTY","PARTY_ONLY","RAID","ALL"}
local totemModeText={
  OFF="关闭",
  SELF="仅自己",
  PARTY="小队(含自己)",
  PARTY_ONLY="仅小队队友",
  RAID="团队",
  ALL="全部",
}

local function SetEnemyCircle(on,announce)
  enemyCircle=on and true or false
  AutoRangeDB.enemyCircle=enemyCircle
  Warn.dirty=true
  if not enemyCircle then Warn.SetState("NONE","SINGLE") end
  RefreshSourceVisibility("ENEMY","UNKNOWN")
  if ApplyRuntimeHooks then ApplyRuntimeHooks() end
  if announce then Chat("敌人圈："..(enemyCircle and "开启" or "关闭")) end
end

function SF.ApplyRuleToHazard(h,rule)
  if not h then return end
  local oldStyle=h.ruleStyle
  h.ruleStyle=rule and SF.ValidStyle(rule.style,(h.sourceType=="SELF" and "WHITE" or "STANDARD")) or nil
  h.manualRadius=rule and math.max(0,tonumber(rule.radius) or 0) or 0
  local r=Resolve(h.spell)
  if h.manualRadius>0 then
    h.radius=h.manualRadius
    if not r and h.modeHint=="UNKNOWN" then h.modeHint="CASTER"; h.modeNow="CASTER" end
  elseif r and r.radius and r.radius>0 then
    h.radius=r.radius; h.geomSpell=r.geomSpell; h.modeHint=r.mode; h.centerFix=r.centerFix
    if h.modeNow~="DYNAMIC" then h.modeNow=r.mode end
  end
  if oldStyle~=h.ruleStyle then ClearVisual(h) end
  RefreshHazardRuntimeClass(h)
end

function SF.ApplyEnemyLive()
  local remove={}
  local i
  for i=1,table.getn(order) do
    local h=hazards[order[i]]
    if h and (h.sourceType=="ENEMY" or h.sourceType=="UNKNOWN") then
      if not SF.EnemyAllowed(h.spell) then table.insert(remove,h.key)
      else SF.ApplyRuleToHazard(h,SF.RuleForSource(h.sourceType,h.spell)) end
    elseif h and h.sourceType=="FRIEND" and not SF.AutoSemanticAllowed(h.spell) then
      table.insert(remove,h.key)
    end
  end
  for i=1,table.getn(remove) do RemoveHazard(remove[i]) end
  if ApplyRuntimeHooks then ApplyRuntimeHooks() end
end

function SF.ApplySelfLive()
  local remove={}
  local i,rule
  for i=1,table.getn(order) do
    local h=hazards[order[i]]
    if h and h.sourceType=="SELF" then
      rule=SF.RuleForSource("SELF",h.spell)
      if not SF.selfEnabled or not rule then table.insert(remove,h.key)
      else SF.ApplyRuleToHazard(h,rule) end
    end
  end
  for i=1,table.getn(remove) do RemoveHazard(remove[i]) end
  if ApplyRuntimeHooks then ApplyRuntimeHooks() end
end

function SF.SetEnemyMode(mode,announce)
  mode=string.upper(tostring(mode or "ALL"))
  if mode~="ALL" and mode~="ONLY" and mode~="EXCLUDE" then mode="ALL" end
  SF.enemyMode=mode; AutoRangeDB.enemySkillMode=mode
  SF.ApplyEnemyLive()
  if announce then
    local text=(mode=="ONLY" and "仅指定") or (mode=="EXCLUDE" and "排除指定") or "全部技能"
    Chat("敌人技能模式："..text)
  end
  return mode
end

function SF.RecountRules(kind)
  local rules=(kind=="SELF") and SF.selfRules or SF.enemyRules
  local count=0
  local id,rule
  for id,rule in pairs(rules) do if type(rule)=="table" then count=count+1 end end
  if kind=="SELF" then SF.selfCount=count; SF.selfActiveCount=count; SF.selfIds=nil
  else SF.enemyCount=count; SF.enemyActiveCount=count; SF.enemyIds=nil end
end

function SF.SaveRules()
  AutoRangeDB.enemySkillRules=SF.enemyRules
  AutoRangeDB.selfSkillRules=SF.selfRules
  AutoRangeDB.enemySkills=nil
  AutoRangeDB.selfSkills=nil
end

function SF.UpsertRules(kind,text,radius,style,announce)
  local rules=(kind=="SELF") and SF.selfRules or SF.enemyRules
  local defaultStyle=(kind=="SELF") and "WHITE" or "STANDARD"
  radius=math.max(0,tonumber(radius) or 0)
  style=SF.ValidStyle(style,defaultStyle)
  local touched=0
  SF.ForEachInput(text,function(id)
    local old=rules[id]
    local name=(old and old.name) or ""
    if name=="" then name=SF.GetSpellName(id) end
    rules[id]={radius=radius,style=style,name=name,
      decision=(kind=="ENEMY" and SF.ValidDecision(SF.enemyEditDecision) or "AUTO"),
      mode=(kind=="ENEMY" and SF.ValidRuleMode(SF.enemyEditMode) or "NORMAL")}
    touched=touched+1
  end)
  SF.RecountRules(kind); SF.SaveRules()
  if kind=="SELF" then SF.ApplySelfLive(); SF.RefreshSelfAuraHazards(); if RestoreActiveSelfAuraHazards then RestoreActiveSelfAuraHazards() end else SF.ApplyEnemyLive() end
  if announce then Chat((kind=="SELF" and "自己的技能规则：添加/更新 " or "敌人技能规则：添加/更新 ")..tostring(touched).." 条") end
  return touched
end

function SF.RemoveRules(kind,text,announce)
  local rules=(kind=="SELF") and SF.selfRules or SF.enemyRules
  local removed=0
  SF.ForEachInput(text,function(id) if rules[id] then rules[id]=nil; removed=removed+1 end end)
  SF.RecountRules(kind); SF.SaveRules()
  if kind=="SELF" then SF.ApplySelfLive() else SF.ApplyEnemyLive() end
  if announce then Chat((kind=="SELF" and "自己的技能规则：删除 " or "敌人技能规则：删除 ")..tostring(removed).." 条") end
  return removed
end

function SF.ClearRules(kind,announce)
  if kind=="SELF" then SF.selfRules={}; SF.selfCount=0; SF.selfActiveCount=0; SF.selfPage=1; SF.selfIds=nil; SF.selectedSelfRule=nil; SF.ApplySelfLive()
  else SF.enemyRules={}; SF.enemyCount=0; SF.enemyActiveCount=0; SF.enemyPage=1; SF.enemyIds=nil; SF.selectedEnemyRule=nil; SF.ApplyEnemyLive() end
  SF.SaveRules()
  if announce then Chat((kind=="SELF" and "自己的技能规则：已清空" or "敌人技能规则：已清空")) end
end

function SF.LoadRule(kind,text,announce)
  local _,_,token=string.find(tostring(text or ""),"(%d+)")
  local id=tonumber(token)
  if not id then if announce then Chat("请输入 SpellID") end; return nil end
  id=math.floor(id)
  local rules=(kind=="SELF") and SF.selfRules or SF.enemyRules
  local rule=rules[id]
  if not rule then if announce then Chat("没有这个 SpellID 的规则") end; return nil end
  if rule.name=="" then rule.name=SF.GetSpellName(id); SF.SaveRules() end
  if kind=="SELF" then
    SF.selectedSelfRule=id
    if SF.ui.selfEdit then SF.ui.selfEdit:SetText(tostring(id)) end
    SF.selfEditStyle=SF.ValidStyle(rule.style,"WHITE")
    if SF.ui.selfRadius then SF.ui.selfRadius:SetText(tostring(rule.radius or 0)) end
  else
    SF.selectedEnemyRule=id
    if SF.ui.enemyEdit then SF.ui.enemyEdit:SetText(tostring(id)) end
    SF.enemyEditStyle=SF.ValidStyle(rule.style,"STANDARD")
    SF.enemyEditDecision=SF.ValidDecision(rule.decision)
    SF.enemyEditMode=SF.ValidRuleMode(rule.mode)
    if SF.ui.enemyRadius then SF.ui.enemyRadius:SetText(tostring(rule.radius or 0)) end
  end
  if announce then Chat("已载入 "..tostring(id).." "..tostring(rule.name or "")) end
  return rule
end

function SF.SetSelfEnabled(on,announce)
  SF.selfEnabled=on and true or false; AutoRangeDB.selfCircleEnabled=SF.selfEnabled
  if SF.selfEnabled and not SF.playerGuid then SF.playerGuidTried=false; SF.RefreshPlayerGuid() end
  SF.ApplySelfLive()
  if SF.selfEnabled then
    SF.RefreshSelfAuraHazards()
    if RestoreActiveSelfAuraHazards then RestoreActiveSelfAuraHazards() end
  end
  if ApplyRuntimeHooks then ApplyRuntimeHooks() end
  if announce then Chat("自己的技能圈："..(SF.selfEnabled and "开启" or "关闭")) end
end

SF.enemyDecisionOrder={"AUTO","SHOW","HIDE"}
function SF.CycleEnemyDecision()
  local cur=SF.ValidDecision(SF.enemyEditDecision)
  local i
  for i=1,table.getn(SF.enemyDecisionOrder) do
    if SF.enemyDecisionOrder[i]==cur then
      local n=i+1
      if n>table.getn(SF.enemyDecisionOrder) then n=1 end
      SF.enemyEditDecision=SF.enemyDecisionOrder[n]
      return SF.enemyEditDecision
    end
  end
  SF.enemyEditDecision="AUTO"
  return SF.enemyEditDecision
end

function SF.SetEnemyEditMode(mode)
  SF.enemyEditMode=SF.ValidRuleMode(mode)
  return SF.enemyEditMode
end

function SF.SetSemanticFilter(on,announce)
  SF.semanticFilterEnabled=on and true or false
  AutoRangeDB.semanticFilterEnabled=SF.semanticFilterEnabled
  SF.ApplyEnemyLive()
  if announce then Chat("危险语义过滤："..(SF.semanticFilterEnabled and "开启" or "关闭")) end
  return SF.semanticFilterEnabled
end

SF.enemyModeOrder={"ALL","ONLY","EXCLUDE"}
function SF.CycleEnemyMode()
  local i
  for i=1,table.getn(SF.enemyModeOrder) do
    if SF.enemyModeOrder[i]==SF.enemyMode then local n=i+1; if n>table.getn(SF.enemyModeOrder) then n=1 end; return SF.SetEnemyMode(SF.enemyModeOrder[n],true) end
  end
  return SF.SetEnemyMode("ALL",true)
end

SF.ruleStyleOrder={"STANDARD","WHITE","FLAME","JAGGED","JAGGEDG"}

function SF.RulePageSize(kind)
  if kind=="ENEMY" then return math.max(1,math.floor(tonumber(SF.enemyPageSize) or 4)) end
  if kind=="SELF" then return math.max(1,math.floor(tonumber(SF.selfPageSize) or 8)) end
  return math.max(1,math.floor(tonumber(SF.pageSize) or 4))
end

function SF.CycleEditStyle(kind)
  local cur=(kind=="SELF") and SF.selfEditStyle or SF.enemyEditStyle
  local list=(M2 and M2.styleOrder and table.getn(M2.styleOrder)>0) and M2.styleOrder or SF.ruleStyleOrder
  local i,nextStyle
  for i=1,table.getn(list) do
    if list[i]==cur then local n=i+1; if n>table.getn(list) then n=1 end; nextStyle=list[n]; break end
  end
  nextStyle=nextStyle or ((kind=="SELF") and "WHITE" or "STANDARD")
  if kind=="SELF" then SF.selfEditStyle=nextStyle else SF.enemyEditStyle=nextStyle end
  return nextStyle
end

function SF.CyclePage(kind,delta)
  delta=tonumber(delta) or 0
  local count=(kind=="SELF") and SF.selfCount or SF.enemyCount
  local pages=math.max(1,math.ceil((tonumber(count) or 0)/SF.RulePageSize(kind)))
  local page=(kind=="SELF") and SF.selfPage or SF.enemyPage
  page=page+delta; if page<1 then page=pages elseif page>pages then page=1 end
  if kind=="SELF" then SF.selfPage=page else SF.enemyPage=page end
end

local function SetEnemyLimitMax(value,announce)
  local n=math.floor(tonumber(value) or enemyLimitMax or 20)
  if n<1 then n=1 elseif n>9999 then n=9999 end
  enemyLimitMax=n
  AutoRangeDB.enemyLimitMax=n
  if enemyLimitEnabled and EnforceEnemyLimit then EnforceEnemyLimit() end
  if ApplyRuntimeHooks then ApplyRuntimeHooks() end
  if announce then Chat("最大可见敌人圈："..tostring(n)) end
  return n
end

function SF.SetRefreshInterval(value,announce)
  local n=tonumber(value) or SF.refreshInterval or 0.20
  if n<0.02 then n=0.02 elseif n>1.00 then n=1.00 end
  n=math.floor(n*100+0.5)/100
  SF.refreshInterval=n
  AutoRangeDB.refreshInterval=n
  elapsed=0
  if announce then Chat("危险圈刷新间隔："..string.format("%.2f",n).." 秒（约 "..string.format("%.1f",1/n).." 次/秒）") end
  return n
end

local function SetEnemyLimitEnabled(on,announce)
  enemyLimitEnabled=on and true or false
  AutoRangeDB.enemyLimitEnabled=enemyLimitEnabled
  if enemyLimitEnabled then
    RebuildRuntimeBuckets()
    if EnforceEnemyLimit then EnforceEnemyLimit() end
  else
    ResetEnemyLimitTracking()
  end
  if ApplyRuntimeHooks then ApplyRuntimeHooks() end
  if announce then Chat("敌人圈性能保护："..(enemyLimitEnabled and "开启" or "关闭")) end
end

local function SetFriendCircle(on,announce)
  friendCircle=on and true or false
  AutoRangeDB.friendCircle=friendCircle
  RefreshSourceVisibility("FRIEND")
  if ApplyRuntimeHooks then ApplyRuntimeHooks() end
  if announce then Chat("队友圈："..(friendCircle and "开启" or "关闭")) end
end

local function SetTotemMode(mode,announce)
  mode=string.upper(tostring(mode or "OFF"))
  if mode=="PARTYONLY" then mode="PARTY_ONLY" end
  local valid=(mode=="OFF" or mode=="SELF" or mode=="PARTY" or mode=="PARTY_ONLY" or mode=="RAID" or mode=="ALL")
  if not valid then return false end
  totemMode=mode
  AutoRangeDB.totemMode=mode
  if mode~="OFF" then
    totemLastMode=mode; AutoRangeDB.totemLastMode=mode
    local i
    for i=table.getn(pendingTotems),1,-1 do
      if not TotemScopeAllowed(pendingTotems[i].scope) then table.remove(pendingTotems,i) end
    end
    if running then totemBootstrapAt=GetTime()+0.05; totemBootstrapRetries=0 end
  else
    pendingTotems={}
    totemBootstrapAt=nil
  end
  RefreshSourceVisibility("TOTEM")
  if ApplyRuntimeHooks then ApplyRuntimeHooks() end
  if announce then Chat("图腾圈来源："..tostring(totemModeText[mode] or mode)) end
  return true
end

function SF.RefreshTotemStyleVisuals(element)
  local i
  for i=1,table.getn(order) do
    local h=hazards[order[i]]
    if h and h.totemOnly and (not element or (h.totemElement or "OTHER")==element) then
      ClearVisual(h)
      SetVisual(h)
    end
  end
end

local function SetTotemVisualStyle(style,announce)
  style=SF.ValidTotemVisualStyle(style,"AUTO")
  if style==totemVisualStyle then
    if announce then Chat("图腾统一M2样式："..TotemVisualStyleLabel(style)) end
    return true
  end
  totemVisualStyle=style
  AutoRangeDB.totemVisualStyle=style
  if SF.totemUnifiedStyle then SF.RefreshTotemStyleVisuals(nil) end
  if announce then Chat("图腾统一M2样式："..TotemVisualStyleLabel(style)) end
  return true
end

function SF.TotemStyleCycleList()
  local list={"AUTO"}
  local src=(M2 and M2.styleOrder and table.getn(M2.styleOrder)>0) and M2.styleOrder or SF.ruleStyleOrder
  local j
  for j=1,table.getn(src) do list[table.getn(list)+1]=src[j] end
  return list
end

function SF.NextTotemVisualStyle(current)
  current=SF.ValidTotemVisualStyle(current,"AUTO")
  local list=SF.TotemStyleCycleList()
  local i
  for i=1,table.getn(list) do
    if list[i]==current then
      local n=i+1
      if n>table.getn(list) then n=1 end
      return list[n]
    end
  end
  return "AUTO"
end

local function CycleTotemVisualStyle()
  SetTotemVisualStyle(SF.NextTotemVisualStyle(totemVisualStyle),true)
end

function SF.SetTotemUnifiedStyle(on,announce)
  on=on and true or false
  if SF.totemUnifiedStyle==on then return true end
  SF.totemUnifiedStyle=on
  AutoRangeDB.totemUnifiedStyle=on
  SF.RefreshTotemStyleVisuals(nil)
  if announce then Chat("图腾全部一个颜色："..(on and "开启" or "关闭")) end
  return true
end

function SF.SetTotemCategoryStyle(element,style,announce)
  element=string.upper(tostring(element or "OTHER"))
  if not SF.totemCategoryStyles[element] then element="OTHER" end
  style=SF.ValidTotemVisualStyle(style,"AUTO")
  if SF.totemCategoryStyles[element]==style then return true end
  SF.totemCategoryStyles[element]=style
  AutoRangeDB.totemCategoryStyles=SF.totemCategoryStyles
  if not SF.totemUnifiedStyle then SF.RefreshTotemStyleVisuals(element) end
  if announce then Chat(tostring(SF.totemElementText[element] or element).."图腾样式："..TotemVisualStyleLabel(style)) end
  return true
end

function SF.CycleTotemCategoryStyle(element)
  element=string.upper(tostring(element or "OTHER"))
  if not SF.totemCategoryStyles[element] then element="OTHER" end
  SF.SetTotemCategoryStyle(element,SF.NextTotemVisualStyle(SF.totemCategoryStyles[element]),true)
end

function SF.CycleTotemCustomEditStyle()
  SF.totemCustomEditStyle=SF.NextTotemVisualStyle(SF.totemCustomEditStyle)
  if SF.ui.totemCustomStyleBtn then SF.ui.totemCustomStyleBtn:SetText("样式："..TotemVisualStyleLabel(SF.totemCustomEditStyle)) end
end

function SF.RenderTotemCustomPopup()
  if not SF.ui.totemCustomPopup then return end
  local ids=SF.RebuildTotemCustomIds()
  local pages=math.max(1,math.ceil(table.getn(ids)/(SF.totemCustomPageSize or 5)))
  if SF.totemCustomPage<1 then SF.totemCustomPage=1 elseif SF.totemCustomPage>pages then SF.totemCustomPage=pages end
  local first=(SF.totemCustomPage-1)*(SF.totemCustomPageSize or 5)+1
  local i,key,rule,row,idx
  for i=1,(SF.totemCustomPageSize or 5) do
    idx=first+i-1; key=ids[idx]; row=SF.ui.totemCustomRows and SF.ui.totemCustomRows[i]
    if row and key then
      rule=SF.totemCustomRules[key]
      row.key=key
      row.select:SetText(tostring(rule.name).."  "..tostring(rule.radius).."码  "..TotemVisualStyleLabel(rule.style))
      row.select:Show(); row.delete:Show()
    elseif row then
      row.key=nil; row.select:Hide(); row.delete:Hide()
    end
  end
  if SF.ui.totemCustomPageText then SF.ui.totemCustomPageText:SetText(tostring(SF.totemCustomPage).."/"..tostring(pages).." · 共"..tostring(table.getn(ids)).."条") end
end

function SF.OpenTotemCustomPopup()
  if not SF.ui.totemCustomPopup then return end
  if SF.ui.totemCustomPopup:IsShown() then SF.ui.totemCustomPopup:Hide(); return end
  SF.RenderTotemCustomPopup(); SF.ui.totemCustomPopup:Show()
end

function SF.SelectTotemCustomRule(key)
  local rule=SF.totemCustomRules[key]
  if not rule then return end
  if SF.ui.totemCustomName then SF.ui.totemCustomName:SetText(rule.name or "") end
  if SF.ui.totemCustomRadius then SF.ui.totemCustomRadius:SetText(tostring(rule.radius or "")) end
  SF.totemCustomEditStyle=SF.ValidTotemVisualStyle(rule.style,"AUTO")
  if SF.ui.totemCustomStyleBtn then SF.ui.totemCustomStyleBtn:SetText("样式："..TotemVisualStyleLabel(SF.totemCustomEditStyle)) end
end

local function CycleTotemMode()
  local current=totemMode
  if current=="OFF" then current=totemLastMode or "SELF" end
  local i
  for i=1,table.getn(totemModes) do
    if totemModes[i]==current then
      local nextIndex=i+1
      if nextIndex>table.getn(totemModes) then nextIndex=1 end
      SetTotemMode(totemModes[nextIndex],true)
      return
    end
  end
  SetTotemMode("SELF",true)
end

function M2.SaveLibrary()
  local out={}
  local i,id,v
  for i=1,table.getn(M2.customIds) do
    id=M2.customIds[i]; v=M2.customById[id]
    if v then out[id]={label=v.label,mpq=v.mpq,path=v.path,diameter=v.diameter} end
  end
  AutoRangeDB.customM2Library=out
  AutoRangeDB.customM2NextId=M2.nextId
end

function M2.RefreshStyledVisuals(id)
  local i,h
  for i=1,table.getn(order) do
    h=hazards[order[i]]
    if h and (h.ruleStyle==id or (h.totemOnly and SF.TotemStyleForHazard(h)==id)) then
      ClearVisual(h); SetVisual(h)
    end
  end
end

function M2.Basename(path)
  local s=M2.NormalizePath(path)
  local _,slash=string.find(s,".*\\")
  if slash then s=string.sub(s,slash+1) end
  s=string.gsub(s,"%.[mM][2]$","")
  s=string.gsub(s,"%.[mM][dD][xX]$","")
  if s=="" then s="M2素材" end
  return s
end

function M2.AutoDiameter(path)
  local _,_,w=string.find(tostring(path or ""),"_W([0-9]+)_")
  local n=tonumber(w)
  if n and n>0 then return n end
  return 10
end

function M2.SourceLabel(source)
  source=tostring(source or "")
  if source=="BUILTIN" then return "内置" end
  if source=="LOOSE" then return "散文件" end
  if source=="" then return "未分类" end
  return source
end

function M2.RecordKey(path,source)
  return string.lower(tostring(source or "")).."|"..string.lower(M2.NormalizePath(path))
end

function M2.FindCustomByPath(path)
  local needle=string.lower(M2.NormalizePath(path))
  local i,id,v
  for i=1,table.getn(M2.customIds) do
    id=M2.customIds[i]; v=M2.customById[id]
    if v and string.lower(M2.NormalizePath(v.path))==needle then return id,v end
  end
  return nil,nil
end

function M2.DeleteId(id,announce)
  local v=id and M2.customById[id] or nil
  if not v then if announce then Chat("M2素材：当前项目不在已添加素材库") end; return false end
  M2.customById[id]=nil
  local i
  for i=table.getn(M2.customIds),1,-1 do if M2.customIds[i]==id then table.remove(M2.customIds,i) end end
  for _,rule in pairs(SF.enemyRules) do if rule and rule.style==id then rule.style="STANDARD" end end
  for _,rule in pairs(SF.selfRules) do if rule and rule.style==id then rule.style="WHITE" end end
  if SF.enemyEditStyle==id then SF.enemyEditStyle="STANDARD" end
  if SF.selfEditStyle==id then SF.selfEditStyle="WHITE" end
  M2.RebuildStyleOrder(); M2.SaveLibrary(); SF.SaveRules()
  if totemVisualStyle==id then SetTotemVisualStyle("AUTO",false) end
  local element
  for element in pairs(SF.totemCategoryStyles) do
    if SF.totemCategoryStyles[element]==id then SF.totemCategoryStyles[element]="AUTO" end
  end
  AutoRangeDB.totemCategoryStyles=SF.totemCategoryStyles
  SF.RefreshTotemStyleVisuals(nil)
  SF.ApplyEnemyLive(); SF.ApplySelfLive()
  if M2.selectedRecord and M2.selectedRecord.customId==id then M2.selectedRecord=nil end
  if announce then Chat("M2素材：已删除 "..tostring(v.label or id)) end
  return true
end

local function ParsePlayerPosition(text)
  -- Current GroundProbe builds normally return P|x|y|z. Keep a conservative
  -- fallback for builds that append fields.
  text=tostring(text or "")
  if string.sub(text,1,2)=="U|" then return ParseUnitPosition(text) end
  if string.sub(text,1,2)~="P|" then return nil end
  local a=Split(text)
  local x=tonumber(a[2]); local y=tonumber(a[3]); local z=tonumber(a[4])
  if x and y then return x,y,z or 0 end
  local n=table.getn(a)
  if n>=4 then
    x=tonumber(a[n-2]); y=tonumber(a[n-1]); z=tonumber(a[n])
    if x and y then return x,y,z or 0 end
  end
  return nil
end

function M2.CurrentPlayerPosition()
  -- Preferred: dedicated player record.
  if type(TaiYangShenDian)=="function" then
    local ok,text=pcall(TaiYangShenDian,"GroundProbe.PlayerPosition")
    if ok and type(text)=="string" then
      local x,y,z=ParsePlayerPosition(text)
      if x and y then return x,y,z end
    end
    -- Fallback: query the player's GUID through the same UnitByGuid coordinate space.
    if not SF.playerGuid then SF.playerGuidTried=false; SF.RefreshPlayerGuid() end
    if SF.playerGuid then
      local ok2,text2=pcall(TaiYangShenDian,"GroundProbe.UnitByGuid",tostring(SF.playerGuid))
      if ok2 and type(text2)=="string" then
        local x2,y2,z2=ParseUnitPosition(text2)
        if x2 and y2 then return x2,y2,z2 end
      end
    end
  end
  return nil
end

Warn.mediaDir="Interface\\AddOns\\AutoRange\\Media\\"

local function WarnMediaName(v)
  local s=TrimText(v)
  s=string.gsub(s,"/","\\")
  local pos=1
  local q=string.find(s,"\\",pos,true)
  while q do pos=q+1; q=string.find(s,"\\",pos,true) end
  s=string.sub(s,pos)
  if string.lower(string.sub(s,-4))~=".tga" then return nil end
  return s
end

local function WarnAddMediaName(out,seen,v)
  local name=WarnMediaName(v)
  if not name or name=="" then return end
  local key=string.lower(name)
  if seen[key] then return end
  seen[key]=true
  out[table.getn(out)+1]=name
end

function Warn.RefreshMediaList()
  local previous=Warn.CurrentMediaName and Warn.CurrentMediaName() or ""
  if previous=="" then previous=WarnMediaName(AutoRangeDB and AutoRangeDB.warningMediaName or "") or "" end
  local out={}; local seen={}
  local scanOK=false; local scanStatus="TYS不可用"; Warn.mediaRawError=""
  if type(TaiYangShenDian)=="function" then
    local callOK,ok,count,status=pcall(TaiYangShenDian,"AutoRange.Media.Scan")
    if callOK then
      scanStatus=tostring(status or (ok and "OK" or "扫描失败"))
      if ok then
        scanOK=true
        local n=math.floor(tonumber(count) or 0); if n<0 then n=0 elseif n>1024 then n=1024 end
        local i
        for i=1,n do
          local getOK,found,name=pcall(TaiYangShenDian,"AutoRange.Media.Get",i)
          if getOK and found and type(name)=="string" then WarnAddMediaName(out,seen,name) end
        end
      end
    else
      Warn.mediaRawError=tostring(ok or "")
      if string.find(Warn.mediaRawError,"Unknown unit name",1,true) then
        scanStatus="当前加载DLL不支持Media.Scan；完全退出游戏后替换DLL"
      else
        scanStatus="Media.Scan调用失败："..Warn.mediaRawError
      end
    end
  end
  table.sort(out,function(a,b) return string.lower(a)<string.lower(b) end)
  Warn.mediaSource=scanOK and "DLL目录扫描" or ("扫描失败:"..scanStatus)

  if not scanOK then
    -- A transient DLL/directory failure must never erase the last valid TGA choice.
    Warn.mediaFiles={}
    Warn.mediaIndex=0
    Warn.mediaFallbackName=(previous~="") and previous or nil
    Warn.path=(previous~="") and (Warn.mediaDir..previous) or ""
    return 0
  end

  Warn.mediaFiles=out
  Warn.mediaFallbackName=nil
  local wanted=WarnMediaName(AutoRangeDB and AutoRangeDB.warningMediaName or "")
  if not wanted and previous~="" then wanted=previous end
  local index=1; local i
  if wanted then
    for i=1,table.getn(out) do
      if string.lower(out[i])==string.lower(wanted) then index=i; break end
    end
  elseif AutoRangeDB and tonumber(AutoRangeDB.warningMediaIndex) then
    index=math.floor(tonumber(AutoRangeDB.warningMediaIndex))
  end
  if table.getn(out)>0 then
    if index<1 then index=1 elseif index>table.getn(out) then index=1 end
    Warn.mediaIndex=index; Warn.path=Warn.mediaDir..out[index]
  else
    Warn.mediaIndex=0; Warn.path=""
  end
  return table.getn(out)
end

function Warn.CurrentMediaName()
  if Warn.mediaIndex and Warn.mediaIndex>0 then return Warn.mediaFiles[Warn.mediaIndex] or "" end
  return Warn.mediaFallbackName or ""
end

function Warn.NormalizeMode(v)
  local mode=string.upper(tostring(v or "SINGLE"))
  -- Stage2.15 used DIRECTION; Stage2.16 also had DIRECTION4. Both migrate to 2-direction.
  if mode=="DIRECTION" or mode=="DIRECTION4" then mode="DIRECTION2" end
  if mode~="SINGLE" and mode~="DIRECTION2" then mode="SINGLE" end
  return mode
end

function Warn.ModeText()
  if Warn.mode=="DIRECTION2" then return "2方向预警" end
  return "单图预警"
end

local function WarnDirectionLabel(slot)
  if slot=="LEFT" then return "左" end
  if slot=="RIGHT" then return "右" end
  return "?"
end

local function WarnShortMediaName(name,maxLen)
  local s=tostring(name or "")
  if s=="" then return "未设置" end
  maxLen=tonumber(maxLen) or 14
  if string.len(s)<=maxLen then return s end
  return string.sub(s,1,maxLen-3).."..."
end

function Warn.DirectionMediaName(slot)
  if type(Warn.directionMedia)~="table" then Warn.directionMedia={LEFT="",RIGHT=""} end
  return WarnMediaName(Warn.directionMedia[slot] or "") or ""
end

function Warn.DirectionMediaPath(slot)
  local name=Warn.DirectionMediaName(slot)
  if name=="" then return "" end
  return Warn.mediaDir..name
end

function Warn.SafeMediaName()
  return WarnMediaName(Warn.safeMediaName or "") or ""
end

function Warn.SafeMediaPath()
  local name=Warn.SafeMediaName()
  if name=="" then return "" end
  return Warn.mediaDir..name
end

function Warn.ResolveTexturePath()
  if Warn.state=="SAFE" then
    if Warn.showSafeImage then return Warn.SafeMediaPath() end
    return ""
  end
  return Warn.path or ""
end

function Warn.SetDirectionSlot(slot,applyNow)
  local path=""
  if slot then path=Warn.DirectionMediaPath(slot) end
  local changed=(Warn.directionSlot~=slot) or (Warn.directionPath~=path)
  Warn.directionSlot=slot
  Warn.directionPath=path
  if changed and applyNow~=false then Warn.UpdateVisibility() end
end

function Warn.CycleDirectionMedia(slot)
  if slot~="LEFT" and slot~="RIGHT" then return end
  Warn.RefreshMediaList()
  local n=table.getn(Warn.mediaFiles)
  if n<=0 then Chat("方向预警素材：Media 目录没有 TGA") Warn.SyncUI(); return end
  local old=Warn.DirectionMediaName(slot)
  local idx=0; local i
  if old~="" then
    for i=1,n do
      if string.lower(Warn.mediaFiles[i])==string.lower(old) then idx=i; break end
    end
  end
  idx=idx+1; if idx>n then idx=1 end
  Warn.directionMedia[slot]=Warn.mediaFiles[idx]
  Warn.ApplyDirectionFrame(slot); Warn.Save(); Warn.SyncUI(); Warn.UpdateVisibility(); Warn.dirty=true
  Chat("方向素材 "..WarnDirectionLabel(slot).."："..tostring(Warn.mediaFiles[idx]))
end

function Warn.DirectionButtonText(slot)
  return WarnDirectionLabel(slot).."："..WarnShortMediaName(Warn.DirectionMediaName(slot),14).." ▼"
end

function Warn.SafeButtonText()
  return "安全："..WarnShortMediaName(Warn.SafeMediaName(),14).." ▼"
end

function Warn.DangerButtonText()
  return "危险："..WarnShortMediaName(Warn.CurrentMediaName(),14).." ▼"
end

function Warn.CycleSafeMedia()
  Warn.RefreshMediaList()
  local n=table.getn(Warn.mediaFiles)
  if n<=0 then Chat("安全预警素材：Media 目录没有 TGA") Warn.SyncUI(); return end
  local old=Warn.SafeMediaName()
  local idx=0; local i
  if old~="" then
    for i=1,n do
      if string.lower(Warn.mediaFiles[i])==string.lower(old) then idx=i; break end
    end
  end
  idx=idx+1; if idx>n then idx=1 end
  Warn.safeMediaName=Warn.mediaFiles[idx]
  Warn.ApplyFrame(); Warn.Save(); Warn.SyncUI(); Warn.UpdateVisibility(); Warn.dirty=true
  Chat("安全素材："..tostring(Warn.mediaFiles[idx]))
end

function Warn.RefreshMediaOnly()
  local old=Warn.CurrentMediaName()
  Warn.RefreshMediaList()
  if old~="" then
    local i
    for i=1,table.getn(Warn.mediaFiles) do
      if string.lower(Warn.mediaFiles[i])==string.lower(old) then Warn.mediaIndex=i; Warn.path=Warn.mediaDir..Warn.mediaFiles[i]; break end
    end
  end
  Warn.ApplyFrame(); Warn.Save(); Warn.SyncUI(); Warn.dirty=true
  if Warn.ui.mediaMenu and Warn.ui.mediaMenu:IsShown() then Warn.RenderMediaMenu() end
end

function Warn.CycleMedia()
  local old=Warn.CurrentMediaName()
  Warn.RefreshMediaList()
  local n=table.getn(Warn.mediaFiles)
  if n<=0 then Warn.ApplyFrame(); Warn.SyncUI(); return end
  local idx=Warn.mediaIndex or 1
  if old~="" then
    local i
    for i=1,n do if string.lower(Warn.mediaFiles[i])==string.lower(old) then idx=i; break end end
  end
  idx=idx+1; if idx>n then idx=1 end
  Warn.mediaIndex=idx; Warn.path=Warn.mediaDir..Warn.mediaFiles[idx]
  Warn.ApplyFrame(); Warn.Save(); Warn.SyncUI(); Warn.dirty=true
end

function Warn.GetDirectionPos(slot)
  if type(Warn.directionPos)~="table" then Warn.directionPos={} end
  local pos=Warn.directionPos[slot]
  if type(pos)~="table" then
    local sign=(slot=="LEFT") and -1 or 1
    pos={x=(Warn.x or 0)+(sign*220),y=Warn.y or 120}
    Warn.directionPos[slot]=pos
  end
  pos.x=math.floor(tonumber(pos.x) or 0); pos.y=math.floor(tonumber(pos.y) or 120)
  return pos
end

local function WarnScaledSize()
  local w=math.floor(tonumber(Warn.width) or 384); local h=math.floor(tonumber(Warn.height) or 192)
  if w<16 then w=16 elseif w>1024 then w=1024 end
  if h<16 then h=16 elseif h>1024 then h=1024 end
  Warn.width=w; Warn.height=h
  local scale=tonumber(Warn.scale) or 1.0
  if scale<0.25 then scale=0.25 elseif scale>1.50 then scale=1.50 end
  Warn.scale=scale
  local drawW=math.floor(w*scale+0.5); local drawH=math.floor(h*scale+0.5)
  if drawW<8 then drawW=8 end; if drawH<8 then drawH=8 end
  return drawW,drawH
end

function Warn.ApplyDirectionFrame(slot)
  local f=Warn.directionFrames and Warn.directionFrames[slot]
  if not f then return end
  local drawW,drawH=WarnScaledSize()
  local pos=Warn.GetDirectionPos(slot)
  f:ClearAllPoints(); f:SetPoint("CENTER",UIParent,"CENTER",pos.x,pos.y)
  f:SetWidth(drawW); f:SetHeight(drawH)
  local tex=Warn.directionTextures and Warn.directionTextures[slot]
  if tex then
    local path=Warn.DirectionMediaPath(slot)
    if path~="" then tex:SetTexture(path) else tex:SetTexture(nil) end
    tex:SetAllPoints(f)
  end
end

function Warn.ApplyFrame()
  if not Warn.frame then return end
  local drawW,drawH=WarnScaledSize()
  Warn.x=math.floor(tonumber(Warn.x) or 0); Warn.y=math.floor(tonumber(Warn.y) or 120)
  Warn.frame:ClearAllPoints(); Warn.frame:SetPoint("CENTER",UIParent,"CENTER",Warn.x,Warn.y)
  Warn.frame:SetWidth(drawW); Warn.frame:SetHeight(drawH)
  if Warn.texture then
    local texturePath=Warn.ResolveTexturePath()
    if texturePath~="" then Warn.texture:SetTexture(texturePath) else Warn.texture:SetTexture(nil) end
    Warn.texture:SetAllPoints(Warn.frame)
  end
  Warn.ApplyDirectionFrame("LEFT")
  Warn.ApplyDirectionFrame("RIGHT")
end

function Warn.Save()
  AutoRangeDB.warningEnabled=Warn.enabled and true or false
  AutoRangeDB.warningMode=Warn.NormalizeMode(Warn.mode)
  AutoRangeDB.warningMediaName=Warn.CurrentMediaName()
  AutoRangeDB.warningMediaIndex=Warn.mediaIndex or 0
  AutoRangeDB.warningSafeMediaName=Warn.SafeMediaName()
  AutoRangeDB.warningShowSafeImage=Warn.showSafeImage and true or false
  AutoRangeDB.warningDirectionMedia={
    LEFT=Warn.DirectionMediaName("LEFT"),RIGHT=Warn.DirectionMediaName("RIGHT")
  }
  local lp=Warn.GetDirectionPos("LEFT"); local rp=Warn.GetDirectionPos("RIGHT")
  AutoRangeDB.warningDirectionPosition={LEFT={x=lp.x,y=lp.y},RIGHT={x=rp.x,y=rp.y}}
  AutoRangeDB.warningX=Warn.x; AutoRangeDB.warningY=Warn.y
  AutoRangeDB.warningWidth=Warn.width; AutoRangeDB.warningHeight=Warn.height
  AutoRangeDB.warningScale=Warn.scale or 1.0
end

function Warn.SyncUI()
  if Warn.ui.enabled then Warn.ui.enabled:SetChecked(Warn.enabled) end
  if Warn.ui.modeSingle then Warn.ui.modeSingle:SetText((Warn.mode=="SINGLE" and "● " or "○ ").."单图预警") end
  if Warn.ui.modeDirection2 then Warn.ui.modeDirection2:SetText((Warn.mode=="DIRECTION2" and "● " or "○ ").."2方向预警") end
  if Warn.ui.modeHint then
    if Warn.mode=="DIRECTION2" then
      Warn.ui.modeHint:SetText("2方向：普通/视野范围判定左/右；视野全局/全局必中自动改用单图")
    else
      Warn.ui.modeHint:SetText("单图：危险图/安全图双素材；安全图仅在可阻挡模式且开启时显示")
    end
  end
  local isDir=(Warn.mode=="DIRECTION2")
  if Warn.ui.singleDanger then if isDir then Warn.ui.singleDanger:Hide() else Warn.ui.singleDanger:Show() end; Warn.ui.singleDanger:SetText(Warn.DangerButtonText()) end
  if Warn.ui.singleSafe then if isDir then Warn.ui.singleSafe:Hide() else Warn.ui.singleSafe:Show() end; Warn.ui.singleSafe:SetText(Warn.SafeButtonText()) end
  if Warn.ui.safeToggle then if isDir then Warn.ui.safeToggle:Hide() else Warn.ui.safeToggle:Show() end; Warn.ui.safeToggle:SetText(Warn.showSafeImage and "显示安全图：开" or "显示安全图：关") end
  if Warn.ui.mediaRefresh then Warn.ui.mediaRefresh:Show() end
  if Warn.ui.dirLeft then if isDir then Warn.ui.dirLeft:Show() else Warn.ui.dirLeft:Hide() end; Warn.ui.dirLeft:SetText(Warn.DirectionButtonText("LEFT")) end
  if Warn.ui.dirRight then if isDir then Warn.ui.dirRight:Show() else Warn.ui.dirRight:Hide() end; Warn.ui.dirRight:SetText(Warn.DirectionButtonText("RIGHT")) end
  if Warn.ui.media then
    local n=table.getn(Warn.mediaFiles)
    local source="来源："..tostring(Warn.mediaSource or "").."  AutoRange\\Media\\*.tga"
    if Warn.mode=="SINGLE" then
      Warn.ui.media:SetText("单图素材：危险="..WarnShortMediaName(Warn.CurrentMediaName(),12).."  安全="..WarnShortMediaName(Warn.SafeMediaName(),12).."   ["..tostring(n).."]   "..source)
    else
      Warn.ui.media:SetText("方向素材：左="..WarnShortMediaName(Warn.DirectionMediaName("LEFT"),12).."  右="..WarnShortMediaName(Warn.DirectionMediaName("RIGHT"),12).."   "..source)
    end
  end
  if Warn.ui.mediaHint then Warn.ui.mediaHint:SetText(""); Warn.ui.mediaHint:Hide() end
  if Warn.ui.posLabel then
    if Warn.mode=="DIRECTION2" then Warn.ui.posLabel:SetText("2方向位置：测试时左右分别拖动；宽高 / 比例共用")
    else Warn.ui.posLabel:SetText("屏幕位置 / 显示尺寸") end
  end
  local dirMode=(Warn.mode=="DIRECTION2")
  if Warn.ui.xLab then if dirMode then Warn.ui.xLab:Hide() else Warn.ui.xLab:Show() end end
  if Warn.ui.x then if dirMode then Warn.ui.x:Hide() else Warn.ui.x:Show() end; Warn.ui.x:SetText(tostring(Warn.x or 0)) end
  if Warn.ui.yLab then if dirMode then Warn.ui.yLab:Hide() else Warn.ui.yLab:Show() end end
  if Warn.ui.y then if dirMode then Warn.ui.y:Hide() else Warn.ui.y:Show() end; Warn.ui.y:SetText(tostring(Warn.y or 0)) end
  if Warn.ui.apply then Warn.ui.apply:SetText(dirMode and "保存尺寸" or "保存位置尺寸") end
  if Warn.ui.w then Warn.ui.w:SetText(tostring(Warn.width or 384)) end
  if Warn.ui.h then Warn.ui.h:SetText(tostring(Warn.height or 192)) end
  if Warn.ui.test then Warn.ui.test:SetText(Warn.test and "结束测试" or "测试 / 拖动") end
  if Warn.ui.scale then Warn.ui.scale:SetText(tostring(math.floor((Warn.scale or 1.0)*100+0.5)).."% ▼") end
end

function Warn.UpdateVisibility()
  if not Warn.frame then return end
  local useDir=(Warn.mode=="DIRECTION2" and Warn.displayMode=="DIR")
  local useSingle=(Warn.mode=="SINGLE" or Warn.displayMode=="SINGLE")
  local lf=Warn.directionFrames and Warn.directionFrames.LEFT
  local rf=Warn.directionFrames and Warn.directionFrames.RIGHT
  if useDir then
    Warn.frame:Hide(); Warn.frame:EnableMouse(false)
    if Warn.test then
      if lf then if Warn.DirectionMediaName("LEFT")~="" then lf:Show() else lf:Hide() end; lf:EnableMouse(true) end
      if rf then if Warn.DirectionMediaName("RIGHT")~="" then rf:Show() else rf:Hide() end; rf:EnableMouse(true) end
      if Warn.directionLabels.LEFT then Warn.directionLabels.LEFT:Show() end
      if Warn.directionLabels.RIGHT then Warn.directionLabels.RIGHT:Show() end
    else
      if Warn.directionLabels.LEFT then Warn.directionLabels.LEFT:Hide() end
      if Warn.directionLabels.RIGHT then Warn.directionLabels.RIGHT:Hide() end
      if lf then lf:EnableMouse(false); if Warn.enabled and Warn.state=="DANGER" and Warn.directionSlot=="LEFT" and Warn.DirectionMediaName("LEFT")~="" then lf:Show() else lf:Hide() end end
      if rf then rf:EnableMouse(false); if Warn.enabled and Warn.state=="DANGER" and Warn.directionSlot=="RIGHT" and Warn.DirectionMediaName("RIGHT")~="" then rf:Show() else rf:Hide() end end
    end
  else
    if Warn.directionLabels.LEFT then Warn.directionLabels.LEFT:Hide() end
    if Warn.directionLabels.RIGHT then Warn.directionLabels.RIGHT:Hide() end
    if lf then lf:Hide(); lf:EnableMouse(false) end
    if rf then rf:Hide(); rf:EnableMouse(false) end
    local texPath=Warn.ResolveTexturePath()
    if Warn.texture then
      if texPath~="" then Warn.texture:SetTexture(texPath) else Warn.texture:SetTexture(nil) end
      Warn.texture:SetAllPoints(Warn.frame)
    end
    if Warn.test then
      Warn.frame:Show(); Warn.frame:EnableMouse(true)
    elseif useSingle and Warn.enabled and Warn.state~="NONE" and texPath~="" then
      Warn.frame:Show(); Warn.frame:EnableMouse(false)
    else
      Warn.frame:Hide(); Warn.frame:EnableMouse(false)
    end
  end
end

function Warn.SetState(state,displayMode)
  state=string.upper(tostring(state or "NONE"))
  if state~="DANGER" and state~="SAFE" then state="NONE" end
  Warn.state=state
  Warn.danger=(state=="DANGER")
  Warn.displayMode=displayMode or ((Warn.mode=="DIRECTION2") and "DIR" or "SINGLE")
  Warn.UpdateVisibility()
end

function Warn.RuleModeForHazard(h)
  if not h then return "-" end
  local rule=SF.RuleForSource(h.sourceType,h.spell)
  return rule and SF.ValidRuleMode(rule.mode) or "NORMAL"
end

local function WarnHazardCenter(h)
  if not h then return nil end
  return tonumber(h.visualX or h.x),tonumber(h.visualY or h.y)
end

function Warn.IsEnemyHazard(h)
  if not h or not (h.sourceType=="ENEMY" or h.sourceType=="UNKNOWN") then return false end
  if not HazardFilterVisible(h) then return false end
  return true
end

local function WarnGeometryCheck(h,px,py)
  if not h.visualCreated or h.visualHidden then return false,false end
  local r=tonumber(h.radius) or 0
  local cx,cy=WarnHazardCenter(h)
  if r<=0 or cx==nil or cy==nil then return false,false end
  if not px or not py then return true,false end
  local dx=px-cx; local dy=py-cy
  return true,((dx*dx+dy*dy)<=r*r)
end

function Warn.CasterLOSUnit(h)
  if not h then return nil end
  local guid=NormGuid(h.casterGuid or "")
  if guid and guid~="0" and string.find(guid,"^[0-9A-F]+$") then return "0x"..guid end
  local raw=tostring(h.casterGuidRaw or h.caster or "")
  if raw=="" then return nil end
  if string.sub(raw,1,2)=="0X" then raw="0x"..string.sub(raw,3) end
  return raw
end

function Warn.QueryLOS(h)
  if type(TaiYangShenDian)~="function" then return nil end
  local now=GetTime()
  if now<(Warn.losReadyAt or 0) then return nil end
  local unitId=Warn.CasterLOSUnit(h)
  if not unitId then return nil end
  local key=tostring(h.casterGuid or unitId)
  local c=Warn.losCache[key]
  if c and now-(c.at or 0)<(Warn.losCacheTTL or 0.05) then return c.value end
  local ok,result=pcall(TaiYangShenDian,"Unit.InSight","player",unitId)
  local value=nil
  if ok then
    if result==true or result==1 then value=true
    elseif result==false or result==0 then value=false end
  end
  if c then c.at=now; c.value=value else Warn.losCache[key]={at=now,value=value} end
  return value
end

local function WarnPlayerFacing()
  if type(TaiYangShenDian)~="function" then return nil end
  local ok,v=pcall(TaiYangShenDian,"facing","player")
  if ok and tonumber(v) then return tonumber(v) end
  return nil
end

function Warn.DirectionSlotForHazard(h,px,py)
  if Warn.mode~="DIRECTION2" then return nil end
  if not h or px==nil or py==nil then return nil end
  local cx,cy=WarnHazardCenter(h); if cx==nil or cy==nil then return nil end

  -- Escape vector: hazard centre -> player. This is the shortest way out of a circle.
  local ex=px-cx
  local ey=py-cy
  local dist2=(ex*ex)+(ey*ey)
  if dist2<=0.000001 then
    -- Exact hazard centre: left/right is mathematically tied. Never hide the warning.
    if h.warnDirectionSlot=="LEFT" or h.warnDirectionSlot=="RIGHT" then return h.warnDirectionSlot end
    if Warn.directionSlot=="LEFT" or Warn.directionSlot=="RIGHT" then
      h.warnDirectionSlot=Warn.directionSlot
      return Warn.directionSlot
    end
    h.warnDirectionSlot="LEFT"
    return "LEFT"
  end

  -- Stage2.23: LEFT / RIGHT is defined only by the player's own facing.
  -- Native TaiYangShenDian facing uses the Vanilla movement-facing convention:
  --   forward = (cos(facing), sin(facing))
  --   left    = (-sin(facing), cos(facing))
  local facing=WarnPlayerFacing()
  if facing==nil then
    -- Never make the warning disappear just because one facing read failed.
    if h.warnDirectionSlot=="LEFT" or h.warnDirectionSlot=="RIGHT" then return h.warnDirectionSlot end
    if Warn.directionSlot=="LEFT" or Warn.directionSlot=="RIGHT" then
      h.warnDirectionSlot=Warn.directionSlot
      return Warn.directionSlot
    end
    -- Old DLL / first sample fallback: keep 2-dir visible. This path is not used
    -- with the Stage2.23 unified DLL because TaiYangShenDian("facing","player") is available.
    local fallback=(ex<0) and "LEFT" or "RIGHT"
    h.warnDirectionSlot=fallback
    return fallback
  end

  -- Dot the escape vector with the player's LEFT vector.
  -- Positive = escape is on the player's left side; negative = right side.
  local side=(-ex*math.sin(facing))+(ey*math.cos(facing))
  local dist=math.sqrt(dist2)
  local hold=dist*0.08
  if hold<0.05 then hold=0.05 end

  if math.abs(side)<=hold then
    -- Front/back centreline has no unique left/right. Keep the previous result to
    -- prevent flicker; on the very first sample choose a deterministic sign so the
    -- warning is still shown instead of disappearing.
    if h.warnDirectionSlot=="LEFT" or h.warnDirectionSlot=="RIGHT" then return h.warnDirectionSlot end
    if Warn.directionSlot=="LEFT" or Warn.directionSlot=="RIGHT" then
      h.warnDirectionSlot=Warn.directionSlot
      return Warn.directionSlot
    end
    local slot=(side>=0) and "LEFT" or "RIGHT"
    h.warnDirectionSlot=slot
    return slot
  end

  local slot=(side>0) and "LEFT" or "RIGHT"
  h.warnDirectionSlot=slot
  return slot
end

function Warn.EvaluateHazardState(h,px,py,mode)
  if not Warn.IsEnemyHazard(h) then return "NONE" end
  mode=mode or Warn.RuleModeForHazard(h)

  if mode=="NORMAL" then
    local hasGeom,inside=WarnGeometryCheck(h,px,py)
    if hasGeom and px~=nil and py~=nil and inside then return "DANGER" end
    return "NONE"
  end

  if mode=="VISION_RANGE" then
    local hasGeom,inside=WarnGeometryCheck(h,px,py)
    if not hasGeom or px==nil or py==nil or not inside then return "NONE" end
    local los=Warn.QueryLOS(h)
    if los==false then return "SAFE" end
    return "DANGER" -- unknown stays conservative
  end

  if mode=="VISION_GLOBAL" then
    if h.castPending then return "NONE" end
    local los=Warn.QueryLOS(h)
    if los==false then return "SAFE" end
    return "DANGER" -- unknown stays conservative
  end

  if mode=="GLOBAL_HIT" then
    if not h.castPending then return "DANGER" end
    return "NONE"
  end

  return "NONE"
end

-- Module-scope scan state avoids creating a new scanList closure every 0.01s.
local warnScanPX,warnScanPY,warnScanPlayerPosTried
local function WarnScanStateList(list,bestState,bestHazard,bestMode)
  local j,hh,evalMode,state
  for j=1,table.getn(list) do
    hh=hazards[list[j]]
    if hh then
      evalMode=Warn.RuleModeForHazard(hh)
      if not warnScanPlayerPosTried and (evalMode=="NORMAL" or evalMode=="VISION_RANGE") then
        warnScanPX,warnScanPY=M2.CurrentPlayerPosition(); warnScanPlayerPosTried=true
      end
      state=Warn.EvaluateHazardState(hh,warnScanPX,warnScanPY,evalMode)
      if state=="DANGER" then return "DANGER",hh,evalMode end
      if state=="SAFE" and bestState~="SAFE" then bestState,bestHazard,bestMode="SAFE",hh,evalMode end
    end
  end
  return bestState,bestHazard,bestMode
end

function Warn.RefreshDanger()
  Warn.dirty=false
  if not Warn.enabled then Warn.SetDirectionSlot(nil,true); Warn.SetState("NONE","SINGLE"); return end
  if not running or not visualEnabled or not enemyCircle then Warn.SetDirectionSlot(nil,true); Warn.SetState("NONE","SINGLE"); return end

  warnScanPX=nil; warnScanPY=nil; warnScanPlayerPosTried=false
  local hitState,hitHazard,hitMode="NONE",nil,nil
  hitState,hitHazard,hitMode=WarnScanStateList(activeCasterKeys,hitState,hitHazard,hitMode)
  if hitState~="DANGER" then hitState,hitHazard,hitMode=WarnScanStateList(activeDynamicKeys,hitState,hitHazard,hitMode) end
  if hitState~="DANGER" then hitState,hitHazard,hitMode=WarnScanStateList(activeDiscoveryKeys,hitState,hitHazard,hitMode) end

  Warn.SetDirectionSlot(nil,false)
  if hitState=="DANGER" then
    if Warn.mode=="DIRECTION2" and (hitMode=="NORMAL" or hitMode=="VISION_RANGE") then
      local slot=Warn.DirectionSlotForHazard(hitHazard,warnScanPX,warnScanPY)
      if slot and Warn.DirectionMediaName(slot)~="" then
        Warn.SetDirectionSlot(slot,false)
        Warn.SetState("DANGER","DIR")
      else
        Warn.SetState("NONE","DIR")
      end
    else
      Warn.SetState("DANGER","SINGLE")
    end
  elseif hitState=="SAFE" then
    Warn.SetState("SAFE","SINGLE")
  else
    Warn.SetState("NONE","SINGLE")
  end
end

function Warn.ApplyFromUI(announce)
  local x=tonumber(Warn.ui.x and Warn.ui.x:GetText() or Warn.x) or Warn.x
  local y=tonumber(Warn.ui.y and Warn.ui.y:GetText() or Warn.y) or Warn.y
  local w=tonumber(Warn.ui.w and Warn.ui.w:GetText() or Warn.width) or Warn.width
  local h=tonumber(Warn.ui.h and Warn.ui.h:GetText() or Warn.height) or Warn.height
  Warn.x=math.floor(x); Warn.y=math.floor(y); Warn.width=math.floor(w); Warn.height=math.floor(h)
  Warn.ApplyFrame(); Warn.Save(); Warn.SyncUI(); Warn.dirty=true
  if announce then Chat("危险预警：位置 / 尺寸已保存") end
end

function Warn.SetEnabled(on,announce)
  Warn.enabled=on and true or false; Warn.Save(); Warn.dirty=true
  if not Warn.enabled then Warn.SetState("NONE","SINGLE") else Warn.RefreshDanger() end
  if announce then Chat("危险预警："..(Warn.enabled and "开启" or "关闭")) end
  Warn.SyncUI()
end

function Warn.SetMode(mode,announce)
  Warn.mode=Warn.NormalizeMode(mode)
  Warn.SetDirectionSlot(nil,false)
  Warn.Save(); Warn.ApplyFrame(); Warn.SyncUI()
  if Warn.enabled then Warn.RefreshDanger() else Warn.UpdateVisibility() end
  if announce then Chat("危险预警模式："..Warn.ModeText()) end
end

Warn.scaleOptions={0.25,0.40,0.50,0.60,0.70,0.80,0.90,1.00,1.25,1.50}

function Warn.SetScale(v,announce)
  local scale=tonumber(v) or 1.0
  if scale<0.25 then scale=0.25 elseif scale>1.50 then scale=1.50 end
  Warn.scale=scale
  Warn.ApplyFrame(); Warn.Save(); Warn.SyncUI()
  if Warn.ui.scaleMenu then Warn.ui.scaleMenu:Hide() end
  if announce then Chat("危险预警图片比例："..tostring(math.floor(scale*100+0.5)).."%") end
end

function Warn.ToggleScaleMenu()
  if not Warn.ui.scaleMenu then return end
  if Warn.ui.scaleMenu:IsShown() then Warn.ui.scaleMenu:Hide() else Warn.ui.scaleMenu:Show() end
end

Warn.mediaMenuPageSize=10
Warn.mediaMenuPage=1
Warn.mediaMenuSlot=nil

function Warn.MediaSlotLabel(slot)
  if slot=="DANGER" then return "危险" end
  if slot=="SAFE" then return "安全" end
  if slot=="LEFT" then return "左" end
  if slot=="RIGHT" then return "右" end
  return "素材"
end

function Warn.MediaSlotName(slot)
  if slot=="DANGER" then return Warn.CurrentMediaName() end
  if slot=="SAFE" then return Warn.SafeMediaName() end
  if slot=="LEFT" or slot=="RIGHT" then return Warn.DirectionMediaName(slot) end
  return ""
end

function Warn.SetMediaSlot(slot,name,announce)
  name=WarnMediaName(name or "") or ""
  if name=="" then return end
  local applied=false
  if slot=="DANGER" then
    local i,n=1,table.getn(Warn.mediaFiles)
    for i=1,n do
      if string.lower(Warn.mediaFiles[i])==string.lower(name) then
        Warn.mediaIndex=i; Warn.path=Warn.mediaDir..Warn.mediaFiles[i]; applied=true; break
      end
    end
    if not applied then Warn.mediaFallbackName=name; Warn.path=Warn.mediaDir..name; applied=true end
    Warn.ApplyFrame()
  elseif slot=="SAFE" then
    Warn.safeMediaName=name; Warn.ApplyFrame(); applied=true
  elseif slot=="LEFT" or slot=="RIGHT" then
    Warn.directionMedia[slot]=name; Warn.ApplyDirectionFrame(slot); applied=true
  end
  if applied then
    Warn.Save(); Warn.SyncUI(); Warn.UpdateVisibility(); Warn.dirty=true
    if announce then Chat(Warn.MediaSlotLabel(slot).."素材："..tostring(name)) end
  end
end

function Warn.CloseMediaMenu()
  if Warn.ui.mediaMenu then Warn.ui.mediaMenu:Hide() end
  Warn.mediaMenuSlot=nil
end

function Warn.RenderMediaMenu()
  if not Warn.ui.mediaMenu then return end
  Warn.RefreshMediaList()
  local files=Warn.mediaFiles or {}
  local n=table.getn(files)
  local pageSize=Warn.mediaMenuPageSize or 10
  local maxPage=1
  if n>0 then maxPage=math.ceil(n/pageSize) end
  if Warn.mediaMenuPage<1 then Warn.mediaMenuPage=1 elseif Warn.mediaMenuPage>maxPage then Warn.mediaMenuPage=maxPage end
  local start=(Warn.mediaMenuPage-1)*pageSize+1
  local finish=start+pageSize-1
  if Warn.ui.mediaMenuTitle then
    Warn.ui.mediaMenuTitle:SetText(Warn.MediaSlotLabel(Warn.mediaMenuSlot).."素材选择")
  end
  local i,row,btn,idx,name,current
  current=Warn.MediaSlotName(Warn.mediaMenuSlot)
  for i=1,pageSize do
    btn=Warn.ui.mediaMenuButtons[i]
    idx=start+i-1
    if btn then
      if idx<=n then
        name=files[idx]
        btn.mediaName=name
        btn:SetText((string.lower(name)==string.lower(current) and "● " or "○ ")..WarnShortMediaName(name,24))
        btn:Show()
      else
        btn.mediaName=nil
        btn:Hide()
      end
    end
  end
  if Warn.ui.mediaMenuPageText then Warn.ui.mediaMenuPageText:SetText(tostring(Warn.mediaMenuPage).."/"..tostring(maxPage)) end
  if Warn.ui.mediaMenuPrev then if Warn.mediaMenuPage>1 then Warn.ui.mediaMenuPrev:Enable(); Warn.ui.mediaMenuPrev:SetAlpha(1) else Warn.ui.mediaMenuPrev:Disable(); Warn.ui.mediaMenuPrev:SetAlpha(0.45) end end
  if Warn.ui.mediaMenuNext then if Warn.mediaMenuPage<maxPage then Warn.ui.mediaMenuNext:Enable(); Warn.ui.mediaMenuNext:SetAlpha(1) else Warn.ui.mediaMenuNext:Disable(); Warn.ui.mediaMenuNext:SetAlpha(0.45) end end
  Warn.ui.mediaMenu:SetHeight(32 + pageSize*20 + 28)
end

function Warn.ToggleMediaMenu(slot,anchor)
  if not Warn.ui.mediaMenu then return end
  if Warn.ui.scaleMenu then Warn.ui.scaleMenu:Hide() end
  Warn.RefreshMediaList()
  if Warn.ui.mediaMenu:IsShown() and Warn.mediaMenuSlot==slot then
    Warn.CloseMediaMenu(); return
  end
  Warn.mediaMenuSlot=slot
  Warn.mediaMenuPage=1
  local name=Warn.MediaSlotName(slot)
  local i,n=1,table.getn(Warn.mediaFiles)
  if name~="" then
    for i=1,n do
      if string.lower(Warn.mediaFiles[i])==string.lower(name) then
        Warn.mediaMenuPage=math.floor((i-1)/(Warn.mediaMenuPageSize or 10))+1
        break
      end
    end
  end
  Warn.ui.mediaMenu:ClearAllPoints()
  Warn.ui.mediaMenu:SetPoint("TOPLEFT",anchor,"BOTTOMLEFT",0,-2)
  Warn.RenderMediaMenu()
  Warn.ui.mediaMenu:Show()
end

function Warn.ToggleTest()
  Warn.test=not Warn.test
  if Warn.test then
    Warn.ApplyFromUI(false)
    if Warn.mode=="DIRECTION2" then Chat("2方向预警测试：左 / 右素材同时显示，可分别拖动；松开各自保存位置。")
    else Chat("危险预警测试：可直接拖动图片，松开自动保存位置。") end
  else Chat("危险预警测试：结束") end
  Warn.UpdateVisibility(); Warn.SyncUI()
end

function Warn.SaveDraggedPosition()
  if not Warn.frame then return end
  local fx,fy=Warn.frame:GetCenter(); local ux,uy=UIParent:GetCenter()
  if fx and fy and ux and uy then Warn.x=math.floor(fx-ux+0.5); Warn.y=math.floor(fy-uy+0.5) end
  Warn.Save(); Warn.ApplyFrame(); Warn.SyncUI()
end

function Warn.SaveDraggedDirectionPosition(slot)
  local f=Warn.directionFrames and Warn.directionFrames[slot]
  if not f then return end
  local fx,fy=f:GetCenter(); local ux,uy=UIParent:GetCenter()
  if fx and fy and ux and uy then
    local pos=Warn.GetDirectionPos(slot)
    pos.x=math.floor(fx-ux+0.5); pos.y=math.floor(fy-uy+0.5)
  end
  Warn.Save(); Warn.ApplyFrame(); Warn.SyncUI(); Warn.UpdateVisibility()
end

function M2.ClearTest(announce)
  if type(TaiYangShenDian)=="function" then pcall(TaiYangShenDian,"AutoRange.VisualClear",M2.testKey) end
  if announce then Chat("M2测试视觉：已清除") end
end

function M2.RememberPreview(rec)
  if not rec or not rec.path then return end
  local key=M2.RecordKey(rec.path,rec.source)
  local i
  for i=table.getn(M2.recent),1,-1 do
    if M2.RecordKey(M2.recent[i].path,M2.recent[i].source)==key then table.remove(M2.recent,i) end
  end
  table.insert(M2.recent,1,{path=rec.path,source=rec.source,label=rec.label or M2.Basename(rec.path)})
  while table.getn(M2.recent)>(M2.recentMax or 20) do table.remove(M2.recent) end
end

function M2.PreviewSelected()
  local rec=M2.selectedRecord
  if not rec or not rec.path then Chat("M2预览：请先选择右侧素材") return false end
  if type(TaiYangShenDian)~="function" then Chat("M2预览失败：TaiYangShenDian不可用") return false end
  local x,y,z=M2.CurrentPlayerPosition()
  if not x then Chat("M2预览失败：无法读取玩家世界坐标") return false end
  M2.ClearTest(false)
  local callOk,ok,normalized,stage=pcall(TaiYangShenDian,"AutoRange.VisualSet",M2.testKey,rec.path,x,y,z,1,0)
  if callOk and ok then
    M2.RememberPreview(rec)
    Chat("M2预览已显示：Scale=1 · "..tostring(normalized or rec.path))
    if type(M2.RenderBrowser)=="function" then M2.RenderBrowser() end
    return true
  end
  Chat("M2预览失败："..tostring(stage or normalized or "VisualSet")); return false
end

function M2.AddSelected()
  local rec=M2.selectedRecord
  if not rec or not rec.path then Chat("M2素材：请先选择右侧素材") return nil end
  local path=M2.NormalizePath(rec.path)
  local id,old=M2.FindCustomByPath(path)
  local width=tonumber(M2.ui.width and M2.ui.width:GetText() or "") or M2.AutoDiameter(path)
  if width<=0 then width=M2.AutoDiameter(path) end
  if width>9999 then width=9999 end
  local label=M2.Basename(path)
  local source=tostring(rec.source or "")
  if not id then
    id="CUSTOM_"..tostring(M2.nextId); M2.nextId=M2.nextId+1
    table.insert(M2.customIds,id)
  end
  M2.customById[id]={id=id,label=label,mpq=source,path=path,diameter=width,custom=true}
  table.sort(M2.customIds,function(a,b)
    local _,_,aa=string.find(a,"([0-9]+)$"); local _,_,bb=string.find(b,"([0-9]+)$")
    return (tonumber(aa) or 0)<(tonumber(bb) or 0)
  end)
  M2.RebuildStyleOrder(); M2.SaveLibrary(); M2.RefreshStyledVisuals(id)
  rec.customId=id
  Chat("M2素材："..(old and "已更新 " or "已添加 ")..label.." · W"..tostring(width))
  if type(M2.RenderBrowser)=="function" then M2.RenderBrowser() end
  return id
end

function M2.ResetScanData()
  M2.scanRecords={}; M2.bySource={}; M2.sources={}
  M2.sourcePage=1; M2.modelPage=1
  M2.selectedSource=nil; M2.selectedRecord=nil
  M2.searchMode=false; M2.searchResults={}
  M2.scanResultCount=0; M2.scanGetIndex=1
  M2.scanArchiveCount=0; M2.scanArchiveGetIndex=1
end

function M2.AddSource(source)
  source=TrimText(source)
  if source=="" then source="未分类" end
  if not M2.bySource[source] then
    M2.bySource[source]={}
    M2.sources[table.getn(M2.sources)+1]=source
  end
  return source
end

function M2.AddScanRecord(path,source)
  path=M2.NormalizePath(path)
  if path=="" then return end
  source=M2.AddSource(source)
  local rec={path=path,source=source,label=M2.Basename(path)}
  M2.scanRecords[table.getn(M2.scanRecords)+1]=rec
  local list=M2.bySource[source]
  list[table.getn(list)+1]=rec
end

function M2.StopScanDriver()
  if M2.scanFrame then M2.scanFrame:SetScript("OnUpdate",nil) end
  M2.scanElapsed=0
end

function M2.FinishScanIndex()
  table.sort(M2.sources,function(a,b) return string.lower(tostring(a))<string.lower(tostring(b)) end)
  local _,list
  for _,list in pairs(M2.bySource) do
    table.sort(list,function(a,b) return string.lower(a.path)<string.lower(b.path) end)
  end
  if table.getn(M2.sources)>0 then M2.selectedSource=M2.sources[1] end
  M2.scanPhase="DONE"; M2.StopScanDriver()
  M2.scanStatusText="扫描完成："..tostring(table.getn(M2.scanRecords)).." 个M2 / "..tostring(M2.scanArchiveCount or 0).." 个MPQ / "..tostring(table.getn(M2.sources)).." 个来源"
  if table.getn(M2.sources)==1 and M2.sources[1]=="未分类" then
    M2.scanStatusText=M2.scanStatusText.." · DLL未提供MPQ分类"
  end
  if type(M2.RenderBrowser)=="function" then M2.RenderBrowser() end
end

function M2.ScanOnUpdate()
  M2.scanElapsed=(M2.scanElapsed or 0)+(arg1 or 0)
  if M2.scanElapsed<0.04 then return end
  M2.scanElapsed=0
  if M2.scanPhase=="SCAN" then
    local callOk,running,complete,processed,total,count,current,last=pcall(TaiYangShenDian,"AutoRange.M2Scan.Step")
    if not callOk or type(running)~="boolean" or type(complete)~="boolean" then
      M2.scanPhase="ERROR"; M2.StopScanDriver(); M2.scanStatusText="扫描失败：当前DLL不支持AutoRange.M2Scan"; M2.RenderBrowser(); return
    end
    M2.scanResultCount=tonumber(count) or 0
    M2.scanArchiveCount=tonumber(total) or M2.scanArchiveCount or 0
    M2.scanStatusText="扫描MPQ："..tostring(tonumber(processed) or 0).."/"..tostring(M2.scanArchiveCount)..(current and (" · "..tostring(current)) or "")
    if complete then M2.scanPhase="ARCHIVES"; M2.scanArchiveGetIndex=1; M2.scanStatusText="建立MPQ分类：0/"..tostring(M2.scanArchiveCount) end
  elseif M2.scanPhase=="ARCHIVES" then
    local limit=60
    local done=0
    while done<limit and M2.scanArchiveGetIndex<=M2.scanArchiveCount do
      local callOk,ok,source=pcall(TaiYangShenDian,"AutoRange.M2Scan.ArchiveGet",M2.scanArchiveGetIndex)
      if not callOk or type(ok)~="boolean" then
        M2.scanPhase="ERROR"; M2.StopScanDriver(); M2.scanStatusText="当前DLL缺少R7.5.1 MPQ分类接口，请安装R7.5.1 FULL里的DLL"; M2.RenderBrowser(); return
      end
      if ok and source then M2.AddSource(source) end
      M2.scanArchiveGetIndex=M2.scanArchiveGetIndex+1; done=done+1
    end
    M2.scanStatusText="建立MPQ分类："..tostring(math.min(M2.scanArchiveGetIndex-1,M2.scanArchiveCount)).."/"..tostring(M2.scanArchiveCount)
    if M2.scanArchiveGetIndex>M2.scanArchiveCount then
      M2.scanPhase="INDEX"; M2.scanGetIndex=1; M2.scanStatusText="整理M2：0/"..tostring(M2.scanResultCount)
    end
  elseif M2.scanPhase=="INDEX" then
    local limit=60
    local done=0
    while done<limit and M2.scanGetIndex<=M2.scanResultCount do
      local callOk,ok,path,source=pcall(TaiYangShenDian,"AutoRange.M2Scan.Get",M2.scanGetIndex)
      if callOk and ok and path then M2.AddScanRecord(path,source) end
      M2.scanGetIndex=M2.scanGetIndex+1; done=done+1
    end
    M2.scanStatusText="整理M2："..tostring(math.min(M2.scanGetIndex-1,M2.scanResultCount)).."/"..tostring(M2.scanResultCount)
    if M2.scanGetIndex>M2.scanResultCount then M2.FinishScanIndex(); return end
  else
    M2.StopScanDriver()
  end
  if type(M2.RenderBrowser)=="function" then M2.RenderBrowser() end
end

function M2.StartScan()
  if type(TaiYangShenDian)~="function" then Chat("M2扫描失败：TaiYangShenDian不可用") return false end
  M2.ClearTest(false); M2.ResetScanData()
  local callOk,running,complete,processed,total,count,current,last=pcall(TaiYangShenDian,"AutoRange.M2Scan.Start")
  if not callOk or type(running)~="boolean" or type(complete)~="boolean" then
    M2.scanPhase="ERROR"; M2.scanStatusText="当前DLL不支持M2来源分类，请安装R7.5 FULL里的DLL"; Chat(M2.scanStatusText); M2.RenderBrowser(); return false
  end
  M2.scanResultCount=tonumber(count) or 0
  M2.scanArchiveCount=tonumber(total) or 0
  if M2.scanArchiveCount>0 then
    local probeOk,probeResult=pcall(TaiYangShenDian,"AutoRange.M2Scan.ArchiveGet",1)
    if not probeOk or type(probeResult)~="boolean" then
      M2.scanPhase="ERROR"; M2.scanStatusText="当前DLL缺少R7.5.1 MPQ分类接口，请安装R7.5.1 FULL里的DLL"; Chat(M2.scanStatusText); M2.RenderBrowser(); return false
    end
  end
  if complete then
    M2.scanPhase="ARCHIVES"; M2.scanArchiveGetIndex=1; M2.scanStatusText="建立MPQ分类：0/"..tostring(M2.scanArchiveCount)
  else
    M2.scanPhase="SCAN"; M2.scanStatusText="扫描MPQ："..tostring(tonumber(processed) or 0).."/"..tostring(M2.scanArchiveCount)
  end
  if M2.scanFrame then M2.scanFrame:SetScript("OnUpdate",M2.ScanOnUpdate) end
  M2.RenderBrowser(); return true
end

function M2.NavSources()
  local t={"@LIBRARY","@RECENT"}
  local i
  for i=1,table.getn(M2.sources) do t[table.getn(t)+1]=M2.sources[i] end
  return t
end

function M2.NavSourceLabel(source)
  if source=="@LIBRARY" then return "已添加 ("..tostring(table.getn(M2.customIds))..")" end
  if source=="@RECENT" then return "最近预览 ("..tostring(table.getn(M2.recent))..")" end
  local list=M2.bySource[source] or {}
  return M2.SourceLabel(source).." ("..tostring(table.getn(list))..")"
end

function M2.LibraryRecords()
  local t={}
  local i,id,v
  for i=1,table.getn(M2.customIds) do
    id=M2.customIds[i]; v=M2.customById[id]
    if v then t[table.getn(t)+1]={path=v.path,source=v.mpq or "",label=v.label,customId=id,diameter=v.diameter} end
  end
  return t
end

function M2.CurrentRecords()
  if M2.searchMode then return M2.searchResults end
  if M2.selectedSource=="@LIBRARY" then return M2.LibraryRecords() end
  if M2.selectedSource=="@RECENT" then return M2.recent end
  return M2.bySource[M2.selectedSource] or {}
end

function M2.SelectSource(source)
  M2.selectedSource=source; M2.modelPage=1; M2.selectedRecord=nil; M2.searchMode=false
  if M2.ui.search then M2.ui.search:SetText("") end
  M2.RenderBrowser()
end

function M2.ClickSourceRow(row)
  local source=M2.visibleSourceRows[tonumber(row) or 0]
  if source then M2.SelectSource(source) end
end

function M2.ClickModelRow(row)
  local rec=M2.visibleModelRows[tonumber(row) or 0]
  if not rec then return end
  if M2.searchMode then
    M2.JumpToRecord(rec)
  else
    M2.selectedRecord=rec
    if M2.ui.width then M2.ui.width:SetText(tostring(rec.diameter or M2.AutoDiameter(rec.path))) end
    M2.RenderBrowser()
  end
end

function M2.ClickModelAction(row)
  local rec=M2.visibleModelRows[tonumber(row) or 0]
  if not rec then return end
  local same=M2.selectedRecord and M2.selectedRecord.path==rec.path and M2.selectedRecord.source==rec.source
  M2.selectedRecord=rec
  if M2.ui.width and not same then M2.ui.width:SetText(tostring(rec.diameter or M2.AutoDiameter(rec.path))) end
  if M2.selectedSource=="@LIBRARY" and not M2.searchMode then
    M2.DeleteId(rec.customId,true)
    M2.selectedRecord=nil
    M2.RenderBrowser()
  else
    M2.AddSelected()
  end
end

function M2.JumpToRecord(rec)
  if not rec then return end
  local nav=M2.NavSources(); local i,idx=1,nil
  for i=1,table.getn(nav) do if nav[i]==rec.source then idx=i; break end end
  if idx then M2.sourcePage=math.floor((idx-1)/(M2.sourcePageSize or 8))+1 end
  M2.selectedSource=rec.source; M2.searchMode=false; M2.searchResults={}
  if M2.ui.search then M2.ui.search:SetText("") end
  local list=M2.bySource[rec.source] or {}; local pos=nil
  for i=1,table.getn(list) do if list[i].path==rec.path then pos=i; break end end
  M2.modelPage=pos and (math.floor((pos-1)/(M2.modelPageSize or 8))+1) or 1
  M2.selectedRecord=rec
  if M2.ui.width then M2.ui.width:SetText(tostring(M2.AutoDiameter(rec.path))) end
  M2.RenderBrowser()
end

function M2.Search()
  local q=string.lower(TrimText(M2.ui.search and M2.ui.search:GetText() or ""))
  if q=="" then M2.searchMode=false; M2.searchResults={}; M2.modelPage=1; M2.RenderBrowser(); return end
  local out={}; local i,rec,hay
  for i=1,table.getn(M2.scanRecords) do
    rec=M2.scanRecords[i]
    hay=string.lower(tostring(rec.path).." "..tostring(rec.source).." "..tostring(rec.label))
    if string.find(hay,q,1,true) then out[table.getn(out)+1]=rec end
  end
  M2.searchResults=out; M2.searchMode=true; M2.modelPage=1; M2.selectedRecord=nil
  M2.RenderBrowser()
end

function M2.CycleSourcePage(delta)
  local nav=M2.NavSources(); local pages=math.max(1,math.ceil(table.getn(nav)/(M2.sourcePageSize or 8)))
  M2.sourcePage=M2.sourcePage+(tonumber(delta) or 0)
  if M2.sourcePage<1 then M2.sourcePage=pages elseif M2.sourcePage>pages then M2.sourcePage=1 end
  M2.RenderBrowser()
end

function M2.CycleModelPage(delta)
  local list=M2.CurrentRecords(); local pages=math.max(1,math.ceil(table.getn(list)/(M2.modelPageSize or 8)))
  M2.modelPage=M2.modelPage+(tonumber(delta) or 0)
  if M2.modelPage<1 then M2.modelPage=pages elseif M2.modelPage>pages then M2.modelPage=1 end
  M2.RenderBrowser()
end

function M2.RenderBrowser()
  if not M2.ui.status then return end
  M2.ui.status:SetText(M2.scanStatusText or "尚未扫描 · 点击“扫描M2”后按MPQ来源分类")
  local nav=M2.NavSources(); local sourcePages=math.max(1,math.ceil(table.getn(nav)/(M2.sourcePageSize or 8)))
  if M2.sourcePage<1 then M2.sourcePage=1 elseif M2.sourcePage>sourcePages then M2.sourcePage=sourcePages end
  local first=(M2.sourcePage-1)*(M2.sourcePageSize or 8)+1
  local i,idx,source,b
  M2.visibleSourceRows={}
  for i=1,(M2.sourcePageSize or 8) do
    idx=first+i-1; source=nav[idx]; b=M2.ui.sourceRows and M2.ui.sourceRows[i]
    M2.visibleSourceRows[i]=source
    if b and source then
      b:SetText((source==M2.selectedSource and "> " or "  ")..M2.NavSourceLabel(source)); b:Show()
    elseif b then b:Hide() end
  end
  if M2.ui.sourcePage then M2.ui.sourcePage:SetText(tostring(M2.sourcePage).."/"..tostring(sourcePages)) end

  local list=M2.CurrentRecords(); local modelPages=math.max(1,math.ceil(table.getn(list)/(M2.modelPageSize or 8)))
  if M2.modelPage<1 then M2.modelPage=1 elseif M2.modelPage>modelPages then M2.modelPage=modelPages end
  first=(M2.modelPage-1)*(M2.modelPageSize or 8)+1
  M2.visibleModelRows={}
  for i=1,(M2.modelPageSize or 8) do
    idx=first+i-1; local rec=list[idx]; b=M2.ui.modelRows and M2.ui.modelRows[i]
    M2.visibleModelRows[i]=rec
    local ab=M2.ui.modelActionRows and M2.ui.modelActionRows[i]
    if b and rec then
      local prefix=(M2.selectedRecord and M2.selectedRecord.path==rec.path and M2.selectedRecord.source==rec.source) and "> " or "  "
      local label=rec.label or M2.Basename(rec.path)
      if M2.searchMode then label=label.."  ["..M2.SourceLabel(rec.source).."]" end
      b:SetText(prefix..label); b:Show()
      if ab then
        if M2.selectedSource=="@LIBRARY" and not M2.searchMode then ab:SetText("删除") else ab:SetText("添加") end
        ab:Show()
      end
    else
      if b then b:Hide() end
      if ab then ab:Hide() end
    end
  end
  if M2.ui.modelPage then M2.ui.modelPage:SetText(tostring(M2.modelPage).."/"..tostring(modelPages)) end
  if M2.ui.rightTitle then
    if M2.searchMode then M2.ui.rightTitle:SetText("搜索结果 ("..tostring(table.getn(list))..") · 点击结果跳回来源")
    elseif M2.selectedSource then M2.ui.rightTitle:SetText(M2.NavSourceLabel(M2.selectedSource))
    else M2.ui.rightTitle:SetText("M2列表") end
  end

  local rec=M2.selectedRecord
  if M2.ui.selectedPath then M2.ui.selectedPath:SetText(rec and rec.path or "未选择M2") end
  if M2.ui.selectedSource then M2.ui.selectedSource:SetText(rec and ("来源："..M2.SourceLabel(rec.source)) or "来源：--") end
end

local lastFuBarEnabled=nil

-- DebuffFilter-style classic 1.12 UI skin. Visual-only refactor; no runtime polling added.
local ARUI_TEX_WHITE="Interface\\TutorialFrame\\TutorialFrameBackground"
local ARUI_BORDER="Interface\\Tooltips\\UI-Tooltip-Border"
local ARUI_C={
  -- DebuffFilter uses TutorialFrameBackground + Tooltip border. Keep the same classic
  -- Blizzard 1.12 materials, but lift the tint so the manager is not near-black.
  bg={0.30,0.33,0.40,0.98}, panel={0.34,0.37,0.44,0.98}, panel2={0.40,0.43,0.50,0.98},
  line={0.68,0.58,0.36,1}, blue2={0.62,0.78,1.00,1}, green={0.35,0.90,0.42,1},
  red={1.00,0.34,0.26,1}, white={1.00,0.95,0.80,1}, muted={0.86,0.80,0.64,1}, dim={0.69,0.67,0.58,1}
}
local function ARUI_Backdrop(frame,color,borderColor)
  frame:SetBackdrop({bgFile=ARUI_TEX_WHITE,edgeFile=ARUI_BORDER,tile=true,tileSize=16,edgeSize=12,insets={left=4,right=4,top=4,bottom=4}})
  frame:SetBackdropColor(color[1],color[2],color[3],color[4] or 1)
  local b=borderColor or ARUI_C.line
  frame:SetBackdropBorderColor(b[1],b[2],b[3],b[4] or 1)
end
local function ARUI_Font(parent,size,color,justify)
  local fs=parent:CreateFontString(nil,"OVERLAY","GameFontNormal")
  if size then fs:SetFont("Fonts\\ARKai_T.ttf",size) end
  local c=color or ARUI_C.white
  fs:SetTextColor(c[1],c[2],c[3],c[4] or 1)
  if justify then fs:SetJustifyH(justify) end
  return fs
end
local function ARUI_Button(parent,text,width,height)
  local b=CreateFrame("Button",nil,parent)
  b:SetWidth(width or 90); b:SetHeight(height or 26)
  ARUI_Backdrop(b,ARUI_C.panel2,ARUI_C.line)
  b.text=ARUI_Font(b,11,ARUI_C.white,"CENTER")
  b.text:SetAllPoints(b); b.text:SetText(text or "")
  function b:SetText(v) self.text:SetText(v or "") end
  b:SetScript("OnEnter",function() this:SetBackdropBorderColor(1.00,0.82,0.18,1) end)
  b:SetScript("OnLeave",function() this:SetBackdropBorderColor(ARUI_C.line[1],ARUI_C.line[2],ARUI_C.line[3],1) end)
  return b
end
function SF.CreateRuleRows(parent,kind,startY)
  local rows={}
  local i,row,sel,del
  local pageSize=SF.RulePageSize(kind)
  for i=1,pageSize do
    row={}
    sel=ARUI_Button(parent,"",520,22)
    sel:SetPoint("TOPLEFT",parent,"TOPLEFT",14,startY-((i-1)*26))
    sel.ruleKind=kind
    sel:SetScript("OnClick",function() if this.ruleID then SF.SelectRule(this.ruleKind,this.ruleID) end end)
    del=ARUI_Button(parent,"删除",58,22)
    del:SetPoint("LEFT",sel,"RIGHT",8,0)
    del.ruleKind=kind
    del:SetScript("OnClick",function() if this.ruleID then SF.DeleteRuleRow(this.ruleKind,this.ruleID) end end)
    row.select=sel; row.delete=del
    rows[i]=row
  end
  return rows
end


local function ARUI_Toggle(parent,label,width,state)
  local h=CreateFrame("Button",nil,parent)
  h:SetWidth(width or 250); h:SetHeight(28)
  h.label=ARUI_Font(h,11,ARUI_C.white,"LEFT")
  h.label:SetPoint("LEFT",h,"LEFT",0,0); h.label:SetText(label or "")
  h.track=CreateFrame("Frame",nil,h)
  h.track:SetWidth(20); h.track:SetHeight(20); h.track:SetPoint("RIGHT",h,"RIGHT",0,0)
  ARUI_Backdrop(h.track,{0.20,0.22,0.27,1},ARUI_C.line)
  h.knob=h.track:CreateTexture(nil,"OVERLAY")
  h.knob:SetTexture("Interface\\Buttons\\UI-CheckBox-Check")
  h.knob:SetWidth(24); h.knob:SetHeight(24); h.knob:SetPoint("CENTER",h.track,"CENTER",0,0)
  function h:SetState(v)
    self.state=v and true or false
    if self.state then
      self.track:SetBackdropColor(0.30,0.34,0.28,1)
      self.track:SetBackdropBorderColor(0.90,0.72,0.26,1)
      self.knob:Show()
    else
      self.track:SetBackdropColor(0.20,0.22,0.27,1)
      self.track:SetBackdropBorderColor(ARUI_C.line[1],ARUI_C.line[2],ARUI_C.line[3],1)
      self.knob:Hide()
    end
  end
  function h:SetChecked(v) self:SetState(v) end
  function h:GetChecked() return self.state end
  h:SetState(state)
  return h
end

local panel=CreateFrame("Frame","AutoRangePanel",UIParent)
panel:SetWidth(650); panel:SetHeight(522)
panel:SetPoint("TOP",UIParent,"TOP",0,-105)
panel:SetFrameStrata("DIALOG"); panel:SetMovable(true); panel:EnableMouse(true); panel:SetClampedToScreen(true)
ARUI_Backdrop(panel,ARUI_C.bg,{0.78,0.66,0.40,1})

local header=CreateFrame("Frame",nil,panel)
header:SetHeight(40); header:SetPoint("TOPLEFT",panel,"TOPLEFT",2,-2); header:SetPoint("TOPRIGHT",panel,"TOPRIGHT",-2,-2)
ARUI_Backdrop(header,{0.26,0.29,0.36,1},{0.78,0.66,0.40,1})
header:EnableMouse(true)
header:SetScript("OnMouseDown",function() if arg1=="LeftButton" then panel:StartMoving() end end)
header:SetScript("OnMouseUp",function() panel:StopMovingOrSizing() end)

local title=ARUI_Font(header,16,{1.00,0.82,0.18,1},"CENTER")
title:SetPoint("CENTER",header,"CENTER",0,0); title:SetText("AutoRange | 自动危险圈")
local signature=ARUI_Font(header,10,{0.86,0.80,0.64,1},"RIGHT")
signature:SetPoint("RIGHT",header,"RIGHT",-44,0); signature:SetText("太阳神殿")
local closeBtn=ARUI_Button(header,"X",26,22)
closeBtn:SetPoint("RIGHT",header,"RIGHT",-8,0)
closeBtn:SetScript("OnClick",function() Warn.test=false; Warn.UpdateVisibility(); Warn.CloseMediaMenu(); if Warn.ui.scaleMenu then Warn.ui.scaleMenu:Hide() end; M2.ClearTest(false); M2.StopScanDriver(); panel:Hide(); AutoRangeDB.hidden=true end)

local master=CreateFrame("Frame",nil,panel)
master:SetHeight(42); master:SetPoint("TOPLEFT",header,"BOTTOMLEFT",0,-2); master:SetPoint("TOPRIGHT",header,"BOTTOMRIGHT",0,-2)
ARUI_Backdrop(master,ARUI_C.panel,ARUI_C.line)
local enableCheck=ARUI_Toggle(master,"自动危险圈",220,false)
enableCheck:SetPoint("LEFT",master,"LEFT",12,0)
enableCheck:SetScript("OnClick",function()
  local on=not this:GetChecked(); this:SetChecked(on)
  if type(AutoRange_SetEnabled)=="function" then AutoRange_SetEnabled(on,true) end
end)
local stateLabel=ARUI_Font(master,10,ARUI_C.muted,"RIGHT")
stateLabel:SetPoint("RIGHT",master,"RIGHT",-118,0); stateLabel:SetText("运行状态")
local stateText=ARUI_Font(master,11,ARUI_C.white,"RIGHT")
stateText:SetWidth(90); stateText:SetPoint("RIGHT",master,"RIGHT",-12,0)

local tabs=CreateFrame("Frame",nil,panel)
tabs:SetHeight(32); tabs:SetPoint("TOPLEFT",master,"BOTTOMLEFT",0,-2); tabs:SetPoint("TOPRIGHT",master,"BOTTOMRIGHT",0,-2)
ARUI_Backdrop(tabs,{0.30,0.33,0.40,1},ARUI_C.line)
SF.ui.tabBasic=ARUI_Button(tabs,"基础设置",104,24); SF.ui.tabBasic:SetPoint("LEFT",tabs,"LEFT",10,0)
SF.ui.tabM2=ARUI_Button(tabs,"M2素材库",104,24); SF.ui.tabM2:SetPoint("LEFT",SF.ui.tabBasic,"RIGHT",6,0)
SF.ui.tabEnemy=ARUI_Button(tabs,"敌人技能规则",132,24); SF.ui.tabEnemy:SetPoint("LEFT",SF.ui.tabM2,"RIGHT",6,0)
SF.ui.tabSelf=ARUI_Button(tabs,"自己的技能规则",136,24); SF.ui.tabSelf:SetPoint("LEFT",SF.ui.tabEnemy,"RIGHT",6,0)
SF.ui.tabWarning=ARUI_Button(tabs,"预警设置",104,24); SF.ui.tabWarning:SetPoint("LEFT",SF.ui.tabSelf,"RIGHT",6,0)
SF.ui.tabHint=ARUI_Font(tabs,9,ARUI_C.dim,"RIGHT"); SF.ui.tabHint:SetPoint("RIGHT",tabs,"RIGHT",-12,0); SF.ui.tabHint:SetText("")

SF.ui.pageHost=CreateFrame("Frame",nil,panel)
SF.ui.pageHost:SetPoint("TOPLEFT",tabs,"BOTTOMLEFT",0,-2); SF.ui.pageHost:SetPoint("BOTTOMRIGHT",panel,"BOTTOMRIGHT",0,0)

SF.ui.pageBasic=CreateFrame("Frame",nil,SF.ui.pageHost); SF.ui.pageBasic:SetAllPoints(SF.ui.pageHost)
SF.ui.pageM2=CreateFrame("Frame",nil,SF.ui.pageHost); SF.ui.pageM2:SetAllPoints(SF.ui.pageHost)
SF.ui.pageEnemy=CreateFrame("Frame",nil,SF.ui.pageHost); SF.ui.pageEnemy:SetAllPoints(SF.ui.pageHost)
SF.ui.pageSelf=CreateFrame("Frame",nil,SF.ui.pageHost); SF.ui.pageSelf:SetAllPoints(SF.ui.pageHost)
SF.ui.pageWarning=CreateFrame("Frame",nil,SF.ui.pageHost); SF.ui.pageWarning:SetAllPoints(SF.ui.pageHost)
local uiPage="BASIC"
local function SetUIPage(page)
  page=string.upper(tostring(page or "BASIC"))
  if page=="RULES" then page="ENEMY" end
  if page~="BASIC" and page~="M2" and page~="ENEMY" and page~="SELF" and page~="WARNING" then page="BASIC" end
  uiPage=page; AutoRangeDB.uiPage=page
  if page=="BASIC" then SF.ui.pageBasic:Show() else SF.ui.pageBasic:Hide() end
  if page=="M2" then SF.ui.pageM2:Show() else SF.ui.pageM2:Hide() end
  if page=="ENEMY" then SF.ui.pageEnemy:Show() else SF.ui.pageEnemy:Hide() end
  if page=="SELF" then SF.ui.pageSelf:Show() else SF.ui.pageSelf:Hide() end
  if page=="WARNING" then SF.ui.pageWarning:Show() else SF.ui.pageWarning:Hide() end
  SF.ui.tabBasic:SetBackdropColor(page=="BASIC" and 0.48 or ARUI_C.panel2[1],page=="BASIC" and 0.38 or ARUI_C.panel2[2],page=="BASIC" and 0.18 or ARUI_C.panel2[3],1)
  SF.ui.tabM2:SetBackdropColor(page=="M2" and 0.48 or ARUI_C.panel2[1],page=="M2" and 0.38 or ARUI_C.panel2[2],page=="M2" and 0.18 or ARUI_C.panel2[3],1)
  SF.ui.tabEnemy:SetBackdropColor(page=="ENEMY" and 0.48 or ARUI_C.panel2[1],page=="ENEMY" and 0.38 or ARUI_C.panel2[2],page=="ENEMY" and 0.18 or ARUI_C.panel2[3],1)
  SF.ui.tabSelf:SetBackdropColor(page=="SELF" and 0.48 or ARUI_C.panel2[1],page=="SELF" and 0.38 or ARUI_C.panel2[2],page=="SELF" and 0.18 or ARUI_C.panel2[3],1)
  SF.ui.tabWarning:SetBackdropColor(page=="WARNING" and 0.48 or ARUI_C.panel2[1],page=="WARNING" and 0.38 or ARUI_C.panel2[2],page=="WARNING" and 0.18 or ARUI_C.panel2[3],1)
  if type(Render)=="function" then Render() end
end
SF.ui.tabBasic:SetScript("OnClick",function() SetUIPage("BASIC") end)
SF.ui.tabM2:SetScript("OnClick",function() SetUIPage("M2") end)
SF.ui.tabEnemy:SetScript("OnClick",function() SetUIPage("ENEMY") end)
SF.ui.tabSelf:SetScript("OnClick",function() SetUIPage("SELF") end)
SF.ui.tabWarning:SetScript("OnClick",function() SetUIPage("WARNING"); Warn.RefreshMediaList(); Warn.SyncUI(); Warn.dirty=true; Warn.RefreshDanger() end)

-- BASIC PAGE
SF.ui.leftPanel=CreateFrame("Frame",nil,SF.ui.pageBasic)
SF.ui.leftPanel:SetWidth(305); SF.ui.leftPanel:SetPoint("TOPLEFT",SF.ui.pageBasic,"TOPLEFT",0,0); SF.ui.leftPanel:SetPoint("BOTTOMLEFT",SF.ui.pageBasic,"BOTTOMLEFT",0,0)
ARUI_Backdrop(SF.ui.leftPanel,{0.34,0.37,0.44,1},ARUI_C.line)
SF.ui.leftTitle=ARUI_Font(SF.ui.leftPanel,11,ARUI_C.muted,"LEFT")
SF.ui.leftTitle:SetPoint("TOPLEFT",SF.ui.leftPanel,"TOPLEFT",12,-12); SF.ui.leftTitle:SetText("危险圈来源")
SF.ui.enemyCheck=ARUI_Toggle(SF.ui.leftPanel,"敌人圈",270,false); SF.ui.enemyCheck:SetPoint("TOPLEFT",SF.ui.leftPanel,"TOPLEFT",12,-38)
SF.ui.enemyCheck:SetScript("OnClick",function() local on=not this:GetChecked(); this:SetChecked(on); SetEnemyCircle(on,true); if type(Render)=="function" then Render() end end)
SF.ui.friendCheck=ARUI_Toggle(SF.ui.leftPanel,"队友圈",270,false); SF.ui.friendCheck:SetPoint("TOPLEFT",SF.ui.leftPanel,"TOPLEFT",12,-74)
SF.ui.friendCheck:SetScript("OnClick",function() local on=not this:GetChecked(); this:SetChecked(on); SetFriendCircle(on,true); if type(Render)=="function" then Render() end end)
SF.ui.selfToggle=ARUI_Toggle(SF.ui.leftPanel,"自己的技能圈",270,false); SF.ui.selfToggle:SetPoint("TOPLEFT",SF.ui.leftPanel,"TOPLEFT",12,-110)
SF.ui.selfToggle:SetScript("OnClick",function() local on=not this:GetChecked(); this:SetChecked(on); SF.SetSelfEnabled(on,true); if type(Render)=="function" then Render() end end)
SF.ui.enemyLimitCheck=ARUI_Toggle(SF.ui.leftPanel,"敌人圈性能保护",270,false); SF.ui.enemyLimitCheck:SetPoint("TOPLEFT",SF.ui.leftPanel,"TOPLEFT",12,-146)
SF.ui.enemyLimitCheck:SetScript("OnClick",function() local on=not this:GetChecked(); this:SetChecked(on); SetEnemyLimitEnabled(on,true); if type(Render)=="function" then Render() end end)
SF.ui.enemyLimitLabel=ARUI_Font(SF.ui.leftPanel,10,ARUI_C.muted,"LEFT"); SF.ui.enemyLimitLabel:SetPoint("TOPLEFT",SF.ui.leftPanel,"TOPLEFT",14,-196); SF.ui.enemyLimitLabel:SetText("最大可见敌人圈")
SF.ui.enemyLimitEdit=CreateFrame("EditBox","AutoRangeEnemyLimitEdit",SF.ui.leftPanel,"InputBoxTemplate")
SF.ui.enemyLimitEdit:SetWidth(64); SF.ui.enemyLimitEdit:SetHeight(22); SF.ui.enemyLimitEdit:SetPoint("TOPRIGHT",SF.ui.leftPanel,"TOPRIGHT",-18,-190); SF.ui.enemyLimitEdit:SetAutoFocus(false); SF.ui.enemyLimitEdit:SetMaxLetters(4); SF.ui.enemyLimitEdit:SetText(tostring(enemyLimitMax))
SF.ui.enemyLimitEdit:SetScript("OnEnterPressed",function() local n=SetEnemyLimitMax(this:GetText(),true); this:SetText(tostring(n)); this:ClearFocus(); if type(Render)=="function" then Render() end end)
SF.ui.enemyLimitEdit:SetScript("OnEscapePressed",function() this:SetText(tostring(enemyLimitMax)); this:ClearFocus() end)
SF.ui.enemyLimitEdit:SetScript("OnEditFocusLost",function() local n=SetEnemyLimitMax(this:GetText(),false); this:SetText(tostring(n)) end)
SF.ui.enemyLimitHint=ARUI_Font(SF.ui.leftPanel,9,ARUI_C.dim,"LEFT"); SF.ui.enemyLimitHint:SetPoint("TOPLEFT",SF.ui.leftPanel,"TOPLEFT",14,-232); SF.ui.enemyLimitHint:SetText("默认关闭 · 仅限制敌人圈 · 1～9999")
SF.ui.refreshLabel=ARUI_Font(SF.ui.leftPanel,10,ARUI_C.muted,"LEFT"); SF.ui.refreshLabel:SetPoint("TOPLEFT",SF.ui.leftPanel,"TOPLEFT",14,-270); SF.ui.refreshLabel:SetText("危险圈刷新间隔(秒)")
SF.ui.refreshEdit=CreateFrame("EditBox","AutoRangeRefreshIntervalEdit",SF.ui.leftPanel,"InputBoxTemplate")
SF.ui.refreshEdit:SetWidth(64); SF.ui.refreshEdit:SetHeight(22); SF.ui.refreshEdit:SetPoint("TOPRIGHT",SF.ui.leftPanel,"TOPRIGHT",-18,-264); SF.ui.refreshEdit:SetAutoFocus(false); SF.ui.refreshEdit:SetMaxLetters(5); SF.ui.refreshEdit:SetText("0.20")
SF.refreshEditing=false
SF.ui.refreshEdit:SetScript("OnEditFocusGained",function() SF.refreshEditing=true end)
SF.ui.refreshEdit:SetScript("OnEnterPressed",function() local n=SF.SetRefreshInterval(this:GetText(),true); this:SetText(string.format("%.2f",n)); this:ClearFocus(); if type(Render)=="function" then Render() end end)
SF.ui.refreshEdit:SetScript("OnEscapePressed",function() this:SetText(string.format("%.2f",SF.refreshInterval or 0.20)); this:ClearFocus() end)
SF.ui.refreshEdit:SetScript("OnEditFocusLost",function() SF.refreshEditing=false; local n=SF.SetRefreshInterval(this:GetText(),false); this:SetText(string.format("%.2f",n)) end)
SF.ui.refreshHint=ARUI_Font(SF.ui.leftPanel,9,ARUI_C.dim,"LEFT"); SF.ui.refreshHint:SetPoint("TOPLEFT",SF.ui.leftPanel,"TOPLEFT",14,-306); SF.ui.refreshHint:SetText("默认 0.20 · 越小越快 · 范围 0.02～1.00")
SF.ui.rightPanel=CreateFrame("Frame",nil,SF.ui.pageBasic)
SF.ui.rightPanel:SetWidth(335); SF.ui.rightPanel:SetPoint("TOPRIGHT",SF.ui.pageBasic,"TOPRIGHT",0,0); SF.ui.rightPanel:SetPoint("BOTTOMRIGHT",SF.ui.pageBasic,"BOTTOMRIGHT",0,0)
ARUI_Backdrop(SF.ui.rightPanel,{0.34,0.37,0.44,1},ARUI_C.line)
SF.ui.rightTitle=ARUI_Font(SF.ui.rightPanel,11,ARUI_C.muted,"LEFT"); SF.ui.rightTitle:SetPoint("TOPLEFT",SF.ui.rightPanel,"TOPLEFT",12,-12); SF.ui.rightTitle:SetText("图腾圈")
SF.ui.totemCheck=ARUI_Toggle(SF.ui.rightPanel,"图腾圈",300,false); SF.ui.totemCheck:SetPoint("TOPLEFT",SF.ui.rightPanel,"TOPLEFT",12,-38)
SF.ui.totemCheck:SetScript("OnClick",function() local on=not this:GetChecked(); this:SetChecked(on); if on then SetTotemMode(totemLastMode or "SELF",true) else SetTotemMode("OFF",true) end; if type(Render)=="function" then Render() end end)
SF.ui.totemModeCaption=ARUI_Font(SF.ui.rightPanel,10,ARUI_C.muted,"LEFT"); SF.ui.totemModeCaption:SetPoint("TOPLEFT",SF.ui.rightPanel,"TOPLEFT",14,-94); SF.ui.totemModeCaption:SetText("来源范围")
SF.ui.totemModeBtn=ARUI_Button(SF.ui.rightPanel,"来源：仅自己",190,26); SF.ui.totemModeBtn:SetPoint("TOPRIGHT",SF.ui.rightPanel,"TOPRIGHT",-14,-84); SF.ui.totemModeBtn:SetScript("OnClick",function() CycleTotemMode(); if type(Render)=="function" then Render() end end)
SF.ui.totemStyleCaption=ARUI_Font(SF.ui.rightPanel,10,ARUI_C.muted,"LEFT"); SF.ui.totemStyleCaption:SetPoint("TOPLEFT",SF.ui.rightPanel,"TOPLEFT",14,-140); SF.ui.totemStyleCaption:SetText("M2样式")
SF.ui.totemStyleBtn=ARUI_Button(SF.ui.rightPanel,"M2：自动",190,26); SF.ui.totemStyleBtn:SetPoint("TOPRIGHT",SF.ui.rightPanel,"TOPRIGHT",-14,-130); SF.ui.totemStyleBtn:SetScript("OnClick",function() if SF.totemUnifiedStyle then CycleTotemVisualStyle(); if type(Render)=="function" then Render() end end end)
SF.ui.totemCategoryTitle=ARUI_Font(SF.ui.rightPanel,10,ARUI_C.muted,"LEFT"); SF.ui.totemCategoryTitle:SetPoint("TOPLEFT",SF.ui.rightPanel,"TOPLEFT",14,-180); SF.ui.totemCategoryTitle:SetText("图腾分类样式")
SF.ui.totemAllSameBtn=ARUI_Button(SF.ui.rightPanel,"全部一个颜色：开",307,26); SF.ui.totemAllSameBtn:SetPoint("TOPLEFT",SF.ui.rightPanel,"TOPLEFT",14,-198); SF.ui.totemAllSameBtn:SetScript("OnClick",function() SF.SetTotemUnifiedStyle(not SF.totemUnifiedStyle,true); if type(Render)=="function" then Render() end end)
SF.ui.totemFireStyleBtn=ARUI_Button(SF.ui.rightPanel,"火焰图腾：火焰",149,26); SF.ui.totemFireStyleBtn:SetPoint("TOPLEFT",SF.ui.rightPanel,"TOPLEFT",14,-232); SF.ui.totemFireStyleBtn:SetScript("OnClick",function() if not SF.totemUnifiedStyle then SF.CycleTotemCategoryStyle("FIRE"); if type(Render)=="function" then Render() end end end)
SF.ui.totemEarthStyleBtn=ARUI_Button(SF.ui.rightPanel,"大地图腾：锯齿",149,26); SF.ui.totemEarthStyleBtn:SetPoint("LEFT",SF.ui.totemFireStyleBtn,"RIGHT",9,0); SF.ui.totemEarthStyleBtn:SetScript("OnClick",function() if not SF.totemUnifiedStyle then SF.CycleTotemCategoryStyle("EARTH"); if type(Render)=="function" then Render() end end end)
SF.ui.totemWaterStyleBtn=ARUI_Button(SF.ui.rightPanel,"水之图腾：白色",149,26); SF.ui.totemWaterStyleBtn:SetPoint("TOPLEFT",SF.ui.rightPanel,"TOPLEFT",14,-266); SF.ui.totemWaterStyleBtn:SetScript("OnClick",function() if not SF.totemUnifiedStyle then SF.CycleTotemCategoryStyle("WATER"); if type(Render)=="function" then Render() end end end)
SF.ui.totemAirStyleBtn=ARUI_Button(SF.ui.rightPanel,"空气图腾：标准",149,26); SF.ui.totemAirStyleBtn:SetPoint("LEFT",SF.ui.totemWaterStyleBtn,"RIGHT",9,0); SF.ui.totemAirStyleBtn:SetScript("OnClick",function() if not SF.totemUnifiedStyle then SF.CycleTotemCategoryStyle("AIR"); if type(Render)=="function" then Render() end end end)
SF.ui.totemCustomBtn=ARUI_Button(SF.ui.rightPanel,"自定义图腾：设置（最高优先）",307,26); SF.ui.totemCustomBtn:SetPoint("TOPLEFT",SF.ui.rightPanel,"TOPLEFT",14,-300); SF.ui.totemCustomBtn:SetScript("OnClick",function() SF.OpenTotemCustomPopup() end)
SF.ui.totemCategoryHint=ARUI_Font(SF.ui.rightPanel,9,ARUI_C.dim,"LEFT"); SF.ui.totemCategoryHint:SetPoint("TOPLEFT",SF.ui.rightPanel,"TOPLEFT",14,-336); SF.ui.totemCategoryHint:SetText("自定义图腾命中后，码数和样式优先于自动/统一/分类设置。")

-- Stage2.29 custom totem editor popup. Replaces the old OTHER-category style button.
SF.ui.totemCustomPopup=CreateFrame("Frame",nil,panel); SF.ui.totemCustomPopup:SetWidth(430); SF.ui.totemCustomPopup:SetHeight(286); SF.ui.totemCustomPopup:SetPoint("CENTER",panel,"CENTER",0,0); SF.ui.totemCustomPopup:SetFrameStrata("DIALOG"); SF.ui.totemCustomPopup:SetFrameLevel(panel:GetFrameLevel()+40); ARUI_Backdrop(SF.ui.totemCustomPopup,{0.18,0.20,0.25,0.99},ARUI_C.line); SF.ui.totemCustomPopup:Hide()
SF.ui.totemCustomTitle=ARUI_Font(SF.ui.totemCustomPopup,12,ARUI_C.muted,"LEFT"); SF.ui.totemCustomTitle:SetPoint("TOPLEFT",SF.ui.totemCustomPopup,"TOPLEFT",12,-12); SF.ui.totemCustomTitle:SetText("自定义图腾（最高优先）")
SF.ui.totemCustomClose=ARUI_Button(SF.ui.totemCustomPopup,"关闭",52,22); SF.ui.totemCustomClose:SetPoint("TOPRIGHT",SF.ui.totemCustomPopup,"TOPRIGHT",-10,-8); SF.ui.totemCustomClose:SetScript("OnClick",function() SF.ui.totemCustomPopup:Hide() end)
SF.ui.totemCustomNameLab=ARUI_Font(SF.ui.totemCustomPopup,9,ARUI_C.dim,"LEFT"); SF.ui.totemCustomNameLab:SetPoint("TOPLEFT",SF.ui.totemCustomPopup,"TOPLEFT",12,-48); SF.ui.totemCustomNameLab:SetText("图腾名字")
SF.ui.totemCustomName=CreateFrame("EditBox","AutoRangeTotemCustomNameEdit",SF.ui.totemCustomPopup,"InputBoxTemplate"); SF.ui.totemCustomName:SetWidth(132); SF.ui.totemCustomName:SetHeight(22); SF.ui.totemCustomName:SetPoint("TOPLEFT",SF.ui.totemCustomPopup,"TOPLEFT",12,-62); SF.ui.totemCustomName:SetAutoFocus(false); SF.ui.totemCustomName:SetMaxLetters(64)
SF.ui.totemCustomRadiusLab=ARUI_Font(SF.ui.totemCustomPopup,9,ARUI_C.dim,"LEFT"); SF.ui.totemCustomRadiusLab:SetPoint("LEFT",SF.ui.totemCustomName,"RIGHT",10,13); SF.ui.totemCustomRadiusLab:SetText("码数")
SF.ui.totemCustomRadius=CreateFrame("EditBox","AutoRangeTotemCustomRadiusEdit",SF.ui.totemCustomPopup,"InputBoxTemplate"); SF.ui.totemCustomRadius:SetWidth(48); SF.ui.totemCustomRadius:SetHeight(22); SF.ui.totemCustomRadius:SetPoint("LEFT",SF.ui.totemCustomName,"RIGHT",10,0); SF.ui.totemCustomRadius:SetAutoFocus(false); SF.ui.totemCustomRadius:SetMaxLetters(6)
SF.ui.totemCustomStyleBtn=ARUI_Button(SF.ui.totemCustomPopup,"样式：自动尺寸",112,22); SF.ui.totemCustomStyleBtn:SetPoint("LEFT",SF.ui.totemCustomRadius,"RIGHT",10,0); SF.ui.totemCustomStyleBtn:SetScript("OnClick",function() SF.CycleTotemCustomEditStyle() end)
SF.ui.totemCustomSave=ARUI_Button(SF.ui.totemCustomPopup,"添加/更新",82,22); SF.ui.totemCustomSave:SetPoint("LEFT",SF.ui.totemCustomStyleBtn,"RIGHT",8,0); SF.ui.totemCustomSave:SetScript("OnClick",function() if SF.UpsertTotemCustomRule(SF.ui.totemCustomName:GetText(),SF.ui.totemCustomRadius:GetText(),SF.totemCustomEditStyle,true) then SF.RenderTotemCustomPopup() end end)
SF.ui.totemCustomHint=ARUI_Font(SF.ui.totemCustomPopup,9,ARUI_C.dim,"LEFT"); SF.ui.totemCustomHint:SetPoint("TOPLEFT",SF.ui.totemCustomPopup,"TOPLEFT",12,-94); SF.ui.totemCustomHint:SetText("名字按游戏内图腾技能名精确匹配；命中后范围和样式覆盖全部自动规则。")
SF.ui.totemCustomRows={}
do
  local i,row
  for i=1,(SF.totemCustomPageSize or 5) do
    row={}
    row.select=ARUI_Button(SF.ui.totemCustomPopup,"",344,22); row.select:SetPoint("TOPLEFT",SF.ui.totemCustomPopup,"TOPLEFT",12,-118-(i-1)*26)
    row.delete=ARUI_Button(SF.ui.totemCustomPopup,"删除",54,22); row.delete:SetPoint("LEFT",row.select,"RIGHT",8,0)
    row.select.rowIndex=i; row.delete.rowIndex=i
    row.select:SetScript("OnClick",function() local r=SF.ui.totemCustomRows[this.rowIndex]; if r and r.key then SF.SelectTotemCustomRule(r.key) end end)
    row.delete:SetScript("OnClick",function() local r=SF.ui.totemCustomRows[this.rowIndex]; if r and r.key then SF.DeleteTotemCustomRule(r.key,true); SF.RenderTotemCustomPopup() end end)
    SF.ui.totemCustomRows[i]=row
  end
end
SF.ui.totemCustomPrev=ARUI_Button(SF.ui.totemCustomPopup,"上一页",58,20); SF.ui.totemCustomPrev:SetPoint("BOTTOMLEFT",SF.ui.totemCustomPopup,"BOTTOMLEFT",12,10); SF.ui.totemCustomPrev:SetScript("OnClick",function() SF.totemCustomPage=SF.totemCustomPage-1; SF.RenderTotemCustomPopup() end)
SF.ui.totemCustomNext=ARUI_Button(SF.ui.totemCustomPopup,"下一页",58,20); SF.ui.totemCustomNext:SetPoint("LEFT",SF.ui.totemCustomPrev,"RIGHT",8,0); SF.ui.totemCustomNext:SetScript("OnClick",function() SF.totemCustomPage=SF.totemCustomPage+1; SF.RenderTotemCustomPopup() end)
SF.ui.totemCustomPageText=ARUI_Font(SF.ui.totemCustomPopup,9,ARUI_C.white,"LEFT"); SF.ui.totemCustomPageText:SetPoint("LEFT",SF.ui.totemCustomNext,"RIGHT",10,0); SF.ui.totemCustomPageText:SetText("1/1 · 共0条")

-- ENEMY RULES PAGE
SF.ui.enemyPanel=CreateFrame("Frame",nil,SF.ui.pageEnemy); SF.ui.enemyPanel:SetAllPoints(SF.ui.pageEnemy)
ARUI_Backdrop(SF.ui.enemyPanel,{0.34,0.37,0.44,1},ARUI_C.line)
SF.ui.enemyTitle=ARUI_Font(SF.ui.enemyPanel,12,ARUI_C.muted,"LEFT"); SF.ui.enemyTitle:SetPoint("TOPLEFT",SF.ui.enemyPanel,"TOPLEFT",14,-12); SF.ui.enemyTitle:SetText("敌人技能规则")
-- Enemy-rule semantic controls live in the header center. Keep direct panel anchors
-- for WoW 1.12 safety; do not chain clickable controls to child FontStrings.
SF.ui.semanticLabel=ARUI_Font(SF.ui.enemyPanel,11,ARUI_C.white,"LEFT")
SF.ui.semanticLabel:SetPoint("TOPLEFT",SF.ui.enemyPanel,"TOPLEFT",176,-14); SF.ui.semanticLabel:SetText("过滤无害 Buff 圈")
SF.ui.enemyDecision=ARUI_Button(SF.ui.enemyPanel,"判定：自动",88,22); SF.ui.enemyDecision:SetPoint("TOPLEFT",SF.ui.enemyPanel,"TOPLEFT",326,-10)
SF.ui.semanticCheck=ARUI_Toggle(SF.ui.enemyPanel,"",28,false); SF.ui.semanticCheck:SetPoint("TOPLEFT",SF.ui.enemyPanel,"TOPLEFT",428,-7)
SF.ui.semanticCheck:SetScript("OnClick",function() local on=not this:GetChecked(); this:SetChecked(on); SF.SetSemanticFilter(on,true); if type(Render)=="function" then Render() end end)
SF.ui.enemyDecision:SetScript("OnClick",function()
  local mode=SF.CycleEnemyDecision()
  if mode=="SHOW" then
    Chat("判定：强显 — 添加/更新敌人技能规则时，强制显示该技能的危险圈。")
  elseif mode=="HIDE" then
    Chat("判定：强隐 — 添加/更新敌人技能规则时，强制隐藏该技能的危险圈。")
  else
    Chat("判定：自动 — 添加/更新敌人技能规则时使用自动语义判断；明确无害 Buff 不画圈，危险或未知仍显示。")
  end
  if type(Render)=="function" then Render() end
end)
SF.ui.enemyModeBtn=ARUI_Button(SF.ui.enemyPanel,"模式：全部技能",150,22); SF.ui.enemyModeBtn:SetPoint("TOPRIGHT",SF.ui.enemyPanel,"TOPRIGHT",-14,-10); SF.ui.enemyModeBtn:SetScript("OnClick",function() SF.CycleEnemyMode(); if type(Render)=="function" then Render() end end)
SF.ui.enemyCaption=ARUI_Font(SF.ui.enemyPanel,9,ARUI_C.dim,"LEFT"); SF.ui.enemyCaption:SetPoint("TOPLEFT",SF.ui.enemyPanel,"TOPLEFT",14,-52); SF.ui.enemyCaption:SetText("SpellID              半径(0=AUTO)              单技能M2")
SF.ui.enemyEdit=CreateFrame("EditBox","AutoRangeEnemySkillEdit",SF.ui.enemyPanel,"InputBoxTemplate"); SF.ui.enemyEdit:SetWidth(112); SF.ui.enemyEdit:SetHeight(22); SF.ui.enemyEdit:SetPoint("TOPLEFT",SF.ui.enemyPanel,"TOPLEFT",14,-66); SF.ui.enemyEdit:SetAutoFocus(false); SF.ui.enemyEdit:SetMaxLetters(80)
SF.ui.enemyRadius=CreateFrame("EditBox","AutoRangeEnemyRadiusEdit",SF.ui.enemyPanel,"InputBoxTemplate"); SF.ui.enemyRadius:SetWidth(72); SF.ui.enemyRadius:SetHeight(22); SF.ui.enemyRadius:SetPoint("LEFT",SF.ui.enemyEdit,"RIGHT",18,0); SF.ui.enemyRadius:SetAutoFocus(false); SF.ui.enemyRadius:SetMaxLetters(8); SF.ui.enemyRadius:SetText("0")
SF.ui.enemyStyle=ARUI_Button(SF.ui.enemyPanel,"标准",128,22); SF.ui.enemyStyle:SetPoint("LEFT",SF.ui.enemyRadius,"RIGHT",18,0)
SF.ui.enemyAdd=ARUI_Button(SF.ui.enemyPanel,"添加 / 更新",96,22); SF.ui.enemyAdd:SetPoint("LEFT",SF.ui.enemyStyle,"RIGHT",16,0)

SF.ui.modeNormal=ARUI_Toggle(SF.ui.enemyPanel,"普通",118,true); SF.ui.modeNormal:SetPoint("TOPLEFT",SF.ui.enemyPanel,"TOPLEFT",14,-108)
SF.ui.modeNormalText=ARUI_Font(SF.ui.enemyPanel,10,ARUI_C.white,"LEFT"); SF.ui.modeNormalText:SetPoint("LEFT",SF.ui.modeNormal,"RIGHT",18,0); SF.ui.modeNormalText:SetText("= 踩中就危险")
SF.ui.modeRange=ARUI_Toggle(SF.ui.enemyPanel,"视野范围",118,false); SF.ui.modeRange:SetPoint("TOPLEFT",SF.ui.enemyPanel,"TOPLEFT",14,-140)
SF.ui.modeRangeText=ARUI_Font(SF.ui.enemyPanel,10,ARUI_C.white,"LEFT"); SF.ui.modeRangeText:SetPoint("LEFT",SF.ui.modeRange,"RIGHT",18,0); SF.ui.modeRangeText:SetText("= 踩中以后，再看能不能被挡")
SF.ui.modeGlobal=ARUI_Toggle(SF.ui.enemyPanel,"视野全局",118,false); SF.ui.modeGlobal:SetPoint("TOPLEFT",SF.ui.enemyPanel,"TOPLEFT",14,-172)
SF.ui.modeGlobalText=ARUI_Font(SF.ui.enemyPanel,10,ARUI_C.white,"LEFT"); SF.ui.modeGlobalText:SetPoint("LEFT",SF.ui.modeGlobal,"RIGHT",18,0); SF.ui.modeGlobalText:SetText("= 不看踩没踩，只看能不能被挡")
SF.ui.modeHit=ARUI_Toggle(SF.ui.enemyPanel,"全局必中",118,false); SF.ui.modeHit:SetPoint("TOPLEFT",SF.ui.enemyPanel,"TOPLEFT",14,-204)
SF.ui.modeHitText=ARUI_Font(SF.ui.enemyPanel,10,ARUI_C.white,"LEFT"); SF.ui.modeHitText:SetPoint("LEFT",SF.ui.modeHit,"RIGHT",18,0); SF.ui.modeHitText:SetText("= 不看范围，也不看阻挡，放了就危险")
local function SelectEnemyRuleMode(mode)
  SF.SetEnemyEditMode(mode)
  if type(Render)=="function" then Render() end
end
SF.ui.modeNormal:SetScript("OnClick",function() SelectEnemyRuleMode("NORMAL") end)
SF.ui.modeRange:SetScript("OnClick",function() SelectEnemyRuleMode("VISION_RANGE") end)
SF.ui.modeGlobal:SetScript("OnClick",function() SelectEnemyRuleMode("VISION_GLOBAL") end)
SF.ui.modeHit:SetScript("OnClick",function() SelectEnemyRuleMode("GLOBAL_HIT") end)

SF.ui.enemyRows=SF.CreateRuleRows(SF.ui.enemyPanel,"ENEMY",-250)
SF.ui.enemyPageText=ARUI_Font(SF.ui.enemyPanel,8,ARUI_C.dim,"LEFT"); SF.ui.enemyPageText:SetWidth(590); SF.ui.enemyPageText:SetPoint("BOTTOMLEFT",SF.ui.enemyPanel,"BOTTOMLEFT",170,16); SF.ui.enemyPageText:SetText("页 1/1 · 共 0 条")
SF.ui.enemyPrev=ARUI_Button(SF.ui.enemyPanel,"上一页",64,20); SF.ui.enemyPrev:SetPoint("BOTTOMLEFT",SF.ui.enemyPanel,"BOTTOMLEFT",14,12)
SF.ui.enemyNext=ARUI_Button(SF.ui.enemyPanel,"下一页",64,20); SF.ui.enemyNext:SetPoint("LEFT",SF.ui.enemyPrev,"RIGHT",8,0)
SF.ui.enemyHint=ARUI_Font(SF.ui.enemyPanel,8,ARUI_C.dim,"RIGHT"); SF.ui.enemyHint:SetWidth(370); SF.ui.enemyHint:SetPoint("BOTTOMRIGHT",SF.ui.enemyPanel,"BOTTOMRIGHT",-12,15); SF.ui.enemyHint:SetText("4种模式互斥，只能选择一个 · 排除指定 = 黑名单模式")
SF.ui.enemyStyle:SetScript("OnClick",function() SF.CycleEditStyle("ENEMY"); if type(Render)=="function" then Render() end end)
SF.ui.enemyAdd:SetScript("OnClick",function() SF.UpsertRules("ENEMY",SF.ui.enemyEdit:GetText(),SF.ui.enemyRadius:GetText(),SF.enemyEditStyle,true); if type(Render)=="function" then Render() end end)
SF.ui.enemyPrev:SetScript("OnClick",function() SF.CyclePage("ENEMY",-1); if type(Render)=="function" then Render() end end)
SF.ui.enemyNext:SetScript("OnClick",function() SF.CyclePage("ENEMY",1); if type(Render)=="function" then Render() end end)
SF.ui.enemyEdit:SetScript("OnEnterPressed",function() SF.UpsertRules("ENEMY",this:GetText(),SF.ui.enemyRadius:GetText(),SF.enemyEditStyle,true); this:ClearFocus(); if type(Render)=="function" then Render() end end); SF.ui.enemyEdit:SetScript("OnEscapePressed",function() this:ClearFocus() end); SF.ui.enemyRadius:SetScript("OnEscapePressed",function() this:ClearFocus() end)

-- SELF RULES PAGE
SF.ui.selfPanel=CreateFrame("Frame",nil,SF.ui.pageSelf); SF.ui.selfPanel:SetAllPoints(SF.ui.pageSelf)
ARUI_Backdrop(SF.ui.selfPanel,{0.34,0.37,0.44,1},ARUI_C.line)
SF.ui.selfTitle=ARUI_Font(SF.ui.selfPanel,12,ARUI_C.muted,"LEFT"); SF.ui.selfTitle:SetPoint("TOPLEFT",SF.ui.selfPanel,"TOPLEFT",14,-12); SF.ui.selfTitle:SetText("自己的技能规则")
SF.ui.selfCaption=ARUI_Font(SF.ui.selfPanel,9,ARUI_C.dim,"LEFT"); SF.ui.selfCaption:SetPoint("TOPLEFT",SF.ui.selfPanel,"TOPLEFT",14,-52); SF.ui.selfCaption:SetText("SpellID              半径(0=AUTO)              单技能M2")
SF.ui.selfEdit=CreateFrame("EditBox","AutoRangeSelfSkillEdit",SF.ui.selfPanel,"InputBoxTemplate"); SF.ui.selfEdit:SetWidth(112); SF.ui.selfEdit:SetHeight(22); SF.ui.selfEdit:SetPoint("TOPLEFT",SF.ui.selfPanel,"TOPLEFT",14,-66); SF.ui.selfEdit:SetAutoFocus(false); SF.ui.selfEdit:SetMaxLetters(80)
SF.ui.selfRadius=CreateFrame("EditBox","AutoRangeSelfRadiusEdit",SF.ui.selfPanel,"InputBoxTemplate"); SF.ui.selfRadius:SetWidth(72); SF.ui.selfRadius:SetHeight(22); SF.ui.selfRadius:SetPoint("LEFT",SF.ui.selfEdit,"RIGHT",18,0); SF.ui.selfRadius:SetAutoFocus(false); SF.ui.selfRadius:SetMaxLetters(8); SF.ui.selfRadius:SetText("0")
SF.ui.selfStyle=ARUI_Button(SF.ui.selfPanel,"白色",128,22); SF.ui.selfStyle:SetPoint("LEFT",SF.ui.selfRadius,"RIGHT",18,0)
SF.ui.selfAdd=ARUI_Button(SF.ui.selfPanel,"添加 / 更新",96,22); SF.ui.selfAdd:SetPoint("LEFT",SF.ui.selfStyle,"RIGHT",16,0)
SF.ui.selfClear=ARUI_Button(SF.ui.selfPanel,"清空所有",78,22); SF.ui.selfClear:SetPoint("LEFT",SF.ui.selfAdd,"RIGHT",8,0)
SF.ui.selfRows=SF.CreateRuleRows(SF.ui.selfPanel,"SELF",-112)
SF.ui.selfPageText=ARUI_Font(SF.ui.selfPanel,8,ARUI_C.dim,"LEFT"); SF.ui.selfPageText:SetWidth(590); SF.ui.selfPageText:SetPoint("BOTTOMLEFT",SF.ui.selfPanel,"BOTTOMLEFT",170,16); SF.ui.selfPageText:SetText("页 1/1 · 共 0 条")
SF.ui.selfPrev=ARUI_Button(SF.ui.selfPanel,"上一页",64,20); SF.ui.selfPrev:SetPoint("BOTTOMLEFT",SF.ui.selfPanel,"BOTTOMLEFT",14,12)
SF.ui.selfNext=ARUI_Button(SF.ui.selfPanel,"下一页",64,20); SF.ui.selfNext:SetPoint("LEFT",SF.ui.selfPrev,"RIGHT",8,0)
SF.ui.selfHint=ARUI_Font(SF.ui.selfPanel,8,ARUI_C.dim,"RIGHT"); SF.ui.selfHint:SetWidth(300); SF.ui.selfHint:SetPoint("BOTTOMRIGHT",SF.ui.selfPanel,"BOTTOMRIGHT",-12,15); SF.ui.selfHint:SetText("0=AUTO · 添加后自动保存")
SF.ui.selfStyle:SetScript("OnClick",function() SF.CycleEditStyle("SELF"); if type(Render)=="function" then Render() end end)
SF.ui.selfAdd:SetScript("OnClick",function() SF.UpsertRules("SELF",SF.ui.selfEdit:GetText(),SF.ui.selfRadius:GetText(),SF.selfEditStyle,true); if type(Render)=="function" then Render() end end)
SF.ui.selfClear:SetScript("OnClick",function() SF.ClearRules("SELF",true); SF.ClearSelectedRule("SELF"); if type(Render)=="function" then Render() end end)
SF.ui.selfPrev:SetScript("OnClick",function() SF.CyclePage("SELF",-1); if type(Render)=="function" then Render() end end)
SF.ui.selfNext:SetScript("OnClick",function() SF.CyclePage("SELF",1); if type(Render)=="function" then Render() end end)
SF.ui.selfEdit:SetScript("OnEnterPressed",function() SF.UpsertRules("SELF",this:GetText(),SF.ui.selfRadius:GetText(),SF.selfEditStyle,true); this:ClearFocus(); if type(Render)=="function" then Render() end end); SF.ui.selfEdit:SetScript("OnEscapePressed",function() this:ClearFocus() end); SF.ui.selfRadius:SetScript("OnEscapePressed",function() this:ClearFocus() end)

-- WARNING PAGE
SF.ui.warningPanel=CreateFrame("Frame",nil,SF.ui.pageWarning); SF.ui.warningPanel:SetAllPoints(SF.ui.pageWarning); ARUI_Backdrop(SF.ui.warningPanel,{0.34,0.37,0.44,1},ARUI_C.line)
Warn.ui.panel=SF.ui.warningPanel
Warn.ui.title=ARUI_Font(SF.ui.warningPanel,12,ARUI_C.muted,"LEFT"); Warn.ui.title:SetPoint("TOPLEFT",SF.ui.warningPanel,"TOPLEFT",14,-14); Warn.ui.title:SetText("危险预警 TGA")
Warn.ui.enabled=ARUI_Toggle(SF.ui.warningPanel,"启用危险预警",250,true); Warn.ui.enabled:SetPoint("TOPLEFT",SF.ui.warningPanel,"TOPLEFT",14,-43)
Warn.ui.enabled:SetScript("OnClick",function() local on=not this:GetChecked(); this:SetChecked(on); Warn.SetEnabled(on,true) end)
Warn.ui.modeLabel=ARUI_Font(SF.ui.warningPanel,10,ARUI_C.muted,"LEFT"); Warn.ui.modeLabel:SetPoint("TOPLEFT",SF.ui.warningPanel,"TOPLEFT",14,-78); Warn.ui.modeLabel:SetText("预警模式")
Warn.ui.modeSingle=ARUI_Button(SF.ui.warningPanel,"● 单图预警",94,22); Warn.ui.modeSingle:SetPoint("TOPLEFT",SF.ui.warningPanel,"TOPLEFT",82,-72); Warn.ui.modeSingle:SetScript("OnClick",function() Warn.SetMode("SINGLE",true) end)
Warn.ui.modeDirection2=ARUI_Button(SF.ui.warningPanel,"○ 2方向预警",104,22); Warn.ui.modeDirection2:SetPoint("LEFT",Warn.ui.modeSingle,"RIGHT",8,0); Warn.ui.modeDirection2:SetScript("OnClick",function() Warn.SetMode("DIRECTION2",true) end)
Warn.ui.modeHint=ARUI_Font(SF.ui.warningPanel,8,ARUI_C.dim,"LEFT"); Warn.ui.modeHint:SetWidth(330); Warn.ui.modeHint:SetPoint("LEFT",Warn.ui.modeDirection2,"RIGHT",10,0); Warn.ui.modeHint:SetText("单图：危险图/安全图双素材；安全图仅在可阻挡模式且开启时显示")
Warn.ui.media=ARUI_Font(SF.ui.warningPanel,9,ARUI_C.white,"LEFT"); Warn.ui.media:SetWidth(620); Warn.ui.media:SetPoint("TOPLEFT",SF.ui.warningPanel,"TOPLEFT",14,-112); Warn.ui.media:SetText("当前素材：未扫描")
Warn.ui.singleDanger=ARUI_Button(SF.ui.warningPanel,"危险：未设置",132,24); Warn.ui.singleDanger:SetPoint("TOPLEFT",SF.ui.warningPanel,"TOPLEFT",14,-133); Warn.ui.singleDanger:SetScript("OnClick",function() Warn.ToggleMediaMenu("DANGER",Warn.ui.singleDanger) end)
Warn.ui.singleSafe=ARUI_Button(SF.ui.warningPanel,"安全：未设置",132,24); Warn.ui.singleSafe:SetPoint("LEFT",Warn.ui.singleDanger,"RIGHT",8,0); Warn.ui.singleSafe:SetScript("OnClick",function() Warn.ToggleMediaMenu("SAFE",Warn.ui.singleSafe) end)
Warn.ui.safeToggle=ARUI_Button(SF.ui.warningPanel,"显示安全图：开",116,24); Warn.ui.safeToggle:SetPoint("LEFT",Warn.ui.singleSafe,"RIGHT",8,0); Warn.ui.safeToggle:SetScript("OnClick",function() Warn.showSafeImage=not Warn.showSafeImage; Warn.Save(); Warn.SyncUI(); Warn.UpdateVisibility(); Warn.dirty=true; Chat("显示安全图："..(Warn.showSafeImage and "开启" or "关闭")) end)
Warn.ui.mediaRefresh=ARUI_Button(SF.ui.warningPanel,"刷新素材",82,24); Warn.ui.mediaRefresh:SetPoint("LEFT",Warn.ui.safeToggle,"RIGHT",8,0); Warn.ui.mediaRefresh:SetScript("OnClick",function() Warn.RefreshMediaOnly() end)
Warn.ui.dirLeft=ARUI_Button(SF.ui.warningPanel,"左：未设置",132,24); Warn.ui.dirLeft:SetPoint("TOPLEFT",SF.ui.warningPanel,"TOPLEFT",14,-133); Warn.ui.dirLeft:SetScript("OnClick",function() Warn.ToggleMediaMenu("LEFT",Warn.ui.dirLeft) end)
Warn.ui.dirRight=ARUI_Button(SF.ui.warningPanel,"右：未设置",132,24); Warn.ui.dirRight:SetPoint("LEFT",Warn.ui.dirLeft,"RIGHT",8,0); Warn.ui.dirRight:SetScript("OnClick",function() Warn.ToggleMediaMenu("RIGHT",Warn.ui.dirRight) end)
Warn.ui.mediaHint=ARUI_Font(SF.ui.warningPanel,9,ARUI_C.dim,"LEFT"); Warn.ui.mediaHint:SetWidth(1); Warn.ui.mediaHint:SetPoint("TOPLEFT",SF.ui.warningPanel,"TOPLEFT",14,-160); Warn.ui.mediaHint:SetText(""); Warn.ui.mediaHint:Hide()
Warn.ui.mediaMenu=CreateFrame("Frame",nil,SF.ui.warningPanel); Warn.ui.mediaMenu:SetWidth(210); Warn.ui.mediaMenu:SetHeight(240); ARUI_Backdrop(Warn.ui.mediaMenu,{0.08,0.09,0.11,0.98},ARUI_C.line); Warn.ui.mediaMenu:SetFrameLevel(SF.ui.warningPanel:GetFrameLevel()+24); Warn.ui.mediaMenu:Hide()
Warn.ui.mediaMenuTitle=ARUI_Font(Warn.ui.mediaMenu,10,ARUI_C.muted,"LEFT"); Warn.ui.mediaMenuTitle:SetPoint("TOPLEFT",Warn.ui.mediaMenu,"TOPLEFT",8,-8); Warn.ui.mediaMenuTitle:SetText("素材选择")
Warn.ui.mediaMenuButtons={}
do
  local i,b
  for i=1,(Warn.mediaMenuPageSize or 10) do
    b=ARUI_Button(Warn.ui.mediaMenu,"",194,18)
    b:SetPoint("TOPLEFT",Warn.ui.mediaMenu,"TOPLEFT",8,-26-(i-1)*20)
    b:SetScript("OnClick",function() if this.mediaName and Warn.mediaMenuSlot then Warn.SetMediaSlot(Warn.mediaMenuSlot,this.mediaName,true); Warn.CloseMediaMenu() end end)
    Warn.ui.mediaMenuButtons[i]=b
  end
end
Warn.ui.mediaMenuPrev=ARUI_Button(Warn.ui.mediaMenu,"上一页",56,18); Warn.ui.mediaMenuPrev:SetPoint("BOTTOMLEFT",Warn.ui.mediaMenu,"BOTTOMLEFT",8,8); Warn.ui.mediaMenuPrev:SetScript("OnClick",function() Warn.mediaMenuPage=(Warn.mediaMenuPage or 1)-1; Warn.RenderMediaMenu() end)
Warn.ui.mediaMenuPageText=ARUI_Font(Warn.ui.mediaMenu,9,ARUI_C.white,"CENTER"); Warn.ui.mediaMenuPageText:SetWidth(50); Warn.ui.mediaMenuPageText:SetPoint("LEFT",Warn.ui.mediaMenuPrev,"RIGHT",8,0); Warn.ui.mediaMenuPageText:SetText("1/1")
Warn.ui.mediaMenuNext=ARUI_Button(Warn.ui.mediaMenu,"下一页",56,18); Warn.ui.mediaMenuNext:SetPoint("LEFT",Warn.ui.mediaMenuPageText,"RIGHT",8,0); Warn.ui.mediaMenuNext:SetScript("OnClick",function() Warn.mediaMenuPage=(Warn.mediaMenuPage or 1)+1; Warn.RenderMediaMenu() end)
Warn.ui.mediaMenuClose=ARUI_Button(Warn.ui.mediaMenu,"关闭",44,18); Warn.ui.mediaMenuClose:SetPoint("LEFT",Warn.ui.mediaMenuNext,"RIGHT",8,0); Warn.ui.mediaMenuClose:SetScript("OnClick",function() Warn.CloseMediaMenu() end)
Warn.ui.posLabel=ARUI_Font(SF.ui.warningPanel,10,ARUI_C.muted,"LEFT"); Warn.ui.posLabel:SetPoint("TOPLEFT",SF.ui.warningPanel,"TOPLEFT",14,-174); Warn.ui.posLabel:SetText("屏幕位置 / 显示尺寸")
Warn.ui.xLab=ARUI_Font(SF.ui.warningPanel,10,ARUI_C.white,"LEFT"); Warn.ui.xLab:SetPoint("TOPLEFT",SF.ui.warningPanel,"TOPLEFT",14,-200); Warn.ui.xLab:SetText("X")
Warn.ui.x=CreateFrame("EditBox","AutoRangeWarningXEdit",SF.ui.warningPanel,"InputBoxTemplate"); Warn.ui.x:SetWidth(62); Warn.ui.x:SetHeight(22); Warn.ui.x:SetPoint("LEFT",Warn.ui.xLab,"RIGHT",7,0); Warn.ui.x:SetAutoFocus(false); Warn.ui.x:SetMaxLetters(7)
Warn.ui.yLab=ARUI_Font(SF.ui.warningPanel,10,ARUI_C.white,"LEFT"); Warn.ui.yLab:SetPoint("LEFT",Warn.ui.x,"RIGHT",18,0); Warn.ui.yLab:SetText("Y")
Warn.ui.y=CreateFrame("EditBox","AutoRangeWarningYEdit",SF.ui.warningPanel,"InputBoxTemplate"); Warn.ui.y:SetWidth(62); Warn.ui.y:SetHeight(22); Warn.ui.y:SetPoint("LEFT",Warn.ui.yLab,"RIGHT",7,0); Warn.ui.y:SetAutoFocus(false); Warn.ui.y:SetMaxLetters(7)
Warn.ui.wLab=ARUI_Font(SF.ui.warningPanel,10,ARUI_C.white,"LEFT"); Warn.ui.wLab:SetPoint("LEFT",Warn.ui.y,"RIGHT",22,0); Warn.ui.wLab:SetText("宽")
Warn.ui.w=CreateFrame("EditBox","AutoRangeWarningWEdit",SF.ui.warningPanel,"InputBoxTemplate"); Warn.ui.w:SetWidth(62); Warn.ui.w:SetHeight(22); Warn.ui.w:SetPoint("LEFT",Warn.ui.wLab,"RIGHT",7,0); Warn.ui.w:SetAutoFocus(false); Warn.ui.w:SetMaxLetters(5)
Warn.ui.hLab=ARUI_Font(SF.ui.warningPanel,10,ARUI_C.white,"LEFT"); Warn.ui.hLab:SetPoint("LEFT",Warn.ui.w,"RIGHT",18,0); Warn.ui.hLab:SetText("高")
Warn.ui.h=CreateFrame("EditBox","AutoRangeWarningHEdit",SF.ui.warningPanel,"InputBoxTemplate"); Warn.ui.h:SetWidth(62); Warn.ui.h:SetHeight(22); Warn.ui.h:SetPoint("LEFT",Warn.ui.hLab,"RIGHT",7,0); Warn.ui.h:SetAutoFocus(false); Warn.ui.h:SetMaxLetters(5)
Warn.ui.apply=ARUI_Button(SF.ui.warningPanel,"保存位置尺寸",106,24); Warn.ui.apply:SetPoint("TOPLEFT",SF.ui.warningPanel,"TOPLEFT",14,-234); Warn.ui.apply:SetScript("OnClick",function() Warn.ApplyFromUI(true) end)
Warn.ui.test=ARUI_Button(SF.ui.warningPanel,"测试 / 拖动",96,24); Warn.ui.test:SetPoint("LEFT",Warn.ui.apply,"RIGHT",10,0); Warn.ui.test:SetScript("OnClick",function() Warn.ToggleTest() end)
Warn.ui.scaleLab=ARUI_Font(SF.ui.warningPanel,10,ARUI_C.white,"LEFT"); Warn.ui.scaleLab:SetPoint("LEFT",Warn.ui.test,"RIGHT",14,0); Warn.ui.scaleLab:SetText("比例")
Warn.ui.scale=ARUI_Button(SF.ui.warningPanel,"100% ▼",76,24); Warn.ui.scale:SetPoint("LEFT",Warn.ui.scaleLab,"RIGHT",7,0); Warn.ui.scale:SetScript("OnClick",function() Warn.ToggleScaleMenu() end)
Warn.ui.scaleMenu=CreateFrame("Frame",nil,SF.ui.warningPanel); Warn.ui.scaleMenu:SetWidth(76); Warn.ui.scaleMenu:SetHeight(table.getn(Warn.scaleOptions)*20+8); Warn.ui.scaleMenu:SetPoint("TOPLEFT",Warn.ui.scale,"BOTTOMLEFT",0,-2); ARUI_Backdrop(Warn.ui.scaleMenu,{0.08,0.09,0.11,0.98},ARUI_C.line); Warn.ui.scaleMenu:SetFrameLevel(SF.ui.warningPanel:GetFrameLevel()+20); Warn.ui.scaleMenu:Hide()
Warn.ui.scaleButtons={}
do
  local i,sv,b
  for i=1,table.getn(Warn.scaleOptions) do
    sv=Warn.scaleOptions[i]
    b=ARUI_Button(Warn.ui.scaleMenu,tostring(math.floor(sv*100+0.5)).."%",66,18)
    b:SetPoint("TOPLEFT",Warn.ui.scaleMenu,"TOPLEFT",5,-4-(i-1)*20)
    b.scaleValue=sv
    b:SetScript("OnClick",function() Warn.SetScale(this.scaleValue,true) end)
    Warn.ui.scaleButtons[i]=b
  end
end
-- Actual warning overlay. Mouse is enabled only in explicit test mode.
Warn.frame=CreateFrame("Frame","AutoRangeDangerWarningFrame",UIParent)
Warn.frame:SetFrameStrata("FULLSCREEN_DIALOG"); Warn.frame:SetMovable(true); Warn.frame:SetClampedToScreen(true); Warn.frame:EnableMouse(false)
Warn.texture=Warn.frame:CreateTexture(nil,"ARTWORK"); Warn.texture:SetAllPoints(Warn.frame)
Warn.frame:SetScript("OnMouseDown",function() if Warn.test and arg1=="LeftButton" then this:StartMoving() end end)
Warn.frame:SetScript("OnMouseUp",function() if Warn.test then this:StopMovingOrSizing(); Warn.SaveDraggedPosition() end end)

-- Stage2.18: 2-direction mode owns two independent overlay frames/positions.
local function WarnCreateDirectionFrame(slot,name)
  local f=CreateFrame("Frame",name,UIParent)
  f:SetFrameStrata("FULLSCREEN_DIALOG"); f:SetMovable(true); f:SetClampedToScreen(true); f:EnableMouse(false); f.directionSlot=slot
  local tex=f:CreateTexture(nil,"ARTWORK"); tex:SetAllPoints(f)
  local lab=f:CreateFontString(nil,"OVERLAY","GameFontNormalSmall"); lab:SetPoint("TOP",f,"TOP",0,12); lab:SetText(WarnDirectionLabel(slot)); lab:Hide()
  Warn.directionFrames[slot]=f; Warn.directionTextures[slot]=tex; Warn.directionLabels[slot]=lab
  f:SetScript("OnMouseDown",function() if Warn.test and arg1=="LeftButton" then this:StartMoving() end end)
  f:SetScript("OnMouseUp",function() if Warn.test then this:StopMovingOrSizing(); Warn.SaveDraggedDirectionPosition(this.directionSlot) end end)
end
WarnCreateDirectionFrame("LEFT","AutoRangeDangerWarningLeftFrame")
WarnCreateDirectionFrame("RIGHT","AutoRangeDangerWarningRightFrame")
Warn.ApplyFrame(); Warn.UpdateVisibility(); Warn.SyncUI()
for _,eb in ipairs({Warn.ui.x,Warn.ui.y,Warn.ui.w,Warn.ui.h}) do
  eb:SetScript("OnEnterPressed",function() Warn.ApplyFromUI(true); this:ClearFocus() end)
  eb:SetScript("OnEscapePressed",function() Warn.SyncUI(); this:ClearFocus() end)
end

-- M2 LIBRARY PAGE
SF.ui.m2Panel=CreateFrame("Frame",nil,SF.ui.pageM2); SF.ui.m2Panel:SetAllPoints(SF.ui.pageM2); ARUI_Backdrop(SF.ui.m2Panel,{0.34,0.37,0.44,1},ARUI_C.line)
SF.ui.m2Title=ARUI_Font(SF.ui.m2Panel,12,ARUI_C.muted,"LEFT"); SF.ui.m2Title:SetPoint("TOPLEFT",SF.ui.m2Panel,"TOPLEFT",14,-12); SF.ui.m2Title:SetText("M2 素材浏览器")
M2.ui.scan=ARUI_Button(SF.ui.m2Panel,"扫描 M2",82,24); M2.ui.scan:SetPoint("TOPLEFT",SF.ui.m2Panel,"TOPLEFT",14,-38)
M2.ui.status=ARUI_Font(SF.ui.m2Panel,9,ARUI_C.dim,"LEFT"); M2.ui.status:SetWidth(310); M2.ui.status:SetPoint("LEFT",M2.ui.scan,"RIGHT",10,0)
M2.ui.search=CreateFrame("EditBox","AutoRangeM2SearchEdit",SF.ui.m2Panel,"InputBoxTemplate"); M2.ui.search:SetWidth(150); M2.ui.search:SetHeight(22); M2.ui.search:SetPoint("TOPRIGHT",SF.ui.m2Panel,"TOPRIGHT",-74,-39); M2.ui.search:SetAutoFocus(false); M2.ui.search:SetMaxLetters(80)
M2.ui.searchBtn=ARUI_Button(SF.ui.m2Panel,"搜索",54,22); M2.ui.searchBtn:SetPoint("LEFT",M2.ui.search,"RIGHT",4,0)
M2.ui.scan:SetScript("OnClick",function() M2.StartScan() end)
M2.ui.searchBtn:SetScript("OnClick",function() M2.Search() end)
M2.ui.search:SetScript("OnEnterPressed",function() M2.Search(); this:ClearFocus() end)
M2.ui.search:SetScript("OnEscapePressed",function() this:SetText(""); this:ClearFocus(); M2.Search() end)

M2.ui.left=CreateFrame("Frame",nil,SF.ui.m2Panel); M2.ui.left:SetWidth(190); M2.ui.left:SetPoint("TOPLEFT",SF.ui.m2Panel,"TOPLEFT",8,-72); M2.ui.left:SetPoint("BOTTOMLEFT",SF.ui.m2Panel,"BOTTOMLEFT",8,86); ARUI_Backdrop(M2.ui.left,{0.38,0.41,0.48,1},ARUI_C.line)
M2.ui.leftTitle=ARUI_Font(M2.ui.left,10,ARUI_C.muted,"LEFT"); M2.ui.leftTitle:SetPoint("TOPLEFT",M2.ui.left,"TOPLEFT",10,-8); M2.ui.leftTitle:SetText("来源 / MPQ")
M2.ui.sourceRows={}
local m2ri
for m2ri=1,M2.sourcePageSize do
  local rb=ARUI_Button(M2.ui.left,"",168,22); rb:SetID(m2ri); rb:SetPoint("TOPLEFT",M2.ui.left,"TOPLEFT",10,-28-(m2ri-1)*23); rb.text:SetJustifyH("LEFT"); rb:SetScript("OnClick",function() M2.ClickSourceRow(this:GetID()) end); M2.ui.sourceRows[m2ri]=rb
end
M2.ui.sourcePrev=ARUI_Button(M2.ui.left,"<",28,20); M2.ui.sourcePrev:SetPoint("BOTTOMLEFT",M2.ui.left,"BOTTOMLEFT",10,6)
M2.ui.sourceNext=ARUI_Button(M2.ui.left,">",28,20); M2.ui.sourceNext:SetPoint("LEFT",M2.ui.sourcePrev,"RIGHT",4,0)
M2.ui.sourcePage=ARUI_Font(M2.ui.left,9,ARUI_C.dim,"LEFT"); M2.ui.sourcePage:SetPoint("LEFT",M2.ui.sourceNext,"RIGHT",8,0)
M2.ui.sourcePrev:SetScript("OnClick",function() M2.CycleSourcePage(-1) end); M2.ui.sourceNext:SetScript("OnClick",function() M2.CycleSourcePage(1) end)

M2.ui.right=CreateFrame("Frame",nil,SF.ui.m2Panel); M2.ui.right:SetPoint("TOPLEFT",M2.ui.left,"TOPRIGHT",4,0); M2.ui.right:SetPoint("BOTTOMRIGHT",SF.ui.m2Panel,"BOTTOMRIGHT",-8,86); ARUI_Backdrop(M2.ui.right,{0.38,0.41,0.48,1},ARUI_C.line)
M2.ui.rightTitle=ARUI_Font(M2.ui.right,10,ARUI_C.muted,"LEFT"); M2.ui.rightTitle:SetWidth(420); M2.ui.rightTitle:SetPoint("TOPLEFT",M2.ui.right,"TOPLEFT",10,-8); M2.ui.rightTitle:SetText("M2列表")
M2.ui.modelRows={}
M2.ui.modelActionRows={}
for m2ri=1,M2.modelPageSize do
  local rb=ARUI_Button(M2.ui.right,"",350,22); rb:SetID(m2ri); rb:SetPoint("TOPLEFT",M2.ui.right,"TOPLEFT",10,-28-(m2ri-1)*23); rb.text:SetJustifyH("LEFT"); rb:SetScript("OnClick",function() M2.ClickModelRow(this:GetID()) end); M2.ui.modelRows[m2ri]=rb
  local ab=ARUI_Button(M2.ui.right,"添加",58,22); ab:SetID(m2ri); ab:SetPoint("LEFT",rb,"RIGHT",6,0); ab:SetScript("OnClick",function() M2.ClickModelAction(this:GetID()) end); M2.ui.modelActionRows[m2ri]=ab
end
M2.ui.modelPrev=ARUI_Button(M2.ui.right,"上一页",58,20); M2.ui.modelPrev:SetPoint("BOTTOMLEFT",M2.ui.right,"BOTTOMLEFT",10,6)
M2.ui.modelNext=ARUI_Button(M2.ui.right,"下一页",58,20); M2.ui.modelNext:SetPoint("LEFT",M2.ui.modelPrev,"RIGHT",5,0)
M2.ui.modelPage=ARUI_Font(M2.ui.right,9,ARUI_C.dim,"LEFT"); M2.ui.modelPage:SetPoint("LEFT",M2.ui.modelNext,"RIGHT",8,0)
M2.ui.modelPrev:SetScript("OnClick",function() M2.CycleModelPage(-1) end); M2.ui.modelNext:SetScript("OnClick",function() M2.CycleModelPage(1) end)

M2.ui.selectedSource=ARUI_Font(SF.ui.m2Panel,9,ARUI_C.muted,"LEFT"); M2.ui.selectedSource:SetWidth(170); M2.ui.selectedSource:SetPoint("BOTTOMLEFT",SF.ui.m2Panel,"BOTTOMLEFT",14,61); M2.ui.selectedSource:SetText("来源：--")
M2.ui.selectedPath=ARUI_Font(SF.ui.m2Panel,9,ARUI_C.white,"LEFT"); M2.ui.selectedPath:SetWidth(440); M2.ui.selectedPath:SetPoint("LEFT",M2.ui.selectedSource,"RIGHT",4,0); M2.ui.selectedPath:SetText("未选择M2")
SF.ui.m2WidthLabel=ARUI_Font(SF.ui.m2Panel,9,ARUI_C.dim,"LEFT"); SF.ui.m2WidthLabel:SetPoint("BOTTOMLEFT",SF.ui.m2Panel,"BOTTOMLEFT",14,34); SF.ui.m2WidthLabel:SetText("基础宽度(高级)")
M2.ui.width=CreateFrame("EditBox","AutoRangeM2WidthEdit",SF.ui.m2Panel,"InputBoxTemplate"); M2.ui.width:SetWidth(48); M2.ui.width:SetHeight(20); M2.ui.width:SetPoint("LEFT",SF.ui.m2WidthLabel,"RIGHT",6,0); M2.ui.width:SetAutoFocus(false); M2.ui.width:SetMaxLetters(6); M2.ui.width:SetText("10"); M2.ui.width:SetScript("OnEscapePressed",function() this:ClearFocus() end)
M2.ui.preview=ARUI_Button(SF.ui.m2Panel,"预览",58,22); M2.ui.preview:SetPoint("LEFT",M2.ui.width,"RIGHT",12,0)
M2.ui.clearTest=ARUI_Button(SF.ui.m2Panel,"清除预览",70,22); M2.ui.clearTest:SetPoint("LEFT",M2.ui.preview,"RIGHT",6,0)
M2.ui.preview:SetScript("OnClick",function() M2.PreviewSelected() end); M2.ui.clearTest:SetScript("OnClick",function() M2.ClearTest(true) end)
SF.ui.m2Foot=ARUI_Font(SF.ui.m2Panel,8,ARUI_C.dim,"RIGHT"); SF.ui.m2Foot:SetWidth(625); SF.ui.m2Foot:SetPoint("BOTTOMRIGHT",SF.ui.m2Panel,"BOTTOMRIGHT",-12,10); SF.ui.m2Foot:SetText("左侧按真实MPQ分类；右侧素材可直接添加，已添加列表可直接删除。")
M2.scanFrame=CreateFrame("Frame",nil,SF.ui.m2Panel)

SF.ui.pageBasic:Show(); SF.ui.pageM2:Hide(); SF.ui.pageEnemy:Hide(); SF.ui.pageSelf:Hide(); SF.ui.pageWarning:Hide()

function SF.RenderUI()
  -- Hidden pages do no list formatting. Tab switching is Show/Hide only.
  if panel and not panel:IsVisible() then return end
  if SF.ui.selfToggle then SF.ui.selfToggle:SetChecked(SF.selfEnabled) end
  if SF.ui.enemyDecision then SF.ui.enemyDecision:SetText("判定："..SF.DecisionLabel(SF.enemyEditDecision)) end
  if uiPage=="ENEMY" then
    if SF.ui.enemyModeBtn then
      local label=(SF.enemyMode=="ONLY" and "仅指定") or (SF.enemyMode=="EXCLUDE" and "排除指定") or "全部技能"
      SF.ui.enemyModeBtn:SetText("模式："..label)
    end
    if SF.ui.enemyStyle then SF.ui.enemyStyle:SetText(SF.SelfStyleLabel(SF.enemyEditStyle)) end
    local mode=SF.ValidRuleMode(SF.enemyEditMode)
    if SF.ui.modeNormal then SF.ui.modeNormal:SetChecked(mode=="NORMAL") end
    if SF.ui.modeRange then SF.ui.modeRange:SetChecked(mode=="VISION_RANGE") end
    if SF.ui.modeGlobal then SF.ui.modeGlobal:SetChecked(mode=="VISION_GLOBAL") end
    if SF.ui.modeHit then SF.ui.modeHit:SetChecked(mode=="GLOBAL_HIT") end
    SF.RenderRuleRows("ENEMY")
  elseif uiPage=="SELF" then
    if SF.ui.selfStyle then SF.ui.selfStyle:SetText(SF.SelfStyleLabel(SF.selfEditStyle)) end
    SF.RenderRuleRows("SELF")
  elseif uiPage=="M2" then
    M2.RenderBrowser()
  elseif uiPage=="WARNING" then
    if Warn.ui.enabled then Warn.ui.enabled:SetChecked(Warn.enabled) end
    if Warn.ui.test then Warn.ui.test:SetText(Warn.test and "结束测试" or "测试 / 拖动") end
  end
end

AutoRange_TogglePanel=function()
  if panel:IsVisible() then Warn.test=false; Warn.UpdateVisibility(); Warn.CloseMediaMenu(); if Warn.ui.scaleMenu then Warn.ui.scaleMenu:Hide() end; if SF.ui.totemCustomPopup then SF.ui.totemCustomPopup:Hide() end; M2.ClearTest(false); M2.StopScanDriver(); panel:Hide(); AutoRangeDB.hidden=true
  else panel:Show(); AutoRangeDB.hidden=false; SetUIPage(AutoRangeDB.uiPage or "BASIC"); if type(Render)=="function" then Render() end end
end

AutoRange_IsEnabled=function()
  return (running and visualEnabled) and true or false
end

Render=function()
  local enabled=(running and visualEnabled) and true or false
  if enableCheck then enableCheck:SetChecked(enabled) end
  if SF.ui.enemyCheck then SF.ui.enemyCheck:SetChecked(enemyCircle) end
  if SF.ui.friendCheck then SF.ui.friendCheck:SetChecked(friendCircle) end
  if SF.ui.totemCheck then SF.ui.totemCheck:SetChecked(totemMode~="OFF") end
  if SF.ui.enemyLimitCheck then SF.ui.enemyLimitCheck:SetChecked(enemyLimitEnabled) end
  if SF.ui.semanticCheck then SF.ui.semanticCheck:SetChecked(SF.semanticFilterEnabled) end
  if SF.ui.enemyLimitEdit then SF.ui.enemyLimitEdit:SetText(tostring(enemyLimitMax)) end
  if SF.ui.refreshEdit and not SF.refreshEditing then SF.ui.refreshEdit:SetText(string.format("%.2f",SF.refreshInterval or 0.20)) end
  if SF.ui.totemModeBtn then
    local label=totemModeText[totemMode] or totemMode
    if totemMode=="OFF" then label=totemModeText[totemLastMode] or "仅自己" end
    SF.ui.totemModeBtn:SetText("来源："..tostring(label))
  end
  if SF.ui.totemStyleBtn then
    SF.ui.totemStyleBtn:SetText(SF.totemUnifiedStyle and ("M2："..TotemVisualStyleLabel(totemVisualStyle)) or "M2：按分类")
    if SF.totemUnifiedStyle then SF.ui.totemStyleBtn:Enable(); SF.ui.totemStyleBtn:SetAlpha(1)
    else SF.ui.totemStyleBtn:Disable(); SF.ui.totemStyleBtn:SetAlpha(0.45) end
  end
  if SF.ui.totemAllSameBtn then SF.ui.totemAllSameBtn:SetText("全部一个颜色："..(SF.totemUnifiedStyle and "开" or "关")) end
  local catEnabled=not SF.totemUnifiedStyle
  local function RenderTotemCategoryButton(btn,element,label)
    if not btn then return end
    btn:SetText(label.."："..TotemVisualStyleLabel(SF.totemCategoryStyles[element]))
    if catEnabled then btn:Enable(); btn:SetAlpha(1) else btn:Disable(); btn:SetAlpha(0.45) end
  end
  RenderTotemCategoryButton(SF.ui.totemFireStyleBtn,"FIRE","火焰图腾")
  RenderTotemCategoryButton(SF.ui.totemEarthStyleBtn,"EARTH","大地图腾")
  RenderTotemCategoryButton(SF.ui.totemWaterStyleBtn,"WATER","水之图腾")
  RenderTotemCategoryButton(SF.ui.totemAirStyleBtn,"AIR","空气图腾")
  if SF.ui.totemCustomBtn then SF.ui.totemCustomBtn:SetText("自定义图腾："..tostring(table.getn(SF.totemCustomIds or {})).."条（最高优先）") end
  SF.RenderUI()
  if stateText then
    if enabled then stateText:SetText("|cff55ff77已开启|r")
    else stateText:SetText("|cffff6666已关闭|r") end
  end
  if enabled~=lastFuBarEnabled then
    lastFuBarEnabled=enabled
    if AutoRange_FuBarEntry and type(AutoRange_FuBarEntry.SetText)=="function" then
      pcall(AutoRange_FuBarEntry.SetText,AutoRange_FuBarEntry,enabled and "AutoRange:开" or "AutoRange:关")
    end
  end
end

local function AddOrRefreshCast(caster,spell,kind,castDurationMs,forcedSource)
  if not running or not spell or spell<=0 or not caster then return nil end

  local rawCasterGuid=NormGuid(caster)
  local castToken=rawCasterGuid..":"..tostring(spell)
  local now=GetTime()
  local key=nil
  local h=nil
  local sourceType=nil
  local casterGuid=nil
  local canonicalRaw=nil

  -- START -> CAST/CHANNEL reuses the exact pending hazard and therefore also its
  -- already-resolved relation/canonical GUID. No second RelationByGuid call.
  local pendingKey=pendingCastByToken[castToken]
  if pendingKey then
    h=hazards[pendingKey]
    if h then
      key=pendingKey; sourceType=h.sourceType; casterGuid=h.casterGuid; canonicalRaw=h.casterGuidRaw
    else
      pendingCastByToken[castToken]=nil
    end
  end

  if h and (h.sourceType=="ENEMY" or h.sourceType=="UNKNOWN") and not SF.EnemyAllowed(spell) then RemoveHazard(h.key); return nil end
  if h and h.sourceType=="FRIEND" and not SF.AutoSemanticAllowed(spell) then RemoveHazard(h.key); return nil end

  if not h then
    if forcedSource=="SELF" then
      sourceType="SELF"; casterGuid=rawCasterGuid; canonicalRaw=tostring(caster)
    else
      local allowed,src,canonical,canonicalRawValue=CasterSourceAllowed(caster)
      if not allowed then return nil end
      sourceType=src or "UNKNOWN"
      if (sourceType=="ENEMY" or sourceType=="UNKNOWN") and not SF.EnemyAllowed(spell) then return nil end
      if sourceType=="FRIEND" and not SF.AutoSemanticAllowed(spell) then return nil end
      casterGuid=canonical or rawCasterGuid
      canonicalRaw=(canonicalRawValue and canonicalRawValue~="" and canonicalRawValue or tostring(caster))
    end

    -- CASTER effects are unique by caster+spell. Reuse lookup is O(1), not a scan
    -- over every live ground/totem hazard.
    local reuseToken=tostring(sourceType or "UNKNOWN")..":"..casterGuid..":"..tostring(spell)
    local reuseKey=casterReuseByToken[reuseToken]
    if reuseKey then
      local oldh=hazards[reuseKey]
      if oldh and not oldh.totemOnly and oldh.modeHint=="CASTER" and not oldh.castPending then
        h=oldh; key=reuseKey; h.pendingReuse=true
      else
        casterReuseByToken[reuseToken]=nil
      end
    end
  end

  if not h then
    castSerial=castSerial+1
    key=casterGuid..":"..tostring(spell).."#"..tostring(castSerial)
    local r=Resolve(spell)
    local rule=SF.RuleForSource(sourceType,spell)
    local manualRadius=rule and math.max(0,tonumber(rule.radius) or 0) or 0
    if r and r.mode=="CONE" and manualRadius<=0 then return nil end
    h={key=key,caster=caster,casterGuid=casterGuid,casterGuidRaw=canonicalRaw,
      castToken=castToken,spell=spell,geomSpell=spell,started=now,radius=0,
      modeHint="UNKNOWN",modeNow="PENDING",durationSec=0.75,expires=now+0.75,
      sourceType=sourceType or "UNKNOWN",castPending=(kind=="START") and true or false,
      ruleStyle=rule and rule.style or nil,manualRadius=manualRadius}
    if sourceType=="SELF" then h.selfAuraManaged=SF.SpellHasAuraEffect(spell) and true or false end
    if r then
      h.geomSpell=r.geomSpell; h.radius=(manualRadius>0 and manualRadius or r.radius); h.modeHint=r.mode; h.modeNow=r.mode; h.centerFix=r.centerFix
      if r.mode=="CONE" and manualRadius>0 then h.modeHint="CASTER"; h.modeNow="CASTER" end
      h.durationSec=(r.durationMs>0 and r.durationMs/1000 or SF.SpellDurationSec(spell) or 2.0)
      if kind=="START" then h.expires=now+math.max(0.75,(tonumber(castDurationMs) or 0)/1000)
      else h.expires=now+math.max(h.durationSec,0.75) end
    elseif manualRadius>0 then
      h.radius=manualRadius; h.modeHint="CASTER"; h.modeNow="CASTER"
      h.durationSec=SF.SpellDurationSec(spell) or 2.0
      if kind=="START" then h.expires=now+math.max(0.75,(tonumber(castDurationMs) or 0)/1000)
      else h.expires=now+math.max(h.durationSec,0.75) end
    else
      h.durationSec=1.2; h.expires=now+((kind=="START") and 0.75 or 1.2)
    end
    if h.modeHint=="GROUND" or h.modeHint=="UNKNOWN" then
      h.dynamicDiscoverUntil=(kind=="START") and nil or (now+1.50)
      h.nextDynamicScan=now; h.dynamicMissCount=0
    end
    RegisterHazard(h)
    RefreshHazardRuntimeClass(h)
    if kind=="START" then pendingCastByToken[castToken]=key end
  else
    h.caster=caster
    h.casterGuid=casterGuid or h.casterGuid
    h.casterGuidRaw=canonicalRaw or h.casterGuidRaw or tostring(caster)
    h.sourceType=sourceType or h.sourceType
    local liveRule=SF.RuleForSource(h.sourceType,spell)
    if liveRule then
      local newStyle=SF.ValidStyle(liveRule.style,(h.sourceType=="SELF" and "WHITE" or "STANDARD"))
      if h.ruleStyle~=newStyle then h.ruleStyle=newStyle; ClearVisual(h) end
      h.manualRadius=math.max(0,tonumber(liveRule.radius) or 0)
      if h.manualRadius>0 then h.radius=h.manualRadius end
    end
  end

  if kind=="CAST" or kind=="CHANNEL" then
    h.castPending=false; h.pendingReuse=nil
    if pendingCastByToken[castToken]==key then pendingCastByToken[castToken]=nil end
    h.started=now; h.expires=now+math.max(h.durationSec or 0.75,0.75)
    -- A successful configured SELF spell that DBC classifies as an Aura is held
    -- immediately. We do not wait for the fragile 1.12 buff-name scan before
    -- suppressing the old 2-second fallback expiry. Aura removal events own end.
    if h.sourceType=="SELF" and h.selfAuraManaged then h.selfAuraHeld=true end
    if h.modeHint=="GROUND" or h.modeHint=="UNKNOWN" then
      h.dynamicDiscoverUntil=now+1.50; h.nextDynamicScan=now; h.dynamicMissCount=0
    end
    if h.modeHint=="CASTER" and h.casterGuid then
      casterReuseByToken[tostring(h.sourceType or "UNKNOWN")..":"..h.casterGuid..":"..tostring(h.spell or 0)]=h.key
    end
  elseif kind=="START" then
    h.castPending=true; pendingCastByToken[castToken]=key
    if not h.pendingReuse then h.expires=math.max(h.expires or now,now+0.75) end
  end

  RefreshHazardRuntimeClass(h)
  if enemyLimitEnabled and EnforceEnemyLimit then EnforceEnemyLimit() end
  if ApplyRuntimeHooks then ApplyRuntimeHooks() end
  return hazards[h.key] or nil
end

RestoreActiveSelfAuraHazards=function()
  if not running or not visualEnabled or not SF.selfEnabled or SF.selfActiveCount<=0 then return 0 end
  if not SF.playerGuid then SF.playerGuidTried=false; SF.RefreshPlayerGuid() end
  if not SF.playerGuid then return 0 end

  -- Refresh from the player's current auras, not only from hazards that happened
  -- to survive an OFF period.  This repairs a SELF circle if an older build or a
  -- source-toggle cleanup already removed its logical hazard.
  local names,textures=SF.ScanPlayerHelpfulAuras()
  local have={}
  local i,h,spell,rule,name
  for i=1,table.getn(order) do
    h=hazards[order[i]]
    if h and h.sourceType=="SELF" then have[h.spell]=true end
  end

  local restored=0
  for spell,rule in pairs(SF.selfRules) do
    spell=tonumber(spell) or 0
    if spell>0 and not have[spell] then
      name=(rule and rule.name) or ""
      if name=="" then name=SF.GetSpellName(spell); if rule and name~="" then rule.name=name end end
      if SF.PlayerHasSpellAura(spell,names,name,textures) then
        h=AddOrRefreshCast(SF.playerGuid,spell,"CAST",0,"SELF")
        if h then h.selfAuraManaged=true; h.selfAuraSeen=true; h.selfAuraHeld=true; restored=restored+1 end
      end
    end
  end
  if restored>0 then SF.SaveRules() end
  return restored
end

local function HideAllOwnedVisuals()
  -- Master/visual OFF is a user action, not a hot path.  Release native preview
  -- slots completely while keeping logical hazards, so ON always recreates from
  -- authoritative Lua state instead of depending on stale VisualHide/VisualShow
  -- handles shared by enemy/friend/self circles.
  local i
  for i=1,table.getn(order) do
    local h=hazards[order[i]]
    if h and h.visualCreated then ClearVisual(h) end
  end
end


local function RefreshAllOwnedVisuals()
  RefreshSourceVisibility(nil,nil)
end

local function ClearAll()
  Warn.dirty=true; Warn.SetState("NONE","SINGLE"); Warn.losCache={}
  -- Verified native scope: AutoRange.VisualClearAll only touches the 16
  -- gAutoRangePreviews slots; it does not clear MoonMarker placement/advanced slots.
  if type(TaiYangShenDian)=="function" then pcall(TaiYangShenDian,"AutoRange.VisualClearAll") end
  hazards={}; order={}; orderIndex={}; dynamicMeta={}; casterRuntime={}; casterHazardCounts={}; casterReuseByToken={}; pendingTotems={}; pendingCastByToken={}; totemBootstrapAt=nil; totemBootstrapRetries=0
  enemyLimitCount=0; enemyLimitHead=nil; enemyLimitTail=nil
  WipeTable(pollDynamics); WipeTable(pollDynamicSeen); WipeTable(pollClaimed); WipeTable(pollRemove); WipeTable(pollReclass); WipeTable(pollCasterCache)
  RuntimeBucketClear(activeCasterKeys,activeCasterIndex)
  RuntimeBucketClear(activeDiscoveryKeys,activeDiscoveryIndex)
  RuntimeBucketClear(activeDynamicKeys,activeDynamicIndex)
  RuntimeBucketClear(activeTotemKeys,activeTotemIndex)
  if ApplyRuntimeHooks then ApplyRuntimeHooks() end
end

local function RuntimeEventNeeded()
  return running and visualEnabled
end

function SF.RegisterTysCastEvents()
  if type(TaiYangShenDian)~="function" then return false end
  local ok,status=pcall(TaiYangShenDian,"Cast.Status")
  if not ok or type(status)~="table" or not status.customEventsReady then return false end
  local names={"TYS_CAST_START","TYS_CAST_SUCCESS","TYS_CAST_FAILED","TYS_CAST_INTERRUPTED","TYS_CHANNEL_START"}
  local i
  for i=1,table.getn(names) do
    local rok=pcall(function() F:RegisterEvent(names[i]) end)
    if not rok then return false end
  end
  SF.castEventSource="TYS"
  return true
end

function SF.UnregisterTysCastEvents()
  local names={"TYS_CAST_START","TYS_CAST_SUCCESS","TYS_CAST_FAILED","TYS_CAST_INTERRUPTED","TYS_CHANNEL_START"}
  local i
  for i=1,table.getn(names) do pcall(function() F:UnregisterEvent(names[i]) end) end
  SF.castEventSource="NONE"
end

local function RuntimeUpdateNeeded()
  if not running or not visualEnabled then return false end
  if totemBootstrapAt or table.getn(pendingTotems)>0 or table.getn(activeTotemKeys)>0 then return true end
  return table.getn(activeCasterKeys)>0 or table.getn(activeDiscoveryKeys)>0 or table.getn(activeDynamicKeys)>0
end

ApplyRuntimeHooks=function()
  local wantEvent=RuntimeEventNeeded() and true or false
  if wantEvent~=runtimeEventAttached then
    if wantEvent then runtimeEventAttached=SF.RegisterTysCastEvents() and true or false
    else SF.UnregisterTysCastEvents(); runtimeEventAttached=false end
  end
  local wantAura=(running and visualEnabled and SF.selfEnabled and SF.selfActiveCount>0) and true or false
  if wantAura~=runtimeAuraAttached then
    if wantAura then
      F:RegisterEvent("PLAYER_AURAS_CHANGED")
      F:RegisterEvent("CHAT_MSG_SPELL_AURA_GONE_SELF")
    else
      F:UnregisterEvent("PLAYER_AURAS_CHANGED")
      F:UnregisterEvent("CHAT_MSG_SPELL_AURA_GONE_SELF")
    end
    runtimeAuraAttached=wantAura
  end
  local wantUpdate=(RuntimeUpdateNeeded() and OnUpdateHandler) and true or false
  if wantUpdate~=runtimeUpdateAttached then
    if wantUpdate then F:SetScript("OnUpdate",OnUpdateHandler) else F:SetScript("OnUpdate",nil) end
    runtimeUpdateAttached=wantUpdate
  end
end

AutoRange_SetEnabled=function(enabled,announce)
  local on=enabled and true or false
  if on==running and on==visualEnabled then
    if announce then Chat(on and "自动危险圈已开启。" or "自动危险圈已关闭。") end
    Render(); return
  end
  if not on then HideAllOwnedVisuals(); pendingTotems={}; totemBootstrapAt=nil; Warn.SetState("NONE","SINGLE") end
  running=on; visualEnabled=on
  AutoRangeDB.running=on; AutoRangeDB.visualEnabled=on
  elapsed=0; totemLifeElapsed=0; totemDiscoverElapsed=0

  -- Commit the master state to the UI before doing any native/source refresh.
  -- This prevents the checkbox from showing ON while the status text is left at
  -- the previous OFF value if a later refresh path encounters a client/API error.
  Render()

  RebuildRuntimeBuckets()
  ApplyRuntimeHooks()
  if on then
    if SF.selfEnabled and SF.selfActiveCount>0 then
      SF.RefreshSelfAuraHazards()
      RestoreActiveSelfAuraHazards()
    end
    if PurgeStaleHiddenHazards then PurgeStaleHiddenHazards() end
    RefreshAllOwnedVisuals()
    RebuildRuntimeBuckets()
    if totemMode~="OFF" then totemBootstrapAt=GetTime()+0.05; totemBootstrapRetries=0 end
    ApplyRuntimeHooks()
  end
  if announce then Chat(on and "自动危险圈已开启。" or "自动危险圈已关闭。") end
  Render()
end

AutoRange_ToggleEnabled=function(announce)
  local on=not ((running and visualEnabled) and true or false)
  AutoRange_SetEnabled(on,announce)
  return on
end

local function NeedDynamicSnapshot(now)
  if table.getn(activeDiscoveryKeys)==0 then return false end
  now=now or GetTime()
  local i
  for i=1,table.getn(activeDiscoveryKeys) do
    local h=hazards[activeDiscoveryKeys[i]]
    if h and now<=(h.dynamicDiscoverUntil or 0) and now>=(h.nextDynamicScan or 0) then return true end
  end
  return false
end

local function HasNormalWork()
  return table.getn(activeCasterKeys)>0 or table.getn(activeDiscoveryKeys)>0 or table.getn(activeDynamicKeys)>0
end

local function Poll()
  if not running or not visualEnabled or not HasNormalWork() then return end
  local now=GetTime()

  WipeTable(pollDynamics); WipeTable(pollDynamicSeen); WipeTable(pollClaimed)
  WipeTable(pollRemove); WipeTable(pollReclass); WipeTable(pollCasterCache)
  local dynamics=pollDynamics
  if NeedDynamicSnapshot(now) then
    DynamicSnapshot(dynamics,pollDynamicSeen)
    local i
    for i=1,table.getn(activeDiscoveryKeys) do
      local h=hazards[activeDiscoveryKeys[i]]
      if h and now<=(h.dynamicDiscoverUntil or 0) then h.nextDynamicScan=now+(SF.refreshInterval or 0.20) end
    end
  end

  local i
  for i=1,table.getn(activeDynamicKeys) do
    local h=hazards[activeDynamicKeys[i]]
    if h and h.dynamicGuid then pollClaimed[h.dynamicGuid]=h.key end
  end

  for i=1,table.getn(activeCasterKeys) do
    local key=activeCasterKeys[i]
    local h=hazards[key]
    if h then
      local retire=(not (h.sourceType=="SELF" and h.selfAuraHeld)) and now>(h.expires or now)
      if not retire and UpdateHazard(h,nil,nil,pollCasterCache)==false then retire=true end
      if retire then pollRemove[table.getn(pollRemove)+1]=key end
    end
  end

  for i=1,table.getn(activeDiscoveryKeys) do
    local key=activeDiscoveryKeys[i]
    local h=hazards[key]
    if h then
      local retire=h.dynamicDiscoverUntil and now>(h.dynamicDiscoverUntil or 0)
      if not retire and UpdateHazard(h,dynamics,pollClaimed,pollCasterCache)==false then retire=true end
      if h.dynamicGuid and not retire then pollReclass[table.getn(pollReclass)+1]=h end
      if retire then pollRemove[table.getn(pollRemove)+1]=key end
    end
  end

  for i=1,table.getn(activeDynamicKeys) do
    local key=activeDynamicKeys[i]
    local h=hazards[key]
    if h then
      local retire=(UpdateHazard(h,nil,pollClaimed,pollCasterCache)==false) and true or false
      if not retire and now>(h.expires or now) and (not h.lastDynamic or now-h.lastDynamic>0.35) then retire=true end
      if retire then pollRemove[table.getn(pollRemove)+1]=key end
    end
  end

  for i=1,table.getn(pollReclass) do RefreshHazardRuntimeClass(pollReclass[i]) end
  for i=1,table.getn(pollRemove) do RemoveHazard(pollRemove[i]) end
  if (table.getn(pollReclass)>0 or table.getn(pollRemove)>0) and ApplyRuntimeHooks then ApplyRuntimeHooks() end
end

-- WoW 1.12 has a hard 32-upvalue limit per closure. Keep event handlers split.
local function HandleVariablesLoaded()
  if AutoRangeDB.running==nil then AutoRangeDB.running=true end
  if AutoRangeDB.visualEnabled==nil then AutoRangeDB.visualEnabled=true end
  if AutoRangeDB.enemyCircle==nil then AutoRangeDB.enemyCircle=true end
  if AutoRangeDB.friendCircle==nil then AutoRangeDB.friendCircle=false end
  if AutoRangeDB.enemyLimitEnabled==nil then AutoRangeDB.enemyLimitEnabled=false end
  if AutoRangeDB.enemyLimitMax==nil then AutoRangeDB.enemyLimitMax=20 end
  if AutoRangeDB.refreshInterval==nil then AutoRangeDB.refreshInterval=0.20 end
  if AutoRangeDB.semanticFilterEnabled==nil then AutoRangeDB.semanticFilterEnabled=true end
  if AutoRangeDB.enemySkillMode==nil then AutoRangeDB.enemySkillMode="ALL" end
  if AutoRangeDB.selfCircleEnabled==nil then AutoRangeDB.selfCircleEnabled=false end
  if AutoRangeDB.enemySkillRules==nil then AutoRangeDB.enemySkillRules=AutoRangeDB.enemySkills or {} end
  if AutoRangeDB.selfSkillRules==nil then AutoRangeDB.selfSkillRules=AutoRangeDB.selfSkills or {} end
  if AutoRangeDB.skillRuleStoreVersion==nil or AutoRangeDB.skillRuleStoreVersion<4 then AutoRangeDB.skillRuleStoreVersion=4 end
  if AutoRangeDB.totemMode==nil then AutoRangeDB.totemMode="SELF" end
  if AutoRangeDB.totemLastMode==nil then AutoRangeDB.totemLastMode="SELF" end
  if AutoRangeDB.totemVisualStyle==nil then AutoRangeDB.totemVisualStyle="AUTO" end
  if AutoRangeDB.totemUnifiedStyle==nil then AutoRangeDB.totemUnifiedStyle=true end
  if type(AutoRangeDB.totemCategoryStyles)~="table" then AutoRangeDB.totemCategoryStyles={} end
  if type(AutoRangeDB.totemCustomRules)~="table" then AutoRangeDB.totemCustomRules={} end
  if AutoRangeDB.customM2Library==nil then AutoRangeDB.customM2Library={} end
  if AutoRangeDB.customM2NextId==nil then AutoRangeDB.customM2NextId=1 end
  if AutoRangeDB.uiPage==nil then AutoRangeDB.uiPage="BASIC" elseif AutoRangeDB.uiPage=="RULES" then AutoRangeDB.uiPage="ENEMY" end
  if AutoRangeDB.warningEnabled==nil then AutoRangeDB.warningEnabled=true end
  if AutoRangeDB.warningMode==nil then AutoRangeDB.warningMode="SINGLE" end
  AutoRangeDB.warningMode=Warn.NormalizeMode(AutoRangeDB.warningMode)
  if type(AutoRangeDB.warningDirectionMedia)~="table" then AutoRangeDB.warningDirectionMedia={} end
  if type(AutoRangeDB.warningDirectionPosition)~="table" then AutoRangeDB.warningDirectionPosition={} end
  if AutoRangeDB.warningSafeMediaName==nil then AutoRangeDB.warningSafeMediaName="" end
  if AutoRangeDB.warningShowSafeImage==nil then AutoRangeDB.warningShowSafeImage=true end
  if AutoRangeDB.warningScale==nil then AutoRangeDB.warningScale=1.0 end
  if AutoRangeDB.warningMediaVersion==nil or AutoRangeDB.warningMediaVersion<3 then
    local oldMedia=WarnMediaName(AutoRangeDB.warningMediaName or AutoRangeDB.warningTexture or "")
    if oldMedia and string.lower(oldMedia)=="warning_01.tga" then oldMedia=nil end
    AutoRangeDB.warningMediaName=oldMedia
    AutoRangeDB.warningMediaIndex=1
    AutoRangeDB.warningMediaVersion=3
  end
  if (tonumber(AutoRangeDB.warningMediaVersion) or 0)<4 then
    if not WarnMediaName(AutoRangeDB.warningMediaName or "") then
      local legacyMedia=WarnMediaName(AutoRangeDB.warningTexture or "")
      if legacyMedia and string.lower(legacyMedia)~="warning_01.tga" then AutoRangeDB.warningMediaName=legacyMedia end
    end
    AutoRangeDB.warningTexture=nil
    AutoRangeDB.warningMediaVersion=4
  end
  if AutoRangeDB.warningX==nil then AutoRangeDB.warningX=0 end
  if AutoRangeDB.warningY==nil then AutoRangeDB.warningY=120 end
  if AutoRangeDB.warningWidth==nil then AutoRangeDB.warningWidth=384 end
  if AutoRangeDB.warningHeight==nil then AutoRangeDB.warningHeight=192 end
  M2.LoadSaved()
  if not M2.selectedSource then M2.selectedSource="@LIBRARY" end

  running=AutoRangeDB.running and true or false
  visualEnabled=AutoRangeDB.visualEnabled and true or false
  enemyCircle=AutoRangeDB.enemyCircle and true or false
  friendCircle=AutoRangeDB.friendCircle and true or false
  enemyLimitEnabled=AutoRangeDB.enemyLimitEnabled and true or false
  enemyLimitMax=math.floor(tonumber(AutoRangeDB.enemyLimitMax) or 20)
  if enemyLimitMax<1 then enemyLimitMax=1 elseif enemyLimitMax>9999 then enemyLimitMax=9999 end
  AutoRangeDB.enemyLimitMax=enemyLimitMax
  Warn.enabled=AutoRangeDB.warningEnabled and true or false
  Warn.mode=Warn.NormalizeMode(AutoRangeDB.warningMode)
  Warn.state="NONE"; Warn.displayMode=(Warn.mode=="DIRECTION2" and "DIR" or "SINGLE")
  Warn.showSafeImage=AutoRangeDB.warningShowSafeImage and true or false
  Warn.safeMediaName=WarnMediaName(AutoRangeDB.warningSafeMediaName or "") or ""
  Warn.directionPath=""; Warn.directionSlot=nil
  Warn.directionMedia={
    LEFT=WarnMediaName(AutoRangeDB.warningDirectionMedia.LEFT or "") or "",
    RIGHT=WarnMediaName(AutoRangeDB.warningDirectionMedia.RIGHT or "") or ""
  }
  Warn.x=tonumber(AutoRangeDB.warningX) or 0; Warn.y=tonumber(AutoRangeDB.warningY) or 120
  Warn.width=tonumber(AutoRangeDB.warningWidth) or 384; Warn.height=tonumber(AutoRangeDB.warningHeight) or 192
  Warn.scale=tonumber(AutoRangeDB.warningScale) or 1.0
  if Warn.scale<0.25 then Warn.scale=0.25 elseif Warn.scale>1.50 then Warn.scale=1.50 end
  local savedDirPos=AutoRangeDB.warningDirectionPosition or {}
  local sep=math.floor((Warn.width*Warn.scale)*0.58+0.5); if sep<120 then sep=120 elseif sep>420 then sep=420 end
  Warn.directionPos={
    LEFT={x=tonumber(savedDirPos.LEFT and savedDirPos.LEFT.x) or (Warn.x-sep),y=tonumber(savedDirPos.LEFT and savedDirPos.LEFT.y) or Warn.y},
    RIGHT={x=tonumber(savedDirPos.RIGHT and savedDirPos.RIGHT.x) or (Warn.x+sep),y=tonumber(savedDirPos.RIGHT and savedDirPos.RIGHT.y) or Warn.y}
  }
  Warn.RefreshMediaList(); Warn.ApplyFrame(); Warn.Save(); Warn.SyncUI(); Warn.UpdateVisibility(); Warn.dirty=true
  SF.refreshInterval=tonumber(AutoRangeDB.refreshInterval) or 0.20
  if SF.refreshInterval<0.02 then SF.refreshInterval=0.02 elseif SF.refreshInterval>1.00 then SF.refreshInterval=1.00 end
  SF.refreshInterval=math.floor(SF.refreshInterval*100+0.5)/100
  AutoRangeDB.refreshInterval=SF.refreshInterval
  SF.semanticFilterEnabled=AutoRangeDB.semanticFilterEnabled and true or false
  AutoRangeDB.losCasterFilterEnabled=nil
  SF.enemyEditMode="NORMAL"
  SF.enemyMode=string.upper(tostring(AutoRangeDB.enemySkillMode or "ALL")); if SF.enemyMode~="ALL" and SF.enemyMode~="ONLY" and SF.enemyMode~="EXCLUDE" then SF.enemyMode="ALL" end; AutoRangeDB.enemySkillMode=SF.enemyMode
  SF.enemyRules,SF.enemyCount,SF.enemyActiveCount=SF.NormalizeRules(AutoRangeDB.enemySkillRules,"STANDARD")
  SF.RecountRules("ENEMY")
  SF.selfEnabled=AutoRangeDB.selfCircleEnabled and true or false
  local legacySelfStyle=SF.ValidStyle(AutoRangeDB.selfVisualStyle,"WHITE")
  SF.selfRules,SF.selfCount,SF.selfActiveCount=SF.NormalizeRules(AutoRangeDB.selfSkillRules,legacySelfStyle)
  SF.SaveRules()
  SF.playerGuidTried=false; SF.RefreshPlayerGuid()
  totemMode=string.upper(tostring(AutoRangeDB.totemMode or "SELF"))
  totemLastMode=string.upper(tostring(AutoRangeDB.totemLastMode or "SELF"))
  if totemLastMode=="OFF" then totemLastMode="SELF" end
  totemVisualStyle=SF.ValidTotemVisualStyle(AutoRangeDB.totemVisualStyle,"AUTO")
  AutoRangeDB.totemVisualStyle=totemVisualStyle
  SF.totemUnifiedStyle=AutoRangeDB.totemUnifiedStyle and true or false
  local savedTotemStyles=AutoRangeDB.totemCategoryStyles or {}
  local categoryDefaults={FIRE="FLAME",EARTH="JAGGED",WATER="WHITE",AIR="STANDARD",OTHER="STANDARD"}
  local element,defaultStyle
  for element,defaultStyle in pairs(categoryDefaults) do
    SF.totemCategoryStyles[element]=SF.ValidTotemVisualStyle(savedTotemStyles[element],defaultStyle)
  end
  AutoRangeDB.totemCategoryStyles=SF.totemCategoryStyles
  SF.LoadTotemCustomRules(AutoRangeDB.totemCustomRules)

  SetUIPage(AutoRangeDB.uiPage or "BASIC")
  if AutoRangeDB.hidden then panel:Hide() else panel:Show() end
  RebuildRuntimeBuckets()
  ApplyRuntimeHooks()
  Render()
end

local function HandleEnteringWorld()
  ClearAll()
  Warn.losReadyAt=GetTime()+0.75
  SF.playerGuidTried=false; SF.RefreshPlayerGuid()
  if running then
    if HasNormalWork() then Poll() end
    if totemMode~="OFF" then totemBootstrapAt=GetTime()+0.35; totemBootstrapRetries=0 end
    if ApplyRuntimeHooks then ApplyRuntimeHooks() end
  end
end

function SF.HandleNormalizedCastEvent(caster,kind,spell,castDurationMs)
  kind=string.upper(tostring(kind or "")); spell=tonumber(spell) or 0
  if kind=="FAIL" or kind=="INTERRUPT" then
    local castToken=NormGuid(caster)..":"..tostring(spell)
    local pendingKey=pendingCastByToken[castToken]
    if pendingKey then
      local ph=hazards[pendingKey]
      if ph and ph.pendingReuse then ph.castPending=false; ph.pendingReuse=nil else RemoveHazard(pendingKey) end
    end
    pendingCastByToken[castToken]=nil
    if ApplyRuntimeHooks then ApplyRuntimeHooks() end
    return
  end
  if kind~="START" and kind~="CAST" and kind~="CHANNEL" then return end

  local enemyWork=enemyCircle and not (SF.enemyMode=="ONLY" and SF.enemyActiveCount<=0)
  local selfWork=SF.selfEnabled and SF.selfActiveCount>0
  if not enemyWork and not friendCircle and totemMode=="OFF" and not selfWork then return end

  local playerCaster=false
  if selfWork then
    playerCaster=SF.IsPlayerCaster(caster)
    if not enemyWork and not friendCircle and totemMode=="OFF" and not playerCaster then return end
  end

  local ti=TotemInfo(spell)
  if ti then
    if kind=="CAST" and totemMode~="OFF" then StartTotemDiscovery(caster,spell) end
    return
  end

  if selfWork and playerCaster then
    if SF.RuleForSource("SELF",spell) then
      AddOrRefreshCast(caster,spell,kind,tonumber(castDurationMs) or 0,"SELF")
      if kind=="CAST" or kind=="CHANNEL" then SF.RefreshSelfAuraHazards() end
    end
    return
  end

  if not enemyWork and not friendCircle then return end
  if enemyWork and not friendCircle and SF.enemyMode~="ALL" and not SF.EnemyAllowed(spell) then return end
  AddOrRefreshCast(caster,spell,kind,tonumber(castDurationMs) or 0,nil)
end

function SF.HandleTysCastEvent(ev)
  local caster=arg1
  local spell=tonumber(arg3) or 0
  local nativeKind=tonumber(arg4) or 0
  local duration=tonumber(arg7) or 0
  if ev=="TYS_CAST_START" then SF.HandleNormalizedCastEvent(caster,"START",spell,duration)
  elseif ev=="TYS_CHANNEL_START" then SF.HandleNormalizedCastEvent(caster,"CHANNEL",spell,duration)
  elseif ev=="TYS_CAST_SUCCESS" then
    if nativeKind~=2 then SF.HandleNormalizedCastEvent(caster,"CAST",spell,duration) end
  elseif ev=="TYS_CAST_FAILED" then SF.HandleNormalizedCastEvent(caster,"FAIL",spell,duration)
  elseif ev=="TYS_CAST_INTERRUPTED" then SF.HandleNormalizedCastEvent(caster,"INTERRUPT",spell,duration) end
end

F:SetScript("OnEvent",function()
  if event=="VARIABLES_LOADED" then HandleVariablesLoaded()
  elseif event=="PLAYER_ENTERING_WORLD" then HandleEnteringWorld()
  elseif event=="PLAYER_LEAVING_WORLD" then Warn.test=false; Warn.SetState("NONE","SINGLE"); M2.ClearTest(false); M2.StopScanDriver(); ClearAll()
  elseif event=="PLAYER_LOGOUT" then Warn.Save(); SF.SaveRules()
  elseif event=="PLAYER_AURAS_CHANGED" then SF.RefreshSelfAuraHazards()
  elseif event=="CHAT_MSG_SPELL_AURA_GONE_SELF" then SF.HandleSelfAuraGone(arg1)
  elseif event=="TYS_CAST_START" or event=="TYS_CAST_SUCCESS" or event=="TYS_CAST_FAILED" or event=="TYS_CAST_INTERRUPTED" or event=="TYS_CHANNEL_START" then SF.HandleTysCastEvent(event) end
end)

OnUpdateHandler=function()
  if not running or not visualEnabled then return end
  local dt=arg1 or 0
  elapsed=elapsed+dt
  if elapsed>=(SF.refreshInterval or 0.20) then
    elapsed=0
    if HasNormalWork() then Poll() end
  end

  if Warn.enabled then
    Warn.elapsed=(Warn.elapsed or 0)+dt
    if Warn.dirty or Warn.elapsed>=(Warn.interval or 0.01) then
      Warn.elapsed=0; Warn.RefreshDanger()
    end
  else
    Warn.elapsed=0
  end

  -- Enemy-only operation must not pay totem maintenance costs. Pending discovery
  -- and exact totem lifetime checks tick only when there is actual totem work.
  if totemMode~="OFF" and table.getn(pendingTotems)>0 then
    totemDiscoverElapsed=totemDiscoverElapsed+dt
    if totemDiscoverElapsed>=0.06 then
      totemDiscoverElapsed=0
      DiscoverPendingTotems()
    end
  else
    totemDiscoverElapsed=0
  end

  if totemMode~="OFF" and table.getn(activeTotemKeys)>0 then
    totemLifeElapsed=totemLifeElapsed+dt
    if totemLifeElapsed>=0.25 then
      totemLifeElapsed=0
      CheckTotemLifetimes()
    end
  else
    totemLifeElapsed=0
  end

  if totemBootstrapAt and GetTime()>=totemBootstrapAt then
    totemBootstrapAt=nil
    local ok=BootstrapTotems()
    if not ok and totemBootstrapRetries<3 then
      totemBootstrapRetries=totemBootstrapRetries+1
      totemBootstrapAt=GetTime()+0.25
    else
      totemBootstrapRetries=0
    end
    if ApplyRuntimeHooks then ApplyRuntimeHooks() end
  end
end

-- Runtime hooks are attached after VARIABLES_LOADED; do not accept cast events
-- against pre-SavedVariables defaults during the addon load phase.

-- WoW 1.12 / Lua 5.0 compatibility note:
-- A single closure may not capture more than 32 upvalues. Keep slash commands split
-- into small handlers instead of one giant anonymous function.

local function SlashBasic(msg)
  if msg=="start" or msg=="on" then
    AutoRange_SetEnabled(true,true); panel:Show(); AutoRangeDB.hidden=false
  elseif msg=="stop" or msg=="off" then
    AutoRange_SetEnabled(false,true)
  elseif msg=="show" then
    panel:Show(); AutoRangeDB.hidden=false
  elseif msg=="hide" then
    Warn.test=false; Warn.UpdateVisibility(); M2.ClearTest(false); panel:Hide(); AutoRangeDB.hidden=true
  elseif msg=="visual on" then
    visualEnabled=true; AutoRangeDB.visualEnabled=true
    RefreshAllOwnedVisuals()
    if running and totemMode~="OFF" then totemBootstrapAt=GetTime()+0.05; totemBootstrapRetries=0 end
    if ApplyRuntimeHooks then ApplyRuntimeHooks() end
    Chat("危险圈视觉：开启"); Render()
  elseif msg=="visual off" then
    HideAllOwnedVisuals()
    visualEnabled=false; AutoRangeDB.visualEnabled=false
    if ApplyRuntimeHooks then ApplyRuntimeHooks() end
    Chat("危险圈视觉：关闭"); Render()
  elseif msg=="clear" then
    ClearAll()
  else
    return false
  end
  return true
end

local function SlashSources(msg)
  if msg=="enemy on" or msg=="hostile on" then
    SetEnemyCircle(true,true); Render()
  elseif msg=="enemy off" or msg=="hostile off" then
    SetEnemyCircle(false,true); Render()
  elseif msg=="friend on" then
    SetFriendCircle(true,true); Render()
  elseif msg=="friend off" then
    SetFriendCircle(false,true); Render()
  elseif string.find(msg,"^totem%s+") then
    local mode=string.gsub(msg,"^totem%s+","")
    mode=string.upper(mode)
    if mode=="PARTYONLY" then mode="PARTY_ONLY" end
    if not SetTotemMode(mode,true) then
      Chat("图腾模式：off | self | party | partyonly | raid | all")
    end
    Render()
  else
    return false
  end
  return true
end

local function AutoRangeSlashHandler(msg)
  msg=string.lower(tostring(msg or ""))
  msg=string.gsub(msg,"^%s+","")
  msg=string.gsub(msg,"%s+$","")

  if SlashBasic(msg) then return end
  if SlashSources(msg) then return end

  Chat("/arange on|off|show|hide|clear | enemy on|off | friend on|off | totem off|self|party|partyonly|raid|all")
end

SLASH_AUTORANGE1="/arange"
SlashCmdList["AUTORANGE"]=AutoRangeSlashHandler
