/**
* Copyright (c) 2011-2014 - Ashita Development Team
*
* Ashita is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* Ashita is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with Ashita.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "Deeps.h"
#include "DSP-Utils.h"

#include <algorithm>
#include <thread>

source_t* Deeps::getDamageSource(entitysources_t* entityInfo, uint8_t actionType, uint16_t actionID)
{
    uint32_t key = (actionID << 8) + actionType;
    auto sourcesIt = entityInfo->sources.find(key);

    source_t* source;

    if (sourcesIt != entityInfo->sources.end())
    {
        source = &sourcesIt->second;
    }
    else
    {
        source_t newsource;

        sourcesIt = entityInfo->sources.insert(std::make_pair(key, newsource)).first;

        source = &sourcesIt->second;
        if (actionType == 1) { source->name.append("Attack"); }
        else if (actionType == 2) { source->name.append("Ranged Attack"); }
        else if (actionType == 3 || actionType == 11){ source->name.append(m_AshitaCore->GetResourceManager()->GetAbilityByID(actionID)->Name[2]); }
        else if (actionType == 4) { source->name.append(m_AshitaCore->GetResourceManager()->GetSpellByID(actionID)->Name[2]); }
        else if (actionType == 6 || actionType == 14 || actionType == 15) { source->name.append(m_AshitaCore->GetResourceManager()->GetAbilityByID(actionID + 512)->Name[2]); }

    }
    return source;
}

bool Deeps::updateDamageSource(source_t* source, uint16_t message, uint32_t damage)
{
    damage_t* type = NULL;
    bool val = false;
    if (std::find(hitMessages.begin(), hitMessages.end(), message) != hitMessages.end())
    {
        type = &source->damage["Hit"];
        val = true;
    }
    else if (std::find(critMessages.begin(), critMessages.end(), message) != critMessages.end())
    {
        type = &source->damage["Crit"];
        val = true;
    }
    else if (std::find(missMessages.begin(), missMessages.end(), message) != missMessages.end())
    {
        type = &source->damage["Miss"];
    }
    else if (std::find(evadeMessages.begin(), evadeMessages.end(), message) != evadeMessages.end())
    {
        type = &source->damage["Evade"];
    }
    else if (std::find(parryMessages.begin(), parryMessages.end(), message) != parryMessages.end())
    {
        type = &source->damage["Parry"];
    }
    if (type)
    {
        damage = val ? damage : 0;
        type->total += damage;
        type->count++;
        type->min = (damage < type->min ? damage : type->min);
        type->max = (damage > type->max ? damage : type->max);
        return true;
    }
    return false;
}

uint16_t Deeps::getIndex(std::function<bool(IEntity*, int)> func)
{
    for (int i = 0; i < 2048; i++)
    {
        if (func(m_AshitaCore->GetDataManager()->GetEntity(), i))
        {
            return i;
        }
    }
    return 0;
}

/**
 * @brief Constructor and Deconstructor
 */
Deeps::Deeps(void)
: m_AshitaCore(NULL)
, m_PluginId(0)
, m_Direct3DDevice(NULL)
{ }
Deeps::~Deeps(void)
{ }

/**
 * @brief Obtains the plugin data for this plugin.
 *
 * @return The PluginData structure for this plugin.
 */
PluginData Deeps::GetPluginData(void)
{
    return (*g_PluginData);
}

/**
 * @brief Initializes our plugin. This is the main call that happens when your plugin is loaded.
 *
 * @param ashitaCore        The main Ashita Core object interface to interact with Ashita.
 * @param scriptEngine      The main script engine object interface to interact with the script engine.
 * @param dwPluginId        The base address of your plugin. This is used as the ID.
 *
 * @return True on success, false otherwise.
 *
 * @note If your plugin returns false here, it will be unloaded immediately!
 */
bool Deeps::Initialize(IAshitaCore* ashitaCore, DWORD dwPluginId)
{
    // Store the variables we are passed..
    this->m_AshitaCore = ashitaCore;
    this->m_PluginId = dwPluginId;
    g_Deeps = this;
    srand(time(NULL));

    m_charInfo = 0;
    m_bars = 0;

    return true;
}

