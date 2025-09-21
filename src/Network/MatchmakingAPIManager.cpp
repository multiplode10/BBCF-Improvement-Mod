#include "MatchmakingAPIManager.h"

#include "Core/interfaces.h"
#include "Core/logger.h"
#include "Core/utils.h"
#include "Core/Settings.h"
#include "Overlay/Logger/ImGuiLogger.h"
#include "Game/Room/Room.h"

#include <wininet.h>
#include <random>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "wininet.lib")

MatchmakingAPIManager::MatchmakingAPIManager()
    : m_isRegionOnly(false), m_playerRegion("NA"), m_playerRating(1300), m_gameMode("FT3"), m_currentLobbyId(0)
{
    LOG(2, "MatchmakingAPIManager::MatchmakingAPIManager\n");
    InitializeFromSettings();
}

MatchmakingAPIManager::~MatchmakingAPIManager()
{
    LOG(2, "MatchmakingAPIManager::~MatchmakingAPIManager\n");
    
    // Cleanup any active room on destruction
    if (HasActiveRoom())
    {
        DeleteRoom(m_currentRoomId);
    }
}

APIResponse MatchmakingAPIManager::CreateOrUpdateRoom(const RoomData& roomData)
{
    LOG(2, "MatchmakingAPIManager::CreateOrUpdateRoom\n");

    std::string jsonData = CreateRoomJSON(roomData);
    APIResponse response = SendHTTPRequest("POST", m_apiRoomsEndpoint, jsonData);

    if (response.success)
    {
        m_currentRoomId = roomData.id;
        m_currentRoomData = roomData;
        g_imGuiLogger->Log("[MatchmakingAPI] Room created/updated successfully: %s\n", roomData.id.c_str());
    }
    else
    {
        g_imGuiLogger->Log("[MatchmakingAPI] Failed to create/update room: %s\n", response.errorMessage.c_str());
    }

    return response;
}

APIResponse MatchmakingAPIManager::DeleteRoom(const std::string& roomId)
{
    LOG(2, "MatchmakingAPIManager::DeleteRoom\n");

    if (roomId.empty())
    {
        return APIResponse(false, 0, "", "Room ID is empty");
    }

    std::string endpoint = m_apiRoomsEndpoint + "/" + roomId;
    APIResponse response = SendHTTPRequest("DELETE", endpoint);

    if (response.success)
    {
        m_currentRoomId.clear();
        m_currentRoomData = RoomData();
        g_imGuiLogger->Log("[MatchmakingAPI] Room deleted successfully: %s\n", roomId.c_str());
    }
    else
    {
        g_imGuiLogger->Log("[MatchmakingAPI] Failed to delete room: %s\n", response.errorMessage.c_str());
    }

    return response;
}

void MatchmakingAPIManager::OnSteamLobbyCreated(uint64_t lobbyId)
{
    LOG(2, "MatchmakingAPIManager::OnSteamLobbyCreated - Lobby ID: %llu\n", lobbyId);
    
    // Store the lobby ID for URL generation
    m_currentLobbyId = lobbyId;
    
    // If we have an active room, update it with the new Steam URL
    if (HasActiveRoom())
    {
        m_currentRoomData.steam_url = GenerateSteamURL();
        CreateOrUpdateRoom(m_currentRoomData);
    }
}

void MatchmakingAPIManager::OnRoomCreated()
{
    LOG(2, "MatchmakingAPIManager::OnRoomCreated\n");

    if (!m_apiEnabled || !ShouldCreateAPIRoom())
    {
        LOG(2, "MatchmakingAPIManager::OnRoomCreated - API disabled or not a ranked room, skipping API call\n");
        return;
    }

    // Create room data from current game state
    RoomData roomData;
    roomData.id = GenerateRoomId();
    roomData.host_steamid64 = GetPlayerSteamId();
    roomData.region = GetPlayerRegionFromGame();
    roomData.is_region_only = m_isRegionOnly;
    roomData.mode = GetGameModeFromRoom();
    roomData.target_rating = GetPlayerRatingFromGame();
    roomData.capacity = 1;
    roomData.open_slots = 1;
    roomData.steam_url = GenerateSteamURL();

    CreateOrUpdateRoom(roomData);
}

