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

#ifndef _TC9_PLAYER_OPS_H
#define _TC9_PLAYER_OPS_H

#include "ObjectGuid.h"

#include <cstdint>

class GameObject;
class Player;
class Unit;

// Cross-server player operations. Acting on a player by name/GUID (GM summon,
// meeting stone, summoning ritual...) resolves the target through the local
// ObjectAccessor: a player live on another worldserver falls into the
// "offline" fallback and the action is lost. These helpers relay the
// operation over NATS to the worldserver that owns the live session; the
// resulting TeleportTo is then routed back to the right server by the
// gateway's transfer interception.
namespace TC9PlayerOps
{
    // World thread, idempotent; wired into ToCloud9Sidecar::ProcessHooks().
    void EnsureSubscribed();

    // The target could not be resolved locally: true when cluster mode is on
    // and the character row says it is online (= live on another server).
    // Callers use this to skip their offline fallback (which would write a
    // position the live session overwrites on its next save).
    bool IsLiveElsewhere(ObjectGuid target);

    // Ask the owning worldserver to teleport its live player. The move to a
    // map this server does not own is then handled by the gateway redirect.
    void RelayTeleport(ObjectGuid target, uint32 mapId, float x, float y, float z, float o, Player* requester);

    // Ask the owning worldserver to offer its live player a summon
    // (SetSummonPoint + SMSG_SUMMON_REQUEST); the accept runs there too.
    void RelaySummonRequest(ObjectGuid target, uint32 mapId, float x, float y, float z, uint32 zoneId, Player* requester);

    // Summoning ritual completion (meeting stone portal, warlock ritual): the
    // final summon spell resolves the caster's selection locally and would
    // fail on a cross-server target. True = the summon was relayed instead
    // (caller skips the final cast), false = proceed with the vanilla cast.
    bool RelayRitualSummonIfRemote(Unit* spellCaster, GameObject* portal, uint32 spellId);

    // GM summon while inside a dungeon, target live on another worldserver.
    // A direct relay to instance coordinates would make the remote server
    // evaluate dungeon-entry rules for a map it does not own, so the move is
    // done in two hops: relay to the dungeon's world-side entrance (the same
    // proven path as any cross-server teleport — the gateway redirects the
    // client here), then finish with a plain local summon when the target
    // logs in on this server. True = relay sent, false = preconditions not
    // met (caller reports the vanilla instance-summon error).
    bool RelayInstanceSummon(ObjectGuid target, Player* gm);

    // CharacterHandler login hook (this server only): if the character has a
    // pending instance summon, schedule its final local hop.
    void OnCharacterLoggedIn(Player* player);

    // World thread, every ProcessHooks tick: run the scheduled final hops
    // once their settle delay elapsed, re-validating everything locally.
    void ProcessPending();
}

#endif // _TC9_PLAYER_OPS_H