/**
 * @brief Releases this plugin. This is called when your plugin is unloaded.
 *
 * @note Your plugin should cleanup all its data here before it unloads. Anything such as:
 *          - Font objects.
 *          - Gui objects.
 *          - Bindings to the script engine (if you extended it any).
 */
void Deeps::Release(void)
{
}

/**
 * @brief Allows a plugin to attempt to handle a game command.
 *
 * @param pszCommand            The command being processed.
 * @param nCommandType          The type of command being processed.
 *
 * @return True on handled, false otherwise.
 */
bool Deeps::HandleCommand(const char* pszCommand, int nCommandType)
{
    std::vector<std::string> args;
    auto count = Ashita::Commands::GetCommandArgs(pszCommand, &args);
    if (count <= 0) return false;
    HANDLECOMMAND("/deeps", "/dps")
    {
        if (count >= 2)
        {
            if (args[1] == "reset")
            {
                entities.clear();
                m_sourceInfo.clear();
                m_charInfo = 0;
                return true;
            }
            else if (args[1] == "report")
            {
                char mode = 0x00;
                int max = 3;
                if (count > 2)
                {
                    if (std::all_of(args[2].begin(), args[2].end(), ::isdigit))
                    {
                        max = atoi(args[2].c_str());
                    }
                    else
                    {
                        mode = args[2][0];
                        if (count > 3)
                        {
                            if (std::all_of(args[2].begin(), args[2].end(), ::isdigit))
                            {
                                max = atoi(args[2].c_str());
                            }
                        }
                    }
                }

                std::thread(&Deeps::report, this, mode, max).detach();

                return true;
            }
            else if (args[1] == "debug")
            {
                m_debug = !m_debug;
                if (m_debug)
                {
                    m_AshitaCore->GetChatManager()->AddChatMessage(5, "Deeps: Debug on");
                }
                else
                {
                    m_AshitaCore->GetChatManager()->AddChatMessage(5, "Deeps: Debug off");
                }
                return true;
            }
        }
        m_AshitaCore->GetChatManager()->AddChatMessage(5, "Deeps usage: /dps reset, /dps report [s/p/l] [#]");
        return true;
    }
    return false;
}

void Deeps::report(char mode, int max)
{
    IFontObject* deepsBase = m_AshitaCore->GetFontManager()->GetFontObject("DeepsBackground");
    if (deepsBase)
    {
        std::string line;
        char buff[256];
        if (mode != 0x00)
        {
            sprintf_s(buff, 256, "/%c ", mode);
            line.append(buff);
        }
        line.append(deepsBase->GetText().ascii().c_str() + 1);
        m_AshitaCore->GetChatManager()->QueueCommand(line.c_str(), Ashita::Enums::Typed);
        for (int i = 0; i < m_bars; i++)
        {
            if (i > max)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1100));
            line.clear();
            memset(buff, sizeof buff, 0);
            if (mode != 0x00)
            {
                sprintf_s(buff, 256, "/%c ", mode);
                line.append(buff);
            }
            sprintf_s(buff, 256, "%d -", i + 1);
            line.append(buff);
            char name[32];
            sprintf_s(name, 32, "DeepsBar%d", i);
            IFontObject* bar = m_AshitaCore->GetFontManager()->GetFontObject(name);
            line.append(bar->GetText().ascii());
            m_AshitaCore->GetChatManager()->QueueCommand(line.c_str(), Ashita::Enums::Typed);
        }
    }
}

/**
 * @brief Allows a plugin to attempt to handle a new chat line.
 *
 * @param sMode                 The chat type being added.
 * @param pszChatLine           The chat line being added.
 *
 * @return True on handled, false otherwise.
 */
bool Deeps::HandleNewChatLine(short sMode, char* pszChatLine)
{
    return false;
}