void MatchmakingAPIManager::OnRoomDestroyed()
{
    LOG(2, "MatchmakingAPIManager::OnRoomDestroyed\n");

    if (HasActiveRoom())
    {
        DeleteRoom(m_currentRoomId);
    }
}

void MatchmakingAPIManager::OnMatchEnd()
{
    LOG(2, "MatchmakingAPIManager::OnMatchEnd\n");

    if (!HasActiveRoom() || !ShouldCreateAPIRoom())
    {
        return;
    }

    // Reset room for new opponent (reuse same room ID and Steam lobby)
    m_currentRoomData.open_slots = 1;
    CreateOrUpdateRoom(m_currentRoomData);
}

void MatchmakingAPIManager::OnPlayerLeft()
{
    LOG(2, "MatchmakingAPIManager::OnPlayerLeft\n");

    if (HasActiveRoom())
    {
        DeleteRoom(m_currentRoomId);
    }
}

int MatchmakingAPIManager::GetPlayerRatingFromAPI(uint64_t steamId)
{
    if (steamId == 0 || !m_apiEnabled)
    {
        return 0;
    }
    
    // Build endpoint for player history/rating
    std::stringstream endpoint;
    endpoint << m_apiPlayerEndpoint << "/" << steamId << "/history";
    
    APIResponse response = SendHTTPRequest("GET", endpoint.str());
    
    if (response.success && !response.response.empty())
    {
        // Simple JSON parsing to extract rating
        // Look for "rating": followed by a number
        size_t ratingPos = response.response.find("\"rating\":");
        if (ratingPos != std::string::npos)
        {
            size_t numberStart = response.response.find_first_of("0123456789", ratingPos);
            if (numberStart != std::string::npos)
            {
                size_t numberEnd = response.response.find_first_not_of("0123456789", numberStart);
                if (numberEnd == std::string::npos)
                {
                    numberEnd = response.response.length();
                }
                
                std::string ratingStr = response.response.substr(numberStart, numberEnd - numberStart);
                try 
                {
                    int rating = std::stoi(ratingStr);
                    if (rating > 0 && rating <= 9999) // Sanity check
                    {
                        LOG(2, "MatchmakingAPIManager::GetPlayerRatingFromAPI - Got rating %d for SteamID %llu\n", rating, steamId);
                        return rating;
                    }
                }
                catch (...)
                {
                    LOG(2, "MatchmakingAPIManager::GetPlayerRatingFromAPI - Failed to parse rating\n");
                }
            }
        }
    }
    else
    {
        LOG(7, "MatchmakingAPIManager::GetPlayerRatingFromAPI - API call failed: %s\n", response.errorMessage.c_str());
    }
    
    return 0; // Return 0 if API call failed
}

std::string MatchmakingAPIManager::GetPlayerRegionFromGame()
{
    // Return the configured region from settings
    return m_playerRegion;
}

int MatchmakingAPIManager::GetPlayerRatingFromGame()
{
    // Try to get rating from API first
    uint64_t steamId = GetPlayerSteamId();
    if (steamId != 0)
    {
        int apiRating = GetPlayerRatingFromAPI(steamId);
        if (apiRating > 0)
        {
            LOG(2, "MatchmakingAPIManager::GetPlayerRatingFromGame - Using API rating: %d\n", apiRating);
            return apiRating;
        }
    }
    
    // For ranked mode, always use 1300 if no API rating available
    LOG(2, "MatchmakingAPIManager::GetPlayerRatingFromGame - Using default ranked rating: 1300\n");
    return 1300;
}

std::string MatchmakingAPIManager::GetGameModeFromRoom()
{
    if (!g_gameVals.pRoom)
    {
        return "FT3"; // Default
    }

    // Extract game mode from room rematch settings
    switch (g_gameVals.pRoom->rematch)
    {
        case RematchType_Ft2:
            return "FT2";
        case RematchType_Ft3:
            return "FT3";
        case RematchType_Ft5:
            return "FT5";
        case RematchType_Ft10:
            return "FT10";
        case RematchType_Unlimited:
            return "FT3"; // Default for unlimited
        default:
            return "FT3";
    }
}

uint64_t MatchmakingAPIManager::GetPlayerSteamId()
{
    if (g_interfaces.pSteamUserWrapper && *g_tempVals.ppSteamUser)
    {
        CSteamID steamId = (*g_tempVals.ppSteamUser)->GetSteamID();
        return steamId.ConvertToUint64();
    }
    return 0;
}

