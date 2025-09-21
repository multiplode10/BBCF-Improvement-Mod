# Testing BlazBlue CF Matchmaking API Integration

This guide explains how to test the newly implemented matchmaking API integration in the BBCF Improvement Mod.

## Quick Start

1. **Start the mock API server:**
   ```bash
   python test_api_server.py
   ```

2. **Launch BlazBlue CF** with the Improvement Mod

3. **Create a ranked room** with the name "RANKED"

4. **Watch the console output** - you should see API calls being made

## Detailed Testing Guide

### Prerequisites

- BBCF Improvement Mod compiled with matchmaking integration
- Python 3.x for running the mock API server
- BlazBlue Central Fiction game

### Step 1: Start Mock API Server

```bash
cd BBCF-Improvement-Mod
python test_api_server.py
```

You should see:
```
BlazBlue CF Matchmaking API Mock Server
Server running on: http://localhost:8080
Available endpoints:
  GET  /api/rooms         - List all active rooms
  POST /api/rooms         - Create or update room
  DELETE /api/rooms/{id}  - Delete specific room
```

### Step 2: Basic Room Creation Test

1. Launch BlazBlue Central Fiction
2. Go to **Network Mode**
3. Select **Ranked Match**
4. Create a room with these settings:
   - **Room Name**: `RANKED` (exactly this - case sensitive)
   - **Game Mode**: Any (FT2, FT3, etc.)
   - **Other settings**: Any

**Expected Results:**
- Mock server console shows: `CREATED ROOM - ID: bbcf_room_[random_id]`
- Room data includes your Steam ID, region, game mode
- `open_slots` should be `1`

### Step 3: Region-Only Setting Test

1. While in the lobby, open the **Room Window** overlay
2. Look for **"Region-Only Matchmaking"** checkbox
3. Toggle the checkbox on/off
4. Close and recreate the room

**Expected Results:**
- Checkbox should appear only in ranked rooms
- Room API calls should show `"is_region_only": true/false` based on checkbox state
- Setting should persist between room creations

### Step 4: Room Cleanup Test

1. Create a ranked room (as in Step 2)
2. **Close the lobby** or **exit the game**

**Expected Results:**
- Mock server console shows: `DELETED ROOM - ID: bbcf_room_[same_id]`
- `GET /api/rooms` should show empty list

### Step 5: Match End Reset Test

This test requires two players or AI opponent:

1. Create ranked room and start a match
2. Complete the match (win or lose)
3. Stay in the lobby after opponent leaves

**Expected Results:**
- Mock server shows: `UPDATED ROOM - ID: [same_id]`
- `open_slots` resets to `1`
- Same room ID is reused

### Step 6: Non-Ranked Room Test

1. Create a **Player Match** or **Lobby** room
2. Try different room names (not "RANKED")

**Expected Results:**
- **No API calls should be made** to the server
- Only ranked rooms with name "RANKED" trigger API integration

## Manual API Testing

You can test the API endpoints directly:

### List Active Rooms
```bash
curl http://localhost:8080/api/rooms
```

### Create Test Room
```bash
curl -X POST http://localhost:8080/api/rooms \
  -H "Content-Type: application/json" \
  -d '{
    "id": "test_room_123",
    "host_steamid64": 76561198123456789,
    "region": "NA",
    "is_region_only": false,
    "mode": "FT3",
    "target_rating": 1500,
    "capacity": 1,
    "open_slots": 1,
    "steam_url": "steam://joinlobby/586140/109775241072023652/76561198123456789"
  }'
```

### Delete Test Room
```bash
curl -X DELETE http://localhost:8080/api/rooms/test_room_123
```

## Expected Console Output

### Successful Room Creation
```
[2024-01-15 10:30:45] CREATED ROOM - ID: bbcf_room_a1b2c3d4e5f6
  Host: 76561198123456789
  Region: NA (Global)
  Mode: FT3
  Rating: 1500
  Steam URL: steam://joinlobby/586140/109775241072023652/76561198123456789
```

### Room Update After Match
```
[2024-01-15 10:35:20] UPDATED ROOM - ID: bbcf_room_a1b2c3d4e5f6
  Host: 76561198123456789
  Region: NA (Global)
  Mode: FT3
  Rating: 1500
  Steam URL: steam://joinlobby/586140/109775241072023652/76561198123456789
```