/**
 * @brief Allows a plugin to attempt to handle an incoming packet.
 *
 * @param uiPacketId            The id of the packet.
 * @param uiPacketSize          The size of the packet.
 * @param lpRawData             The raw packet data.
 *
 * @return True on handled, false otherwise.
 *
 * @note    Returning true on this will block the packet from being handled! This can
 *          have undesired effects! Use with caution as this can get you banned!
 */
bool Deeps::HandleIncomingPacket(unsigned int uiPacketId, unsigned int uiPacketSize, void* pData)
{
    if (uiPacketId == 0x28) //action
    {
        uint8_t actionNum = (uint8_t)(unpackBitsBE((unsigned char*)pData, 182, 4));
        uint8_t targetNum = RBUFB(pData, 0x09);
        uint8_t actionType = (uint8_t)(unpackBitsBE((unsigned char*)pData, 82, 4));
        //uint8_t reaction = 0;
        //uint8_t speceffect = 0;
        uint16_t actionID = (uint16_t)(unpackBitsBE((unsigned char*)pData, 86, 10));
        uint32_t userID = RBUFL(pData, 0x05);
        uint16_t startBit = 150;
        uint16_t damage = 0;

        if (userID > 0x1000000)
            return false;

        auto it = entities.find(userID);
        entitysources_t* entityInfo = NULL;

        if (it != entities.end())
        {
            entityInfo = &it->second;
        }
        else
        {
            uint16_t index = getIndex([&](IEntity* entities, int i){if (entities->GetServerID(i) == userID) return true; return false; });
            if (index != 0)
            {
                entitysources_t newInfo;
                newInfo.name = m_AshitaCore->GetDataManager()->GetEntity()->GetName(index);
                newInfo.color = Colors[rand() % Colors.size()];
                entityInfo = &entities.insert(std::make_pair(userID, newInfo)).first->second;
            }
        }

        if (entityInfo)
        {
            if (m_debug)
            {
                m_AshitaCore->GetChatManager()->Write("Action Type: %d Action ID: %d", actionType, actionID);
            }

            if ((actionType >= 1 && actionType <= 4) || (actionType == 6) || (actionType == 11) || (actionType == 14) || (actionType == 15))
            {
                if (actionID == 0)
                    return false;
                source_t* source = getDamageSource(entityInfo, actionType, actionID);
                uint16_t messageID = (uint16_t)(unpackBitsBE((unsigned char*)pData, startBit + 80, 10));

                uint32_t addEffectDamage = 0;
                uint8_t addEffectCount = 0;
                uint16_t addMessageID = 0;
                for (int i = 0; i < targetNum; i++)
                {
                    for (int j = 0; j < actionNum; j++)
                    {
                        uint8_t reaction = (uint8_t)(unpackBitsBE((unsigned char*)pData, startBit + 36, 5));
                        uint16_t animation = (uint16_t)(unpackBitsBE((unsigned char*)pData, startBit + 41, 12));
                        uint32_t mainDamage = (uint16_t)(unpackBitsBE((unsigned char*)pData, startBit + 63, 17));
                        uint8_t speceffect = (uint8_t)(unpackBitsBE((unsigned char*)pData, startBit + 53, 9));

                        if (m_debug)
                        {
                            m_AshitaCore->GetChatManager()->Write("Reaction: %d Animation: %d", reaction, animation);
                            m_AshitaCore->GetChatManager()->Write("Speceffect: %d Param: %d", speceffect, mainDamage);
                        }

                        //Daken (ranged attack on attack)
                        if (actionType == 1 && animation == 4)
                            source = getDamageSource(entityInfo, actionType+1, actionID);

                        if (!updateDamageSource(source, messageID, mainDamage))
                            return false;

                        if ((unpackBitsBE((unsigned char*)pData, startBit + 121, 1) & 0x1) && actionType != 6)
                        {
                            addMessageID = (uint16_t)(unpackBitsBE((unsigned char*)pData, startBit + 149, 10));
                            if (addMessageID == 163 || addMessageID == 229 || (addMessageID >= 288 && addMessageID <= 302))
                            {
                                addEffectDamage = (uint16_t)(unpackBitsBE((unsigned char*)pData, startBit + 132, 16));
                                uint32_t key = 0;
                                if (addMessageID == 163 || addMessageID == 229)
                                    key = 1 << 8;
                                else
                                    key = 2 << 8;
                                auto sourcesIt = entityInfo->sources.find(key);

                                source_t* source;

                                if (sourcesIt != entityInfo->sources.end())
                                {
                                    source = &sourcesIt->second;
                                }
                                else
                                {
                                    source_t newsource;
                                    if (key == 1 << 8) { newsource.name.append("Additional Effect"); }
                                    else { newsource.name.append("Skillchain"); }

                                    sourcesIt = entityInfo->sources.insert(std::make_pair(key, newsource)).first;
                                    source = &sourcesIt->second;
                                }
                                source->damage["Hit"].count += 1;
                                source->damage["Hit"].total += addEffectDamage;
                                source->damage["Hit"].min = (addEffectDamage < source->damage["Hit"].min ? addEffectDamage : source->damage["Hit"].min);
                                source->damage["Hit"].max = (addEffectDamage > source->damage["Hit"].max ? addEffectDamage : source->damage["Hit"].max);
                            }

                            startBit += 37;
                        }
                        startBit += 1;
                        if (unpackBitsBE((unsigned char*)pData, startBit + 121, 1) & 0x1)
                        {
                            startBit += 34;
                        }
                        startBit += 86;
                    }
                    startBit += 36;
                }
            }
        }
    }
    return false;
}