std::string MatchmakingAPIManager::GenerateRoomId()
{
    // Generate "bbcf_room_" + 12 random characters
    const std::string chars = "0123456789abcdef";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, chars.size() - 1);

    std::string randomPart;
    for (int i = 0; i < 12; ++i)
    {
        randomPart += chars[dis(gen)];
    }

    return "bbcf_room_" + randomPart;
}

std::string MatchmakingAPIManager::GenerateSteamURL()
{
    // Get current Steam lobby ID
    // Format: steam://joinlobby/586140/{lobby_id}/{host_steamid64}
    uint64_t steamId = GetPlayerSteamId();
    
    if (steamId == 0)
    {
        return "";
    }

    // Try to get actual lobby ID from Steam matchmaking
    uint64_t lobbyId = GetCurrentSteamLobbyId();
    if (lobbyId == 0)
    {
        // Generate placeholder if no lobby found
        lobbyId = 109775241072023652;
    }

    std::stringstream ss;
    ss << "steam://joinlobby/586140/" << lobbyId << "/" << steamId;
    return ss.str();
}

uint64_t MatchmakingAPIManager::GetCurrentSteamLobbyId()
{
    // Use stored lobby ID from current MatchmakingAPIManager instance
    if (m_currentLobbyId != 0)
    {
        return m_currentLobbyId;
    }
    
    // Try to get the current Steam lobby ID from the matchmaking wrapper
    if (g_interfaces.pSteamMatchmakingWrapper)
    {
        uint64_t lobbyId = g_interfaces.pSteamMatchmakingWrapper->GetCurrentLobbyId();
        if (lobbyId != 0)
        {
            return lobbyId;
        }
    }
    
    // If no valid lobby found, return 0
    return 0;
}

bool MatchmakingAPIManager::ShouldCreateAPIRoom()
{
    if (!g_gameVals.pRoom)
    {
        return false;
    }

    // Only create API room for ranked matches
    if (g_gameVals.pRoom->roomType != RoomType_Ranked)
    {
        return false;
    }

    // Check if room name is "RANKED" (requirement from spec)
    std::wstring roomNameW(g_gameVals.pRoom->roomName);
    std::string roomName = WStringToString(roomNameW);
    
    return roomName == "RANKED";
}

