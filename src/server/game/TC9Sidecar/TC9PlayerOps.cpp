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

#include "DatabaseEnv.h"
#include "GameObject.h"
#include "Group.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "Player.h"
#include "TC9Sidecar.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <cstdio>

namespace
{
    constexpr char PLAYER_OP_SUBJECT[] = "cluster.player.op";

    constexpr uint32 OP_TELEPORT = 1;
    constexpr uint32 OP_SUMMON_REQUEST = 2;

    constexpr uint32 SPELL_RITUAL_OF_SUMMONING_EFFECT = 7720;

    bool subscribed = false;

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
        Publish(OP_TELEPORT, target, mapId, x, y, z, o, 0, requester);
    }

    void RelaySummonRequest(ObjectGuid target, uint32 mapId, float x, float y, float z, uint32 zoneId, Player* requester)
    {
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

        if (!caster->GetGroup() || !caster->GetGroup()->IsMember(targetGuid) || !IsLiveElsewhere(targetGuid))
            return false;

        LOG_INFO("cluster", "PlayerOps: ritual by {} summons cross-server member {}",
                 caster->GetName(), targetGuid.GetCounter());

        RelaySummonRequest(targetGuid, portal->GetMapId(), portal->GetPositionX(),
                           portal->GetPositionY(), portal->GetPositionZ(), portal->GetZoneId(), caster);
        return true;
    }
}