/**
 * @brief Allows a plugin to attempt to handle an outgoing packet.
 *
 * @param uiPacketId            The id of the packet.
 * @param uiPacketSize          The size of the packet.
 * @param lpRawData             The raw packet data.
 *
 * @return True on handled, false otherwise.
 *
 * @note    Returning true on this will block the packet from being handled! This can
 *          have undesired effects! Use with caution as this can get you banned!
 */
bool Deeps::HandleOutgoingPacket(unsigned int uiPacketId, unsigned int uiPacketSize, void* lpRawData)
{
    return false;
}

/**
 * @brief Direct3D initialize call to prepare this plugin for Direct3D calls.
 *
 * @param lpDevice              The Direct3D device currently wrapped by Ashita.
 *
 * @return True on success, false otherwise.
 *
 * @note    Plugins that do not return true on this call will not receive any other
 *          Direct3D calls listed below!
 */
bool Deeps::Direct3DInitialize(IDirect3DDevice8* lpDevice)
{
    this->m_Direct3DDevice = lpDevice;

    float xpos = m_AshitaCore->GetConfigurationManager()->GetConfigFloat("Deeps", "xpos", 300.0f);
    float ypos = m_AshitaCore->GetConfigurationManager()->GetConfigFloat("Deeps", "ypos", 300.0f);

    IFontObject* font = m_AshitaCore->GetFontManager()->CreateFontObject("DeepsBackground");
    font->SetFont("Consolas", 10);
    font->SetAutoResize(false);
    font->GetBackground()->SetColor(D3DCOLOR_ARGB(0xCC, 0x00, 0x00, 0x00));
    font->GetBackground()->SetVisibility(true);
    font->GetBackground()->SetWidth(158);
    font->GetBackground()->SetHeight(256);
    font->SetColor(D3DCOLOR_ARGB(0xFF, 0xFF, 0xFF, 0xFF));
    font->SetBold(false);
    font->SetText("");
    font->SetPosition(xpos, ypos);
    font->SetVisibility(true);
    font->SetClickFunction(g_onClick);

    return true;
}

/**
 * @brief Direct3D release call to allow this plugin to cleanup any Direct3D objects.
 */