APIResponse MatchmakingAPIManager::SendHTTPRequest(const std::string& method, const std::string& endpoint, const std::string& jsonData)
{
    LOG(2, "MatchmakingAPIManager::SendHTTPRequest: %s %s\n", method.c_str(), endpoint.c_str());

    std::wstring wUrl = StringToWString(m_apiBaseURL + endpoint);
    std::wstring wMethod = StringToWString(method);

    HINTERNET hInternet = nullptr;
    HINTERNET hConnect = nullptr;
    HINTERNET hRequest = nullptr;

    APIResponse response;

    try
    {
        // Initialize WinInet
        hInternet = InternetOpen(L"BBCF-IM-MatchmakingAPI/1.0", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
        if (!hInternet)
        {
            response.errorMessage = "Failed to initialize internet connection";
            return response;
        }

        // Parse URL
        URL_COMPONENTS urlComp = {};
        urlComp.dwStructSize = sizeof(urlComp);
        wchar_t hostname[256] = {};
        wchar_t urlPath[1024] = {};
        urlComp.lpszHostName = hostname;
        urlComp.dwHostNameLength = sizeof(hostname) / sizeof(wchar_t);
        urlComp.lpszUrlPath = urlPath;
        urlComp.dwUrlPathLength = sizeof(urlPath) / sizeof(wchar_t);

        if (!InternetCrackUrl(wUrl.c_str(), 0, 0, &urlComp))
        {
            response.errorMessage = "Failed to parse URL";
            return response;
        }

        // Connect to server
        hConnect = InternetConnect(hInternet, hostname, urlComp.nPort, nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 0);
        if (!hConnect)
        {
            response.errorMessage = "Failed to connect to server";
            return response;
        }

        // Open request
        const wchar_t* acceptTypes[] = { L"application/json", nullptr };
        hRequest = HttpOpenRequest(hConnect, wMethod.c_str(), urlPath, nullptr, nullptr, acceptTypes, 0, 0);
        if (!hRequest)
        {
            response.errorMessage = "Failed to open HTTP request";
            return response;
        }

        // Set headers
        std::wstring headers = L"Content-Type: application/json\r\n";
        if (!HttpAddRequestHeaders(hRequest, headers.c_str(), headers.length(), HTTP_ADDREQ_FLAG_ADD))
        {
            response.errorMessage = "Failed to set request headers";
            return response;
        }

        // Send request
        BOOL requestResult;
        if (method == "POST" && !jsonData.empty())
        {
            requestResult = HttpSendRequest(hRequest, nullptr, 0, (LPVOID)jsonData.c_str(), jsonData.length());
        }
        else
        {
            requestResult = HttpSendRequest(hRequest, nullptr, 0, nullptr, 0);
        }

        if (!requestResult)
        {
            response.errorMessage = "Failed to send HTTP request";
            return response;
        }

        // Get response status
        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        if (HttpQueryInfo(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusCodeSize, nullptr))
        {
            response.statusCode = statusCode;
        }

        // Read response
        std::string responseData;
        char buffer[4096];
        DWORD bytesRead = 0;
        while (InternetReadFile(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0)
        {
            responseData.append(buffer, bytesRead);
        }

        response.response = responseData;
        response.success = (statusCode >= 200 && statusCode < 300);

        if (!response.success)
        {
            std::stringstream ss;
            ss << "HTTP " << statusCode << ": " << responseData;
            response.errorMessage = ss.str();
        }
    }
    catch (...)
    {
        response.errorMessage = "Unexpected error during HTTP request";
    }

    // Cleanup
    if (hRequest) InternetCloseHandle(hRequest);
    if (hConnect) InternetCloseHandle(hConnect);
    if (hInternet) InternetCloseHandle(hInternet);

    return response;
}

std::string MatchmakingAPIManager::CreateRoomJSON(const RoomData& roomData)
{
    std::stringstream json;
    
    json << "{\n";
    json << "  \"id\": \"" << roomData.id << "\",\n";
    json << "  \"host_steamid64\": " << roomData.host_steamid64 << ",\n";
    json << "  \"region\": \"" << roomData.region << "\",\n";
    json << "  \"is_region_only\": " << (roomData.is_region_only ? "true" : "false") << ",\n";
    json << "  \"mode\": \"" << roomData.mode << "\",\n";
    json << "  \"target_rating\": " << roomData.target_rating << ",\n";
    json << "  \"capacity\": " << roomData.capacity << ",\n";
    json << "  \"open_slots\": " << roomData.open_slots << ",\n";
    json << "  \"steam_url\": \"" << roomData.steam_url << "\"\n";
    json << "}";
    
    return json.str();
}

std::string MatchmakingAPIManager::WStringToString(const std::wstring& wstr)
{
    if (wstr.empty())
        return std::string();
    
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, nullptr, nullptr);
    return strTo;
}

std::wstring MatchmakingAPIManager::StringToWString(const std::string& str)
{
    if (str.empty())
        return std::wstring();
    
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), nullptr, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

void MatchmakingAPIManager::InitializeFromSettings()
{
    LOG(2, "MatchmakingAPIManager::InitializeFromSettings\n");
    
    // Read settings from settings.ini - use Settings::settingsIni directly
    m_apiEnabled = Settings::settingsIni.enableMatchmakingAPI;
    m_apiBaseURL = "http://" + Settings::settingsIni.matchmakingAPIHost;
    m_apiRoomsEndpoint = Settings::settingsIni.matchmakingRoomsEndpoint;
    m_apiPlayerEndpoint = Settings::settingsIni.matchmakingPlayerEndpoint;
    m_playerRegion = Settings::settingsIni.playerRegion;
    m_playerRating = Settings::settingsIni.playerRatingFallback;
    m_isRegionOnly = Settings::settingsIni.regionOnlyDefault;
    
    LOG(2, "MatchmakingAPIManager settings:\n");
    LOG(2, "\t- API Enabled: %d\n", m_apiEnabled);
    LOG(2, "\t- API Base URL: %s\n", m_apiBaseURL.c_str());
    LOG(2, "\t- Player Region: %s\n", m_playerRegion.c_str());
    LOG(2, "\t- Rating Default: %d\n", m_playerRating);
}