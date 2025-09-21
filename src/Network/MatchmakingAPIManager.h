#pragma once
#include <string>
#include <functional>

struct RoomData
{
    std::string id;
    uint64_t host_steamid64;
    std::string region;
    bool is_region_only;
    std::string mode;
    int target_rating;
    int capacity;
    int open_slots;
    std::string steam_url;

    RoomData() : host_steamid64(0), is_region_only(false), target_rating(1300), capacity(1), open_slots(1) {}
};

struct APIResponse
{
    bool success;
    int statusCode;
    std::string response;
    std::string errorMessage;

    APIResponse() : success(false), statusCode(0) {}
    APIResponse(bool success, int statusCode, const std::string& response, const std::string& error = "")
        : success(success), statusCode(statusCode), response(response), errorMessage(error) {}
};

class MatchmakingAPIManager
{
public:
    MatchmakingAPIManager();
    ~MatchmakingAPIManager();

    // Main API operations
    APIResponse CreateOrUpdateRoom(const RoomData& roomData);
    APIResponse DeleteRoom(const std::string& roomId);

    // Room management
    void OnRoomCreated();
    void OnRoomDestroyed();
    void OnMatchEnd();
    void OnPlayerLeft();
    void OnSteamLobbyCreated(uint64_t lobbyId);

    // Settings
    void SetRegionOnlyMode(bool regionOnly) { m_isRegionOnly = regionOnly; }
    bool GetRegionOnlyMode() const { return m_isRegionOnly; }

    void SetPlayerRegion(const std::string& region) { m_playerRegion = region; }
    void SetPlayerRating(int rating) { m_playerRating = rating; }
    void SetGameMode(const std::string& mode) { m_gameMode = mode; }

    // Current room tracking
    std::string GetCurrentRoomId() const { return m_currentRoomId; }
    bool HasActiveRoom() const { return !m_currentRoomId.empty(); }

    // Helper functions to get game state
    std::string GetPlayerRegionFromGame();
    int GetPlayerRatingFromGame();
    int GetPlayerRatingFromAPI(uint64_t steamId);
    std::string GetGameModeFromRoom();
    uint64_t GetPlayerSteamId();
    std::string GenerateRoomId();
    std::string GenerateSteamURL();
    bool ShouldCreateAPIRoom();
    uint64_t GetCurrentSteamLobbyId();

private:
    // HTTP utilities
    APIResponse SendHTTPRequest(const std::string& method, const std::string& endpoint, const std::string& jsonData = "");
    std::string CreateRoomJSON(const RoomData& roomData);
    std::string WStringToString(const std::wstring& wstr);
    std::wstring StringToWString(const std::string& str);
    
    // Settings
    bool m_isRegionOnly;
    std::string m_playerRegion;
    int m_playerRating;
    std::string m_gameMode;

    // Current room state
    std::string m_currentRoomId;
    RoomData m_currentRoomData;
    uint64_t m_currentLobbyId;

    // Settings initialization
    void InitializeFromSettings();
    
    // API configuration
    std::string m_apiBaseURL;
    std::string m_apiRoomsEndpoint;
    std::string m_apiPlayerEndpoint;
    bool m_apiEnabled;
};