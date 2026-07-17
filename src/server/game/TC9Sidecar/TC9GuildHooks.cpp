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

#include "TC9GuildHooks.h"
#include "CharacterDatabase.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Item.h"
#include "ObjectAccessor.h"
#include "PetitionMgr.h"
#include "Player.h"
#include "WorldSession.h"

void ToCloud9GuildHooks::OnGuildMemberAdded(uint64 guild, uint64 character)
{
    Player *player = ObjectAccessor::FindPlayer(ObjectGuid(character));
    if (!player)
        return;

    player->SetInGuild(guild);
}

void ToCloud9GuildHooks::OnGuildMemberRemoved(uint64 /*guild*/, uint64 character)
{
    Player *player = ObjectAccessor::FindPlayer(ObjectGuid(character));
    if (!player)
        return;

    player->SetInGuild(0);
}

void ToCloud9GuildHooks::OnGuildMemberLeft(uint64 /*guild*/, uint64 character)
{
    Player *player = ObjectAccessor::FindPlayer(ObjectGuid(character));
    if (!player)
        return;

    player->SetInGuild(0);
}

// Consumes guild.created: mirrors the new guild in memory on every shard and
// cleans up the charter item and petition state. The gateway already answered
// the leader's client with the turn-in result.
void ToCloud9GuildHooks::OnGuildCreated(uint64 guildId, char* guildName, uint64 leaderGuid, uint64* memberGuids, int memberGuidsSize)
{
    if (sGuildMgr->GetGuildById(uint32(guildId)))
        return;

    ObjectGuid leader = ObjectGuid(leaderGuid);

    std::vector<ObjectGuid> members;
    members.reserve(memberGuidsSize);
    for (int i = 0; i < memberGuidsSize; ++i)
        members.emplace_back(ObjectGuid(memberGuids[i]));

    // Snapshot the charter before any petition cache scrubbing below: the
    // by-id purge at the end needs the petition id, and
    // RemovePetitionsAndSigns drops the petition from the local store.
    uint32 petitionId = 0;
    ObjectGuid petitionItemGuid;
    if (Petition const* petition = sPetitionMgr->GetPetitionByOwnerWithType(leader, GUILD_CHARTER_TYPE))
    {
        petitionId = petition->petitionId;
        petitionItemGuid = petition->petitionGuid;
    }

    // Destroy the charter item (it lives on the shard where the leader is
    // online).
    if (Player* player = ObjectAccessor::FindPlayer(leader))
        if (Item* item = player->GetItemByGuid(petitionItemGuid))
            player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);

    Guild* guild = new Guild();
    if (!guild->MirrorClusterCreated(uint32(guildId), guildName ? guildName : "", leader, members))
    {
        delete guild;
        return;
    }
    sGuildMgr->AddGuild(guild);

    // Same cleanup the core Guild::Create/AddMember path performs: scrubs the
    // members' signatures from the in-memory maps and notifies charter owners.
    Player::RemovePetitionsAndSigns(leader, GUILD_CHARTER_TYPE);
    for (ObjectGuid memberGuid : members)
        Player::RemovePetitionsAndSigns(memberGuid, GUILD_CHARTER_TYPE);

    // By-id purge, same as the core turn-in handler. The type-filtered DELETE
    // in RemovePetitionsAndSigns never matches petition_sign rows (the sign
    // insert stopped writing `type` with the petition_id schema), so without
    // this the signature rows leak — and the (petitionguid=0, playerguid)
    // primary key then rejects any future sign by the same characters.
    // Deletes are idempotent across shards; the store cleanup matters on every
    // shard that has the charter loaded.
    if (petitionId)
    {
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_PETITION_BY_ID);
        stmt->SetData(0, petitionId);
        trans->Append(stmt);

        stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_PETITION_SIGNATURE_BY_ID);
        stmt->SetData(0, petitionId);
        trans->Append(stmt);

        CharacterDatabase.CommitTransaction(trans);

        sPetitionMgr->RemovePetition(petitionItemGuid);
    }
}
