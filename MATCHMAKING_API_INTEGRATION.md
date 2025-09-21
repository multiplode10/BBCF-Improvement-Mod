# BlazBlue CF Matchmaking API Integration

This document describes the implementation of HTTP API integration for BlazBlue Central Fiction Improvement Mod to sync in-game room state with a matchmaking backend.

## Overview

The integration automatically creates and manages room entries on a matchmaking API server when players create ranked lobbies in BlazBlue CF. This enables web-based matchmaking where players can browse active lobbies and join them directly through Steam URLs.

## Implementation Components

### 1. MatchmakingAPIManager (`src/Network/MatchmakingAPIManager.h/cpp`)

Core class responsible for HTTP API communication:

- **HTTP Operations**: POST for room creation/updates, DELETE for room cleanup
- **Room Data Management**: Generates unique room IDs, extracts game state, builds JSON payloads
- **Steam Integration**: Generates Steam lobby URLs for direct game joining
- **Settings**: Region-only matchmaking preference, player region, rating, game mode

Key Methods:
```cpp
APIResponse CreateOrUpdateRoom(const RoomData& roomData);
APIResponse DeleteRoom(const std::string& roomId);
void OnRoomCreated();           // Called when lobby created
void OnRoomDestroyed();         // Called when lobby closed  
void OnMatchEnd();              // Called after match ends
void OnPlayerLeft();            // Called when host leaves
```

### 2. Room Integration Hooks

Integration points with existing game systems:

- **RoomManager::JoinRoom()**: Calls `OnRoomCreated()` when joining/creating rooms
- **RoomManager destructor**: Calls `OnRoomDestroyed()` for cleanup
- **MatchState::OnMatchEnd()**: Calls `OnMatchEnd()` to reset room for new opponents

### 3. Steam Lobby Tracking

Enhanced `SteamMatchmakingWrapper` to track lobby creation:

- **CreateLobby()**: Stores pending lobby creation API call
- **JoinLobby()**: Notifies MatchmakingAPIManager with lobby ID
- **LeaveLobby()**: Triggers room cleanup when host leaves

### 4. UI Integration

Added matchmaking settings to `RoomWindow`:

- **Region-Only Checkbox**: "Only match with players in your region"
- **Visibility**: Only shown for ranked rooms
- **Real-time Updates**: Changes immediately affect room creation

## API Endpoints

### Base URL
```
http://localhost:8080/api
```

### Create/Update Room
```
POST /api/rooms
Content-Type: application/json

{
  "id": "bbcf_room_a1b2c3d4e5f6",
  "host_steamid64": 76561198123456789,
  "region": "NA", 
  "is_region_only": false,
  "mode": "FT3",
  "target_rating": 1500,
  "capacity": 1,
  "open_slots": 1,
  "steam_url": "steam://joinlobby/586140/109775241072023652/76561198123456789"
}
```

### Delete Room
```
DELETE /api/rooms/bbcf_room_a1b2c3d4e5f6
```

## Room Creation Logic

Rooms are **ONLY** created when:
1. Room type is `RoomType_Ranked`
2. Room name exactly equals `"RANKED"`
3. Player creates or joins a lobby (not spectating)

This ensures API integration only activates for proper ranked matches.

## Data Extraction

### Game State Sources
- **Steam ID**: From Steam User API
- **Region**: Configurable via UI (default: "NA")  
- **Game Mode**: Extracted from room rematch settings (FT2, FT3, FT5, etc.)
- **Rating**: Configurable (future: extract from game memory)
- **Steam URL**: Generated with lobby ID and host Steam ID

### Room ID Generation
Format: `"bbcf_room_" + 12_random_hex_chars`
Example: `"bbcf_room_a1b2c3d4e5f6"`

## Event Flow

### Room Creation
```
1. Player creates ranked lobby with name "RANKED"
2. RoomManager::JoinRoom() called
3. MatchmakingAPIManager::OnRoomCreated() triggered
4. Room data extracted from game state
5. HTTP POST to /api/rooms
6. Room ID stored for future operations
```

