/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "TC9PlayerOps.h"

#include "Chat.h"
#include "DatabaseEnv.h"
#include "GameObject.h"
#include "GameTime.h"
#include "Group.h"
#include "InstanceSaveMgr.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "TC9Sidecar.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <cstdio>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr char PLAYER_OP_SUBJECT[] = "cluster.player.op";

    constexpr uint32 OP_TELEPORT = 1;
    constexpr uint32 OP_SUMMON_REQUEST = 2;

    constexpr uint32 SPELL_RITUAL_OF_SUMMONING_EFFECT = 7720;

    bool subscribed = false;

    // Instance summons relayed in two hops: hop 1 lands the target at the
    // dungeon's world entrance on this server; the login hook then schedules
    // hop 2, delayed a little because a TeleportTo issued during the login
    // sequence is dropped.
    constexpr time_t INSTANCE_SUMMON_TTL = 120;
    constexpr time_t INSTANCE_SUMMON_SETTLE = 2;

    struct PendingInstanceSummon
    {
        uint32 gmLow;
        time_t expires;
    };

    struct DueInstanceSummon
    {
        uint32 targetLow;
        uint32 gmLow;
        time_t due;
    };

    std::unordered_map<uint32 /*targetLow*/, PendingInstanceSummon> pendingInstanceSummons;
    std::vector<DueInstanceSummon> dueInstanceSummons;

    // Delivered on the world thread through TC9ProcessEventsHooks.
    void OnPlayerOp(char const* /*subject*/, char const* payload, int payloadLen)
    {
        uint32 op = 0;
        uint32 targetLow = 0;
        uint32 mapId = 0;
        float x = 0.f, y = 0.f, z = 0.f, o = 0.f;
        uint32 zoneId = 0;
        uint32 requesterLow = 0;

        std::string data(payload, payloadLen);
        if (sscanf(data.c_str(), "{\"op\":%u,\"t\":%u,\"m\":%u,\"x\":%f,\"y\":%f,\"z\":%f,\"o\":%f,\"zn\":%u,\"s\":%u}",
                   &op, &targetLow, &mapId, &x, &y, &z, &o, &zoneId, &requesterLow) != 9)
            return;

        Player* target = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(targetLow));
        if (!target || !target->GetSession())
            return;  // not ours: the owning worldserver picks it up

        switch (op)
        {
            case OP_TELEPORT:
            {
                if (target->IsBeingTeleported())
                    return;

                LOG_INFO("cluster", "PlayerOps: relayed teleport of {} to map {} ({:.1f} {:.1f} {:.1f})",
                         target->GetName(), mapId, x, y, z);

                if (target->IsInFlight())
                {
                    target->GetMotionMaster()->MovementExpired();
                    target->CleanupAfterTaxiFlight();
                }
                else
                    target->SaveRecallPosition();

                target->TeleportTo(mapId, x, y, z, o);
                return;
            }
            case OP_SUMMON_REQUEST:
            {
                LOG_INFO("cluster", "PlayerOps: relayed summon request for {} to map {} zone {}",
                         target->GetName(), mapId, zoneId);

                target->SetSummonPoint(mapId, x, y, z);

                WorldPacket packet(SMSG_SUMMON_REQUEST, 8 + 4 + 4);
                packet << ObjectGuid::Create<HighGuid::Player>(requesterLow);
                packet << uint32(zoneId);
                packet << uint32(MAX_PLAYER_SUMMON_DELAY * IN_MILLISECONDS);
                target->SendDirectMessage(&packet);
                return;
            }
            default:
                return;
        }
    }

    void Publish(uint32 op, ObjectGuid target, uint32 mapId, float x, float y, float z, float o,
                 uint32 zoneId, Player* requester)
    {
        char payload[192];
        int len = snprintf(payload, sizeof(payload),
                           "{\"op\":%u,\"t\":%u,\"m\":%u,\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,\"o\":%.3f,\"zn\":%u,\"s\":%u}",
                           op, target.GetCounter(), mapId, x, y, z, o, zoneId,
                           requester ? requester->GetGUID().GetCounter() : 0);
        sToCloud9Sidecar->NatsPublish(PLAYER_OP_SUBJECT, std::string(payload, len));
    }
}

namespace TC9PlayerOps
{
    void EnsureSubscribed()
    {
        if (subscribed)
            return;

        subscribed = sToCloud9Sidecar->NatsSubscribe(PLAYER_OP_SUBJECT, &OnPlayerOp);
    }

    bool IsLiveElsewhere(ObjectGuid target)
    {
        if (!sToCloud9Sidecar->ClusterModeEnabled() || !target.IsPlayer())
            return false;

        // The row is authoritative enough here: the online flag only lags
        // across a redirect window, and a false negative just means the
        // vanilla offline fallback (today's behavior).
        QueryResult result = CharacterDatabase.Query("SELECT online FROM characters WHERE guid = {}", target.GetCounter());
        return result && (*result)[0].Get<uint8>() != 0;
    }

    void RelayTeleport(ObjectGuid target, uint32 mapId, float x, float y, float z, float o, Player* requester)
    {
        if (!sToCloud9Sidecar->ClusterModeEnabled())
            return;

        Publish(OP_TELEPORT, target, mapId, x, y, z, o, 0, requester);
    }

    void RelaySummonRequest(ObjectGuid target, uint32 mapId, float x, float y, float z, uint32 zoneId, Player* requester)
    {
        if (!sToCloud9Sidecar->ClusterModeEnabled())
            return;

        Publish(OP_SUMMON_REQUEST, target, mapId, x, y, z, 0.f, zoneId, requester);
    }