void Deeps::Direct3DRelease(void)
{
    IFontObject* deepsBase = m_AshitaCore->GetFontManager()->GetFontObject("DeepsBackground");

    m_AshitaCore->GetConfigurationManager()->SetConfigValue("Deeps", "xpos", std::to_string(deepsBase->GetPositionX()).c_str());
    m_AshitaCore->GetConfigurationManager()->SetConfigValue("Deeps", "ypos", std::to_string(deepsBase->GetPositionY()).c_str());
    m_AshitaCore->GetConfigurationManager()->SaveConfiguration("Deeps");

    m_AshitaCore->GetFontManager()->DeleteFontObject("DeepsBackground");

    for (int i = 0; i < m_bars; i++)
    {
        char name[32];
        sprintf_s(name, 32, "DeepsBar%d", i);
        m_AshitaCore->GetFontManager()->DeleteFontObject(name);
        memset(name, 0, sizeof name);
        sprintf_s(name, 32, "DeepsBarClick%d", i);
        m_AshitaCore->GetFontManager()->DeleteFontObject(name);
    }
}

/**
 * @brief Direct3D prerender call to allow this plugin to prepare for rendering.
 *
 * @note This will only be called if you returned true in Direct3DInitialize!
 */
void Deeps::Direct3DPreRender(void)
{
}

/**
 * @brief Direct3D render call to allow this plugin to render any custom things.
 *
 * @note This will only be called if you returned true in Direct3DInitialize!
 */
void Deeps::Direct3DRender(void)
{
    IFontObject* deepsBase = m_AshitaCore->GetFontManager()->GetFontObject("DeepsBackground");

    if (m_charInfo == 0)
    {
        deepsBase->SetText(" Deeps - Damage Done");
        deepsBase->GetBackground()->SetWidth(158);
        std::vector<entitysources_t> temp;
        uint64_t total = 0;
        for (auto e : entities)
        {
            if (e.second.total() != 0 && temp.size() < 15)
            {
                temp.push_back(e.second);
                total += e.second.total();
            }
        }
        std::sort(temp.begin(), temp.end(), [](entitysources_t a, entitysources_t b){return a > b; });
        repairBars(deepsBase, temp.size());

        int i = 0;
        uint64_t max = 0;
        clickMap.clear();
        for (auto e : temp)
        {
            char name[32];
            sprintf_s(name, 32, "DeepsBar%d", i);
            IFontObject* bar = m_AshitaCore->GetFontManager()->GetFontObject(name);
            if (e.total() > max) max = e.total();
            bar->GetBackground()->SetWidth(150 * (total == 0 ? 1 : ((float)e.total() / (float)max)));
            bar->GetBackground()->SetColor(e.color);
            char string[256];
            sprintf_s(string, 256, " %-10.10s %6llu %03.1f%%\n", e.name.c_str(), e.total(), total == 0 ? 0 : 100 * ((float)e.total() / (float)total));
            bar->SetText(string);
            memset(name, 0, sizeof name);
            sprintf_s(name, 32, "DeepsBarClick%d", i);
            bar = m_AshitaCore->GetFontManager()->GetFontObject(name);
            bar->GetBackground()->SetWidth(150);
            clickMap.insert(std::pair<IFontObject*, std::string>(bar, e.name));
            i++;
        }
    }
    else
    {
        auto it = entities.find(m_charInfo);
        if (it != entities.end())
        {
            if (m_sourceInfo == "")
            {
                std::vector<source_t> temp;
                uint64_t total = 0;
                for (auto s : it->second.sources)
                {
                    if (s.second.total() != 0 && temp.size() < 15)
                    {
                        temp.push_back(s.second);
                        total += s.second.total();
                    }
                }
                std::sort(temp.begin(), temp.end(), [](source_t a, source_t b){return a > b; });
                char string[256];
                sprintf_s(string, 256, " %s - Sources\n", it->second.name.c_str());
                deepsBase->SetText(string);
                deepsBase->GetBackground()->SetWidth(158);

                repairBars(deepsBase, temp.size());
                int i = 0;
                uint64_t max = 0;
                clickMap.clear();
                for (auto s : temp)
                {
                    char name[32];
                    sprintf_s(name, 32, "DeepsBar%d", i);
                    IFontObject* bar = m_AshitaCore->GetFontManager()->GetFontObject(name);
                    if (s.total() > max) max = s.total();
                    bar->GetBackground()->SetWidth(150 * (total == 0 ? 1 : ((float)s.total() / (float)max)));
                    bar->GetBackground()->SetColor(it->second.color);
                    char string[256];
                    sprintf_s(string, 256, " %-10.10s %6llu %03.1f%%\n", s.name.c_str(), s.total(), total == 0 ? 0 : 100 * ((float)s.total() / (float)total));
                    bar->SetText(string);
                    memset(name, 0, sizeof name);
                    sprintf_s(name, 32, "DeepsBarClick%d", i);
                    bar = m_AshitaCore->GetFontManager()->GetFontObject(name);
                    bar->GetBackground()->SetWidth(150);
                    clickMap.insert(std::pair<IFontObject*, std::string>(bar, s.name));
                    i++;
                }
            }
            else
            {
                for (auto s : it->second.sources)
                {
                    if (s.second.name == m_sourceInfo)
                    {
                        std::vector<std::pair<const char*, damage_t> > temp;
                        uint32_t count = 0;
                        for (auto d : s.second.damage)
                        {
                            if (d.second.count != 0 && temp.size() < 15)
                            {
                                temp.push_back(d);
                                count += d.second.count;
                            }
                        }
                        std::sort(temp.begin(), temp.end(), [](std::pair<const char*, damage_t> a, std::pair<const char*, damage_t> b){return a.second > b.second; });
                        char string[256];
                        sprintf_s(string, 256, " %s - %s\n", it->second.name.c_str(), s.second.name.c_str());
                        deepsBase->SetText(string);
                        deepsBase->GetBackground()->SetWidth(262);
                        repairBars(deepsBase, temp.size());
                        int i = 0;
                        uint32_t max = 0;
                        for (auto s : temp)
                        {
                            char name[32];
                            sprintf_s(name, 32, "DeepsBar%d", i);
                            IFontObject* bar = m_AshitaCore->GetFontManager()->GetFontObject(name);
                            if (s.second.count > max) max = s.second.count;
                            bar->GetBackground()->SetWidth(254 * (count == 0 ? 1 : 1 * ((float)s.second.count / (float)max)));
                            bar->GetBackground()->SetColor(it->second.color);
                            char string[256];
                            sprintf_s(string, 256, " %-5s Cnt:%4d Avg:%5d Max:%5d %3.1f%%\n", s.first, s.second.count, s.second.avg(), s.second.max, count == 0 ? 0 : 100 * ((float)s.second.count / (float)count));
                            bar->SetText(string);
                            i++;
                        }
                        break;
                    }
                }
            }
        }
    }
    deepsBase->GetBackground()->SetHeight(m_bars * 16 + 17);
}