### Room Deletion
```
[2024-01-15 10:40:10] DELETED ROOM - ID: bbcf_room_a1b2c3d4e5f6
  Host: 76561198123456789
  Region: NA
```

## Troubleshooting

### No API Calls Being Made

Check that:
- Room type is **Ranked Match** (not Player Match or Lobby)
- Room name is exactly `"RANKED"`
- Mock server is running on `localhost:8080`
- Game is not in offline mode

### API Calls Failing

Check:
- Mock server console for error messages
- Network connectivity to `localhost:8080`
- Windows firewall settings
- Antivirus blocking HTTP requests

### Checkbox Not Appearing

Verify:
- Room Window overlay is enabled
- Currently in a ranked room
- MatchmakingAPIManager is properly initialized

### Steam URL Issues

Common problems:
- Lobby ID is placeholder (normal for testing)
- Steam ID is 0 (check Steam integration)
- URL format incorrect (should start with `steam://joinlobby/586140/`)

## Log Analysis

### Game Logs
Look for entries containing:
- `MatchmakingAPIManager::`
- `CreateOrUpdateRoom`
- `DeleteRoom`
- `OnRoomCreated`

### API Server Logs
Look for HTTP requests:
- `POST /api/rooms`
- `DELETE /api/rooms/[id]`
- `GET /api/rooms`

## Performance Testing

### Stress Test
1. Create and delete rooms rapidly
2. Monitor for memory leaks or hangs
3. Check API response times

### Network Failure Test
1. Stop mock server while in room
2. Game should continue normally
3. No crashes or blocking behavior

## Integration with Real API Server

To test with a real matchmaking server:

1. **Update API URL** in `MatchmakingAPIManager.cpp`:
   ```cpp
   const std::string API_BASE_URL = "https://your-api-server.com";
   ```

2. **Configure CORS** on your server for web interface testing

3. **Implement authentication** if required by your API

4. **Test with web interface** - users should see your rooms appear

## Web Matchmaking Test

If you have a web interface:

1. Create room in-game with name "RANKED"
2. Open web browser to your matchmaking site
3. Navigate to active matches/rooms page
4. Your room should appear in the list
5. Steam URL should be clickable and launch the game

## Expected Integration Flow

```
Game Event → Mod Hook → MatchmakingAPIManager → HTTP API → Web Interface
     ↓           ↓               ↓                  ↓           ↓
Room Created → OnRoomCreated → POST /api/rooms → Database → Web Display
Match End   → OnMatchEnd    → POST /api/rooms → Update   → Refresh List  
Room Closed → OnDestroyed   → DELETE /rooms   → Remove   → Remove Display
```

## Common Test Scenarios

### Scenario 1: Normal Matchmaking Flow
1. Player creates ranked room
2. Web user sees room and joins via Steam URL
3. Match is played
4. Loser leaves, winner stays
5. Room resets for new opponent

### Scenario 2: Multiple Rooms
1. Several players create ranked rooms
2. All rooms appear on web interface
3. Each has unique Steam URL
4. Rooms are properly cleaned up when closed

### Scenario 3: Region Filtering
1. Player enables region-only mode
2. Creates room with region preference
3. Web interface can filter by region
4. Only matching region players should see room

## Success Criteria

✅ **Basic Integration**
- API calls made only for ranked rooms named "RANKED"
- Room data accurately reflects game state
- HTTP requests succeed and return proper responses

✅ **UI Integration**  
- Region-only checkbox appears in ranked rooms
- Setting affects API calls immediately
- Checkbox state persists

✅ **Event Handling**
- Room creation triggers API call
- Match end resets room availability  
- Room destruction cleans up API entry

✅ **Error Handling**
- Network failures don't crash game
- Invalid responses are logged but not blocking
- Game continues normally if API unavailable

✅ **Steam Integration**
- Steam URLs generated correctly
- Lobby IDs tracked properly
- URLs enable direct game joining

## Next Steps

After successful testing:

1. **Deploy real API server** with proper database
2. **Create web interface** for room browsing
3. **Add authentication** and user management
4. **Implement advanced features** like rating-based matching
5. **Monitor production usage** and optimize performance

## Support

For issues with the integration:

1. Check game logs for MatchmakingAPIManager entries
2. Verify API server responses with curl/Postman
3. Test with mock server to isolate issues
4. Review Steam integration for lobby tracking problems

Remember: The integration is designed to be non-intrusive - if the API fails, the game should continue working normally.