    bool RelayRitualSummonIfRemote(Unit* spellCaster, GameObject* portal, uint32 spellId)
    {
        if (!sToCloud9Sidecar->ClusterModeEnabled() || spellId != SPELL_RITUAL_OF_SUMMONING_EFFECT)
            return false;

        Player* caster = spellCaster->ToPlayer();
        if (!caster)
            return false;

        ObjectGuid targetGuid = caster->GetTarget();
        if (!targetGuid || ObjectAccessor::FindPlayer(targetGuid))
            return false;  // no selection, or resolvable locally: vanilla cast handles it

        // No IsLiveElsewhere gate: the online flag can stay stale for a whole
        // session (BUG-TC9-067) and would silently fizzle the ritual. For a
        // truly offline member the relayed request just finds no taker.
        if (!caster->GetGroup() || !caster->GetGroup()->IsMember(targetGuid))
            return false;

        LOG_INFO("cluster", "PlayerOps: ritual by {} summons cross-server member {}",
                 caster->GetName(), targetGuid.GetCounter());

        RelaySummonRequest(targetGuid, portal->GetMapId(), portal->GetPositionX(),
                           portal->GetPositionY(), portal->GetPositionZ(), portal->GetZoneId(), caster);
        return true;
    }

    bool RelayInstanceSummon(ObjectGuid target, Player* gm)
    {
        if (!sToCloud9Sidecar->ClusterModeEnabled() || !gm)
            return false;

        Map* map = gm->GetMap();
        if (!map->IsDungeon())
            return false;  // battlegrounds and arenas keep their refusal

        // The strong vanilla precondition that IS checkable from here: the GM
        // leads a group the target belongs to (groups are mirrored across
        // worldservers). Instance.GMSummonPlayer waives it, as it does for
        // the local branch. The rest re-runs locally at hop 2.
        if (!sWorld->getBoolConfig(CONFIG_INSTANCE_GMSUMMON_PLAYER))
        {
            Group* group = gm->GetGroup();
            if (!group || group->GetLeaderGUID() != gm->GetGUID() || !group->IsMember(target))
                return false;
        }

        AreaTriggerTeleport const* entrance = sObjectMgr->GetGoBackTrigger(map->GetId());
        if (!entrance)
            return false;

        LOG_INFO("cluster", "PlayerOps: instance summon of {} by {} — hop 1 to entrance of map {}",
                 target.GetCounter(), gm->GetName(), map->GetId());

        pendingInstanceSummons[target.GetCounter()] =
            { gm->GetGUID().GetCounter(), GameTime::GetGameTime().count() + INSTANCE_SUMMON_TTL };
        RelayTeleport(target, entrance->target_mapId, entrance->target_X, entrance->target_Y,
                      entrance->target_Z, entrance->target_Orientation, gm);
        return true;
    }

    void OnCharacterLoggedIn(Player* player)
    {
        if (pendingInstanceSummons.empty() || !player)
            return;

        auto it = pendingInstanceSummons.find(player->GetGUID().GetCounter());
        if (it == pendingInstanceSummons.end())
            return;

        PendingInstanceSummon pending = it->second;
        pendingInstanceSummons.erase(it);

        time_t now = GameTime::GetGameTime().count();
        if (pending.expires < now)
            return;

        dueInstanceSummons.push_back({ player->GetGUID().GetCounter(), pending.gmLow,
                                       now + INSTANCE_SUMMON_SETTLE });
    }

    void ProcessPending()
    {
        if (dueInstanceSummons.empty() && pendingInstanceSummons.empty())
            return;

        time_t now = GameTime::GetGameTime().count();

        for (auto it = pendingInstanceSummons.begin(); it != pendingInstanceSummons.end();)
        {
            if (it->second.expires < now)
                it = pendingInstanceSummons.erase(it);
            else
                ++it;
        }

        for (auto it = dueInstanceSummons.begin(); it != dueInstanceSummons.end();)
        {
            if (it->due > now)
            {
                ++it;
                continue;
            }

            DueInstanceSummon due = *it;
            it = dueInstanceSummons.erase(it);

            Player* target = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(due.targetLow));
            Player* gm = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(due.gmLow));
            if (!target || !target->GetSession() || target->IsBeingTeleported() || !gm)
                continue;

            Map* map = gm->GetMap();
            if (!map->IsDungeon())
                continue;

            // Hop 2 = the vanilla local .summon instance branch, now that both
            // sides live on this worldserver.
            if (!sWorld->getBoolConfig(CONFIG_INSTANCE_GMSUMMON_PLAYER) && !target->GetSession()->GetSecurity())
            {
                ChatHandler(gm->GetSession()).SendSysMessage("Only GMs can be summoned to an instance!");
                continue;
            }

            if (!sWorld->getBoolConfig(CONFIG_INSTANCE_GMSUMMON_PLAYER) &&
                (!gm->GetGroup() || target->GetGroup() != gm->GetGroup() ||
                 gm->GetGroup()->GetLeaderGUID() != gm->GetGUID()))
                continue;

            Map* destMap = target->GetMap();
            if (destMap->Instanceable() && destMap->GetInstanceId() != map->GetInstanceId())
                sInstanceSaveMgr->PlayerUnbindInstance(target->GetGUID(), map->GetInstanceId(),
                                                       target->GetDungeonDifficulty(), true, target);

            LOG_INFO("cluster", "PlayerOps: instance summon of {} by {} — hop 2 into map {} instance {}",
                     target->GetName(), gm->GetName(), map->GetId(), map->GetInstanceId());

            float x, y, z;
            gm->GetClosePoint(x, y, z, target->GetObjectSize());
            target->TeleportTo(gm->GetMapId(), x, y, z, target->GetOrientation(), 0, gm);
        }
    }
}