void Deeps::repairBars(IFontObject* deepsBase, uint8_t size)
{
    IFontObject* previous = deepsBase;
    if (m_AshitaCore->GetFontManager()->GetFontObject("DeepsBar0") != NULL)
    {
        char name[32] = "DeepsBar0";
        int i = 1;
        while (m_AshitaCore->GetFontManager()->GetFontObject(name) != NULL)
        {
            previous = m_AshitaCore->GetFontManager()->GetFontObject(name);
            sprintf_s(name, 32, "DeepsBar%d", i);
            i++;
        }
    }
    while (m_bars != size)
    {
        if (m_bars > size)
        {
            char name[32];
            sprintf_s(name, 32, "DeepsBar%d", m_bars - 1);
            m_AshitaCore->GetFontManager()->DeleteFontObject(name);
            memset(name, 0, sizeof name);
            sprintf_s(name, 32, "DeepsBarClick%d", m_bars - 1);
            m_AshitaCore->GetFontManager()->DeleteFontObject(name);
            m_bars--;
        }
        else if (m_bars < size)
        {
            char name[32];
            sprintf_s(name, 32, "DeepsBar%d", m_bars);
            IFontObject* bar = m_AshitaCore->GetFontManager()->CreateFontObject(name);
            bar->SetParent(previous);
            if (previous == deepsBase)
            {
                bar->SetPosition(4, 15);
            }
            else
            {
                bar->SetAnchorParent(Ashita::Enums::BottomLeft);
                bar->SetPosition(0, 3);
            }
            bar->SetAutoResize(false);
            bar->SetFont("Consolas", 8);
            bar->GetBackground()->SetColor(D3DCOLOR_ARGB(0xFF, 0x00, 0x7C, 0x5C));
            bar->GetBackground()->SetVisibility(true);
            std::string path = m_AshitaCore->GetAshitaInstallPathA();
            path.append("\\Resources\\Deeps\\bar.tga");
            bar->GetBackground()->SetTextureFromFile(path.c_str());
            bar->GetBackground()->SetWidth(254);
            bar->GetBackground()->SetHeight(13);
            bar->SetVisibility(true);

            memset(name, 0, sizeof name);
            sprintf_s(name, 32, "DeepsBarClick%d", m_bars);
            IFontObject* clickBar = m_AshitaCore->GetFontManager()->CreateFontObject(name);
            clickBar->SetParent(bar);
            clickBar->SetPosition(0, 0);
            clickBar->SetAutoResize(false);
            clickBar->GetBackground()->SetColor(D3DCOLOR_ARGB(0x00, 0x00, 0x00, 0x00));
            clickBar->GetBackground()->SetVisibility(true);
            clickBar->GetBackground()->SetWidth(254);
            clickBar->GetBackground()->SetHeight(13);
            clickBar->SetVisibility(true);
            clickBar->SetClickFunction(g_onClick);

            m_bars++;
            previous = bar;
        }
    }
}