### Match End & Reset
```
1. Match completes, opponent leaves
2. MatchState::OnMatchEnd() called  
3. Room reset with open_slots = 1
4. HTTP POST to /api/rooms (same room ID)
5. Room now available for new opponents
```

### Room Cleanup
```
1. Host closes lobby OR leaves game
2. RoomManager destructor called
3. MatchmakingAPIManager::OnRoomDestroyed() triggered
4. HTTP DELETE to /api/rooms/{room_id}
5. Room removed from matchmaking system
```

## Error Handling

- **Network Timeouts**: Non-blocking, logs errors but doesn't interrupt gameplay
- **API Server Down**: Graceful failure, mod continues functioning normally  
- **Invalid Responses**: Detailed error logging for debugging
- **Steam Integration Failures**: Falls back to placeholder lobby IDs

## Testing Scenarios

### 1. Basic Room Creation
1. Start game, go to Network Mode
2. Create Ranked Room with name "RANKED" 
3. Check API logs - should see room creation
4. Close lobby - should see room deletion

### 2. Match Flow Testing  
1. Create ranked room, wait for opponent
2. Play match to completion
3. When opponent leaves, stay in lobby
4. Check API logs - should see room reset (open_slots = 1)
5. Leave lobby - should see room deletion

### 3. Region Settings
1. Open Room Window overlay (during lobby)
2. Toggle "Region-Only Matchmaking" checkbox
3. Setting should persist for future room creations
4. Room data should reflect is_region_only setting

### 4. Non-Ranked Rooms
1. Create Player Match or Lobby room  
2. No API calls should be made
3. Create ranked room but with different name
4. No API calls should be made

## Configuration

### Default Settings
- **API Base URL**: `http://localhost:8080`
- **Player Region**: `"NA"`
- **Target Rating**: `1500`
- **Game Mode**: `"FT3"`
- **Region-Only**: `false`

### Runtime Configuration
Settings can be changed via the Room Window UI:
- Region-only preference (checkbox)
- Player region (future enhancement)
- Target rating (future enhancement)

## Web Integration Benefits

### For Mod Users (In-Game)
- Create lobbies normally - no extra steps required
- Automatic web visibility increases opponent pool
- Cross-platform matchmaking with web users
- Steam integration enables seamless joining

### For Web Users  
- Browse active lobbies at `/matches` endpoint
- Queue for matches at `/matchmaking` endpoint
- One-click join via Steam URLs
- Real-time lobby updates every 5 seconds

### System Features
- Automatic room cleanup prevents stale entries
- Rating-based opponent matching
- Regional and global matchmaking options
- Steam integration for seamless game launching

## File Changes Summary

### New Files
- `src/Network/MatchmakingAPIManager.h`
- `src/Network/MatchmakingAPIManager.cpp`

### Modified Files  
- `src/Core/interfaces.h` - Added MatchmakingAPIManager forward declaration
- `src/Core/interfaces.cpp` - Added initialization and cleanup
- `src/Network/RoomManager.cpp` - Added room creation/destruction hooks
- `src/Game/MatchState.cpp` - Added match end hook
- `src/SteamApiWrapper/SteamMatchmakingWrapper.h/cpp` - Added lobby tracking
- `src/Overlay/Window/RoomWindow.h/cpp` - Added region-only checkbox
- `BBCF_IM.vcxproj` - Added new files to project
- `BBCF_IM.vcxproj.filters` - Added new files to filters

## Dependencies

- **WinInet**: For HTTP requests (already used by project)
- **Steam API**: For lobby and user information (already available)
- **ImGui**: For UI components (already available)
- **Existing mod infrastructure**: Hooks, logging, room management

## Future Enhancements

1. **Real Rating Integration**: Extract player BP/rating from game memory
2. **Dynamic Region Detection**: Auto-detect player region from Steam/network
3. **Match Result Reporting**: Send match outcomes to API for ranking
4. **Advanced Matchmaking**: Skill-based opponent selection
5. **Statistics Integration**: Track win/loss records, match history
6. **Tournament Mode**: Support for bracket-based competitions