void Deeps::onClick(int type, IFontObject* font, float xPos, float yPos)
{
    if (type == 1 && font == m_AshitaCore->GetFontManager()->GetFontObject("DeepsBackground"))
    {
        if (m_sourceInfo != "")
        {
            m_sourceInfo = "";
        }
        else
        {
            m_charInfo = 0;
        }
        return;
    }

    if (m_charInfo == 0)
    {
        //Char was clicked
        if (type == 0)
        {
            // left click
            try
            {
                auto name = clickMap.at(font);
                for (auto entity : entities)
                {
                    if (entity.second.name == name)
                    {
                        m_charInfo = entity.first;
                        break;
                    }
                }
            }
            catch (...)
            {
                return;
            }
        }
    }
    else
    {
        if (m_sourceInfo == "")
        {
            //source was clicked
            if (type == 0)
            {
                try
                {
                    auto name = clickMap.at(font);
                    m_sourceInfo.assign(name);
                }
                catch (...)
                {
                    return;
                }
            }
        }
    }
}

/**
 * @brief Gets the interface version this plugin was compiled with.
 *
 * @note This is a required export, your plugin must implement this!
 */
__declspec(dllexport) double __stdcall GetInterfaceVersion(void)
{
    return ASHITA_INTERFACE_VERSION;
}

/**
 * @brief Gets the plugin data for this plugin.
 *
 * @note This is a required export, your plugin must implement this!
 */
__declspec(dllexport) void __stdcall CreatePluginData(PluginData* lpBuffer)
{
    g_PluginData = lpBuffer;

    strcpy_s(g_PluginData->Name, sizeof(g_PluginData->Name), "Deeps");
    strcpy_s(g_PluginData->Author, sizeof(g_PluginData->Author), "kjLotus");

    g_PluginData->InterfaceVersion = ASHITA_INTERFACE_VERSION;
    g_PluginData->PluginVersion = 2.03f;
    g_PluginData->Priority = 0;
}

/**
 * @brief Creates an instance of this plugin object.
 *
 * @note This is a required export, your plugin must implement this!
 */
__declspec(dllexport) IPlugin* __stdcall CreatePlugin(char* pszReserved)
{
    UNREFERENCED_PARAMETER(pszReserved);
    return (IPlugin*)new Deeps();
}

void g_onClick(int type, void* font, float xPos, float yPos)
{
    g_Deeps->onClick(type, (IFontObject*)font, xPos, yPos);
}